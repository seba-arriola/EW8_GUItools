/* Estado vivo de csnrvQT: anillo HYPO_RING_REF + seed de BD.
 *
 * Port de `update_summary_loop` (csnrv.c:410-536) y `Lookup`/`Status`
 * (csnrv.c:238-284), sin GTK y sin globales. La UI solo pinta el snapshot.
 *
 * Decisiones:
 *   - se queda con el evento de MAYOR hora de origen (crv_event_decide): una
 *     iteracion de un evento antiguo no degrada la vista. Mismo evento = mismo
 *     qid y |dt0| <= CRV_EVENT_MATCH_SEC; el seed de BD entra con version 0 y
 *     el refinador vivo envuelve 9999 -> 1000, asi que el version no identifica;
 *   - TYPE_MAGNITUDE de un qid ajeno se ignora;
 *   - GET_MISS/GET_NOTRACK traen mensaje y se procesan; GET_TOOBIG no copia
 *     (recsize es el tamano del mensaje), asi que se descarta;
 *   - Start se envia como TYPE_RESTART (no aplica a este visor);
 *   - el heartbeat y la senal de parada son los de `ewgui_ring_*`.
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

#include "crv_internal.h"

struct CrvCore {
    const CrvConfig *cfg;       /* prestada */
    EwGuiRing ring;
    int ring_ok;
    CrvDb db;
    unsigned char inst, mod;
    unsigned char type_hb, type_err, type_arc, type_mag;
    MSG_LOGO logo_arc, logo_mag;
    CrvSnapshot snap;
    EwGuiHeartbeat hb;
    pid_t pid;
    int quit;
};

static void put_heartbeat(CrvCore *c)
{
    MSG_LOGO logo;
    char msg[256];
    time_t t;

    if (!c || !c->ring_ok)
        return;
    logo.instid = c->inst;
    logo.mod = c->mod;
    logo.type = c->type_hb;
    time(&t);
    snprintf(msg, sizeof(msg), "%ld %d\n", (long)t, (int)c->pid);
    ewgui_ring_put(&c->ring, &logo, (long)strlen(msg), msg);
}

CrvCore *crv_core_new(const CrvConfig *cfg)
{
    CrvCore *c;

    if (!cfg)
        return NULL;
    c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->cfg = cfg;
    c->pid = getpid();
    crv_mag_reset(&c->snap.mag, 0);
    return c;
}

void crv_core_free(CrvCore *c)
{
    if (!c)
        return;
    if (c->ring_ok)
        ewgui_ring_detach(&c->ring);
    crv_db_close(&c->db);
    free(c);
}

int crv_core_open_db(CrvCore *c)
{
    int rc;

    if (!c)
        return -1;
    rc = crv_db_open(&c->db, c->cfg->db_env_file);
    if (rc < 0) {
        crv_logf("BD: %s\n", c->db.error);
        return -1;
    }
    if (rc > 0)
        crv_logf("BD conectada (%s)\n", c->cfg->db_env_file);
    return 0;
}

int crv_core_seed_db(CrvCore *c)
{
    int got;

    if (!c)
        return 0;
    got = crv_db_seed(&c->db, c->cfg->db_hours, &c->snap);
    if (got) {
        crv_logf("seed BD: qid %d (%.3f, %.3f)\n",
                 c->snap.qid, c->snap.lat, c->snap.lon);
    }
    return got;
}

int crv_core_attach_ring(CrvCore *c)
{
    long key;
    unsigned char inst = 0, mod = 0;

    if (!c)
        return -1;
    if (c->ring_ok)
        return 0;

    if (ewgui_ring_resolve(c->cfg->ring, c->cfg->my_module,
                           &key, &inst, &mod) != 0) {
        crv_logf("no se pudo resolver el anillo %s / modulo %s\n",
                 c->cfg->ring, c->cfg->my_module);
        return -1;
    }
    /* No llamar a tport_attach si el segmento no existe: aborta el proceso. */
    if (key < 0 || shmget(key, 0, 0) < 0) {
        crv_logf("anillo inexistente <%s> (key %ld)\n", c->cfg->ring, key);
        return -1;
    }

    ewgui_ring_attach(&c->ring, key, inst, mod);
    c->ring_ok = 1;
    c->inst = inst;
    c->mod = mod;

    if (GetType("TYPE_HEARTBEAT", &c->type_hb) != 0 ||
        GetType("TYPE_ERROR", &c->type_err) != 0 ||
        GetType("TYPE_HYP2000ARC", &c->type_arc) != 0 ||
        GetType("TYPE_MAGNITUDE", &c->type_mag) != 0) {
        crv_logf("faltan tipos en earthworm.d (HEARTBEAT/ERROR/HYP2000ARC/MAGNITUDE)\n");
        return -1;
    }

    c->logo_arc.instid = 0;
    c->logo_arc.mod = 0;
    c->logo_arc.type = c->type_arc;
    c->logo_mag.instid = 0;
    c->logo_mag.mod = 0;
    c->logo_mag.type = c->type_mag;

    crv_logf("anillo %s conectado (key %ld, inst %u, mod %u)\n",
             c->cfg->ring, key, (unsigned)inst, (unsigned)mod);
    return 0;
}

int crv_core_ingest_arc(CrvCore *c, const char *msg, size_t len)
{
    CrvArc a;
    CrvEventDecision why;
    int flags;

    if (!c || crv_arc_parse(msg, len, &a) != 0)
        return 0;

    /* El visor es "el ultimo sismo", no "el ultimo mensaje": un ARC de un
     * evento mas antiguo (iteracion de un refinador vivo) se ignora. */
    why = crv_event_decide(&c->snap, &a, CRV_EVENT_MATCH_SEC);
    if (why == CRV_EVENT_STALE) {
        crv_logf("ARC qid=%d v=%d t0=%.2f -> ignorado (evento mas antiguo)\n",
                 a.qid, a.version, a.t0);
        return 0;
    }
    if (why == CRV_EVENT_INVALID) {
        crv_logf("ARC qid=%d v=%d t0=%.2f -> ignorado (parse invalido)\n",
                 a.qid, a.version, a.t0);
        return 0;
    }

    c->snap.has_data = 1;
    c->snap.has_depth = 1;
    c->snap.origin_time = a.t0;
    c->snap.lat = a.lat;
    c->snap.lon = a.lon;
    c->snap.depth_km = a.depth_km;
    c->snap.qid = a.qid;
    c->snap.version = a.version;
    if (c->snap.lon > 180.0)
        c->snap.lon -= 360.0;

    if (why == CRV_EVENT_UPDATE) {
        flags = CRV_CHANGED_SUMMARY;   /* conserva magnitudes, zoom y pan */
    } else {
        crv_mag_reset(&c->snap.mag, a.qid);
        flags = CRV_CHANGED_SUMMARY | CRV_CHANGED_EVENT;
    }
    crv_logf("ARC qid=%d v=%d t0=%.2f -> aceptado (%s)\n",
             a.qid, a.version, a.t0,
             why == CRV_EVENT_NEW ? "evento nuevo" :
             why == CRV_EVENT_FIRST ? "primero" : "iteracion");
    return flags;
}

int crv_core_ingest_mag(CrvCore *c, const char *msg, size_t len)
{
    CrvMagMsg m;

    if (!c || crv_magmsg_parse(msg, len, &m) != 0)
        return 0;
    if (!crv_mag_apply(&c->snap.mag, &m))
        return 0;
    return CRV_CHANGED_MAG;
}

int crv_core_tick(CrvCore *c, time_t now)
{
    int flags = 0;

    if (!c)
        return 0;

    if (c->ring_ok) {
        MSG_LOGO logos[2];
        MSG_LOGO reclogo;
        long recsize;
        char msg[65536];
        int res;

        logos[0] = c->logo_arc;
        logos[1] = c->logo_mag;

        while ((res = ewgui_ring_drain(&c->ring, logos, 2, &reclogo, &recsize,
                                       msg, sizeof(msg) - 1)) != GET_NONE) {
            if (res == GET_TOOBIG)
                continue;   /* no hubo copia: recsize es el tamano real */
            if (recsize <= 0 || recsize >= (long)sizeof(msg) - 1)
                continue;
            msg[recsize] = '\0';

            if (reclogo.type == c->type_mag)
                flags |= crv_core_ingest_mag(c, msg, (size_t)recsize);
            else if (reclogo.type == c->type_arc)
                flags |= crv_core_ingest_arc(c, msg, (size_t)recsize);
        }

        if (ewgui_heartbeat_due(&c->hb, (double)now, c->cfg->heart_beat_int))
            put_heartbeat(c);

        if (ewgui_ring_should_quit(&c->ring.region, (int)c->pid)) {
            c->quit = 1;
            flags |= CRV_CHANGED_QUIT;
        }
    }
    return flags;
}

void crv_core_snapshot(const CrvCore *c, CrvSnapshot *out)
{
    if (!out)
        return;
    if (!c) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = c->snap;
}

const char *crv_core_db_error(const CrvCore *c)
{
    if (!c)
        return "";
    return c->db.error;
}

int crv_core_should_quit(const CrvCore *c)
{
    return c ? c->quit : 0;
}
