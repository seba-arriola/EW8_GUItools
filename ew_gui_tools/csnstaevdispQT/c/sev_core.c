/* Estado vivo: BD + anillo + heartbeat, sin GTK ni globales.
 *
 * `csnstaevdisp` NO lee mensajes del anillo: solo se engancha para latir y
 * atender TERMINATE/peticion de parada (csnstaevdisp.c:236-293). Por eso aqui
 * no hay cursor ni drenaje.
 *
 * Paridad que se conserva:
 *   - fallback `MOD_WILDCARD` si el modulo no esta en earthworm.d;
 *   - el fallo de BD no es fatal: se sigue con 0 sismos;
 *   - la recarga de historial NO toca la vista (zoom/pan persisten).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include "earthworm.h"
#include "transport.h"

#include "ewgui/ring.h"

#include "sev_capi.h"
#include "sev_internal.h"

struct SevCore {
    const SevConfig *cfg;       /* prestada */
    SevDb *db;
    char   db_error[256];       /* "" si todo bien */

    SevStation sta[SEV_MAX_STATIONS];
    size_t     nsta;
    double     elev_min, elev_max;
    SevEvent   ev[SEV_MAX_EVENTS];
    size_t     nev;

    EwGuiRing ring;
    int       ring_ok;
    unsigned char inst, mod;
    unsigned char type_hb, type_err;
    EwGuiHeartbeat hb;
    pid_t     pid;
    int       quit;
};

SevCore *sev_core_new(const SevConfig *cfg)
{
    SevCore *c;

    if (!cfg)
        return NULL;
    c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->cfg = cfg;
    c->pid = getpid();
    c->nsta = sev_stations_load(cfg->sta_file, c->sta, SEV_MAX_STATIONS);
    sev_stations_range(c->sta, c->nsta, &c->elev_min, &c->elev_max);
    return c;
}

void sev_core_free(SevCore *c)
{
    if (!c)
        return;
    if (c->ring_ok)
        ewgui_ring_detach(&c->ring);
    sev_db_free(c->db);
    free(c);
}

int sev_core_open_db(SevCore *c)
{
    char err[256] = {0};

    if (!c)
        return -1;
    c->db = sev_db_open(c->cfg, err, sizeof err);
    if (!c->db) {
        snprintf(c->db_error, sizeof(c->db_error), "%s", err);
        sev_logf("BD: %s\n", c->db_error);
        return -1;
    }
    c->db_error[0] = '\0';
    return 0;
}

int sev_core_attach_ring(SevCore *c)
{
    long key;
    unsigned char inst = 0, mod = 0;

    if (!c)
        return -1;
    if (c->ring_ok)
        return 0;

    key = GetKey((char *)c->cfg->ring);
    if (key == -1) {
        sev_logf("anillo invalido <%s>\n", c->cfg->ring);
        return -1;
    }
    /* No llamar a tport_attach si el segmento no existe: aborta el proceso. */
    if (shmget(key, 0, 0) < 0) {
        sev_logf("anillo inexistente <%s> (key %ld)\n", c->cfg->ring, key);
        return -1;
    }

    if (GetLocalInst(&inst) != 0)
        inst = 0;
    if (GetModId((char *)c->cfg->my_module, &mod) != 0) {
        if (GetModId("MOD_WILDCARD", &mod) != 0)
            mod = 0;
    }
    if (GetType("TYPE_HEARTBEAT", &c->type_hb) != 0)
        c->type_hb = 0;
    if (GetType("TYPE_ERROR", &c->type_err) != 0)
        c->type_err = 0;

    ewgui_ring_attach(&c->ring, key, inst, mod);
    c->inst = inst;
    c->mod = mod;
    c->ring_ok = 1;
    return 0;
}

int sev_core_refresh(SevCore *c)
{
    size_t n = 0;

    if (!c)
        return -1;
    c->nev = 0;
    if (!c->db)
        return 0;   /* paridad: sin BD, lista vacia y sin error */

    if (sev_db_refresh(c->db, c->cfg->db_hours,
                       c->ev, SEV_MAX_EVENTS, &n) != 0)
        return -1;   /* paridad: NumQuakes=0 y return */

    c->nev = n;
    return 0;
}

int sev_core_background(SevCore *c, double now)
{
    if (!c)
        return 0;

    if (c->ring_ok &&
        ewgui_heartbeat_due(&c->hb, now, c->cfg->heart_beat_int)) {
        MSG_LOGO logo;
        char msg[256];
        time_t t = (time_t)now;

        logo.instid = c->inst;
        logo.mod = c->mod;
        logo.type = c->type_hb;
        snprintf(msg, sizeof(msg), "%ld %d\n", (long)t, (int)c->pid);
        ewgui_ring_put(&c->ring, &logo, (long)strlen(msg), msg);
    }

    if (c->ring_ok && ewgui_ring_should_quit(&c->ring.region, c->pid)) {
        c->quit = 1;
        return 1;
    }
    return 0;
}

void sev_core_snapshot(const SevCore *c, SevSnapshot *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!c)
        return;

    memcpy(out->sta, c->sta, c->nsta * sizeof(c->sta[0]));
    out->nsta = c->nsta;
    out->elev_min = c->elev_min;
    out->elev_max = c->elev_max;
    memcpy(out->ev, c->ev, c->nev * sizeof(c->ev[0]));
    out->nev = c->nev;
    out->db_ok = c->db ? 1 : 0;
}

const char *sev_core_db_error(const SevCore *c)
{
    if (!c)
        return "sin core";
    return c->db_error;
}

int sev_core_should_quit(const SevCore *c)
{
    return c ? c->quit : 0;
}
