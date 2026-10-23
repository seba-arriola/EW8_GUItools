/* Estado del CONTROL_RING para ew_controllerQT.
 *
 * Port de la maquina de estado de ew_controller_main.c:166-240 y de la tabla de
 * sensibilidad de ew_controller_gui.c:125-136, SIN GTK. Es el unico sitio donde
 * vive esa logica: la UI solo pinta lo que dice ecq_table().
 *
 * Decisiones que se conservan del modulo GTK:
 *   - identidad por PID (nunca por nombre): startstop_unix.d puede tener dos
 *     `Process` con el mismo nombre;
 *   - GET_MISS y GET_NOTRACK traen mensaje: se parsean y se sigue drenando;
 *     GET_NONE/GET_TOOBIG no, y en TOOBIG recsize es el tamano del mensaje
 *     (transport.c:832-834), asi que se acota antes de cerrar la cadena;
 *   - tras PollInt*4 sin respuesta se deja de afirmar "Alive" (`?`);
 *   - Start se envia como TYPE_RESTART (ew_controller_gui.c:248-254).
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

#include "ewgui/ctrl.h"
#include "ewgui/ring.h"

#include "ecq_capi.h"

/* Mismo tope que ew_controller.h:27. */
#define ECQ_STATUS_MAX 16384

struct EcqCore {
    EcqConfig cfg;

    EwGuiRing ring;
    int ring_ok;

    unsigned char type_hb, type_req, type_status, type_stop, type_restart, type_reconfig;
    unsigned char wc_inst, wc_mod;
    int got_inst, got_mod;

    EwCtrlStatus status;
    int  seen_status;
    time_t last_status;
    time_t last_poll;
    EwGuiHeartbeat hb;

    int selected_pid;
    int noreply;

    unsigned seq;
    EcqRow     rows[ECQ_MAX_ROWS];
    EcqRingRow ringrows[ECQ_MAX_ROWS];
    EcqTable   table;

    char target[160];
    char detail[256];
};

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void copy_str(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0)
        return;
    snprintf(dst, n, "%s", src ? src : "");
}

static int get_type_or0(const char *name, unsigned char *out)
{
    if (GetType((char *)name, out) != 0) {
        *out = 0;
        return -1;
    }
    return 0;
}

/* Copia el estado parseado a la vista plana que consume la UI. */
static void build_view(EcqCore *c)
{
    int i;

    c->table.nmods = c->status.nmods;
    c->table.nrings = c->status.nrings;
    for (i = 0; i < c->status.nmods && i < ECQ_MAX_ROWS; i++) {
        const EwCtrlMod *m = &c->status.mods[i];
        copy_str(c->rows[i].name, sizeof(c->rows[i].name), m->name);
        c->rows[i].pid = m->pid;
        copy_str(c->rows[i].status, sizeof(c->rows[i].status),
                 c->noreply ? "?" : m->status);
        copy_str(c->rows[i].details, sizeof(c->rows[i].details), m->detalle);
        copy_str(c->rows[i].config, sizeof(c->rows[i].config), m->config);
        copy_str(c->rows[i].cfgfile, sizeof(c->rows[i].cfgfile), m->cfgfile);
    }
    for (i = 0; i < c->status.nrings && i < ECQ_MAX_ROWS; i++) {
        copy_str(c->ringrows[i].name, sizeof(c->ringrows[i].name), c->status.rings[i].name);
        c->ringrows[i].key = c->status.rings[i].key;
        c->ringrows[i].size = c->status.rings[i].size;
    }
    if (c->table.nmods > ECQ_MAX_ROWS)
        c->table.nmods = ECQ_MAX_ROWS;
    if (c->table.nrings > ECQ_MAX_ROWS)
        c->table.nrings = ECQ_MAX_ROWS;

    copy_str(c->table.hostname, sizeof(c->table.hostname), c->status.hostname);
    copy_str(c->table.start, sizeof(c->table.start), c->status.starttime);
    copy_str(c->table.current, sizeof(c->table.current), c->status.curtime);
    copy_str(c->table.disk, sizeof(c->table.disk), c->status.disk);
    copy_str(c->table.version, sizeof(c->table.version), c->status.version);
    c->table.seen_status = c->seen_status;
    c->table.noreply = c->noreply;
    c->table.seq = c->seq;
    c->table.mods = c->rows;
    c->table.rings = c->ringrows;

    /* La seleccion es por pid: si el pid desaparecio, se limpia. */
    if (c->selected_pid > 0 && ewgui_ctrl_idx_por_pid(&c->status, c->selected_pid) < 0)
        c->selected_pid = 0;

    c->target[0] = '\0';
    if (c->selected_pid > 0) {
        const int idx = ewgui_ctrl_idx_por_pid(&c->status, c->selected_pid);
        if (idx >= 0)
            snprintf(c->target, sizeof(c->target), "%s (%s)",
                     c->status.mods[idx].name,
                     c->noreply ? "?" : c->status.mods[idx].status);
    }
}

/* ------------------------------------------------------------------------- */
/* Ciclo de vida                                                             */
/* ------------------------------------------------------------------------- */

EcqCore *ecq_core_new(const EcqConfig *cfg)
{
    EcqCore *c;

    if (!cfg)
        return NULL;
    c = (EcqCore *)calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->cfg = *cfg;
    c->last_poll = 0;
    c->hb.last = 0;
    build_view(c);
    return c;
}

void ecq_core_free(EcqCore *core)
{
    if (!core)
        return;
    ecq_ring_detach(core);
    free(core);
}

int ecq_ring_attach(EcqCore *core)
{
    long key;
    unsigned char inst = 0, mod = 0;

    if (!core)
        return -1;
    if (core->ring_ok)
        return 0;

    /* kom ya esta cargado por ecq_config_load() (k_open del .d). */
    if (ewgui_ring_resolve(core->cfg.ring, core->cfg.my_module, &key, &inst, &mod) != 0)
        return -1;
    /* No llamar a tport_attach si el segmento no existe: aborta el proceso. */
    if (key < 0 || shmget(key, 0, 0) < 0)
        return -1;

    ewgui_ring_attach(&core->ring, key, inst, mod);
    core->ring_ok = 1;
    ecq_logf("anillo %s conectado (key %ld, inst %u, mod %u)\n",
             core->cfg.ring, key, (unsigned)inst, (unsigned)mod);

    get_type_or0("TYPE_HEARTBEAT", &core->type_hb);
    get_type_or0("TYPE_REQSTATUS", &core->type_req);
    get_type_or0("TYPE_STATUS", &core->type_status);
    get_type_or0("TYPE_STOP", &core->type_stop);
    get_type_or0("TYPE_RESTART", &core->type_restart);
    get_type_or0("TYPE_RECONFIG", &core->type_reconfig);
    (void)GetInst("INST_WILDCARD", &core->wc_inst);
    core->got_inst = 1;
    (void)GetModId("MOD_WILDCARD", &core->wc_mod);
    core->got_mod = 1;

    return 0;
}

void ecq_ring_detach(EcqCore *core)
{
    if (!core)
        return;
    ewgui_ring_detach(&core->ring);
    core->ring_ok = 0;
}

int ecq_ring_attached(const EcqCore *core)
{
    return core ? core->ring_ok : 0;
}

/* ------------------------------------------------------------------------- */
/* Tráfico del anillo                                                        */
/* ------------------------------------------------------------------------- */

static int send_msg(EcqCore *c, unsigned char type, const char *payload)
{
    if (!c || !c->ring_ok || !type)
        return -1;
    return ewgui_ctrl_send(&c->ring.region, c->ring.instid, c->ring.modid,
                           type, payload ? payload : "");
}

int ecq_request_status(EcqCore *core)
{
    return send_msg(core, core ? core->type_req : 0, "");
}

int ecq_tick(EcqCore *core, time_t now)
{
    int asked = 0;

    if (!core || !core->ring_ok)
        return 0;

    if (ewgui_heartbeat_due(&core->hb, (double)now, core->cfg.heart_beat_int))
        (void)send_msg(core, core->type_hb, "");

    if (core->last_poll == 0 || now - core->last_poll >= core->cfg.poll_int) {
        core->last_poll = now;
        (void)ecq_request_status(core);
        asked = 1;
    }

    (void)ecq_noreply(core, now);
    return asked;
}

int ecq_noreply(EcqCore *core, time_t now)
{
    int on = 0;

    if (!core)
        return 0;
    if (core->seen_status && core->last_status > 0 &&
        now - core->last_status > (time_t)(core->cfg.poll_int * 4))
        on = 1;

    if (on != core->noreply) {
        core->noreply = on;
        if (on)
            ecq_logf("startstop no responde: %ld s sin TYPE_STATUS (PollInt %d)\n",
                     (long)(now - core->last_status), core->cfg.poll_int);
        else
            ecq_logf("startstop vuelve a responder\n");
        build_view(core);   /* la columna Status pasa a "?" y vuelve */
    }
    return on;
}

int ecq_read(EcqCore *core, long timeout_ms)
{
    MSG_LOGO filter[1];
    MSG_LOGO logo;
    char msg[ECQ_STATUS_MAX + 1];
    long recsize;
    int res, applied = 0;

    (void)timeout_ms;   /* tport_getmsg no bloquea; el timeout lo pone el timer */
    if (!core || !core->ring_ok || !core->type_status)
        return 0;

    filter[0].instid = core->got_inst ? core->wc_inst : 0;
    filter[0].mod = core->got_mod ? core->wc_mod : 0;
    filter[0].type = core->type_status;

    do {
        res = ewgui_ring_drain(&core->ring, filter, 1, &logo, &recsize,
                               msg, (long)sizeof(msg) - 1);
        /* GET_OK/GET_MISS/GET_NOTRACK traen el mensaje YA copiado
         * (transport.c:844-865). GET_NONE y GET_TOOBIG no. */
        if ((res == GET_OK || res == GET_MISS || res == GET_NOTRACK) &&
            recsize > 0 && recsize < (long)sizeof(msg)) {
            msg[recsize] = '\0';
            if (ecq_apply_status(core, msg) == 0)
                applied++;
        }
    } while (res == GET_OK || res == GET_MISS || res == GET_NOTRACK);

    return applied;
}

int ecq_apply_status(EcqCore *core, const char *buf)
{
    char tmp[ECQ_STATUS_MAX + 1];

    if (!core || !buf)
        return -1;
    /* ewgui_ctrl_parse_status() muta el buffer (strtok_r). */
    snprintf(tmp, sizeof(tmp), "%s", buf);
    ewgui_ctrl_parse_status(tmp, &core->status);
    core->seen_status = 1;
    core->last_status = time(NULL);
    core->noreply = 0;
    core->seq++;
    build_view(core);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Vista y acciones                                                          */
/* ------------------------------------------------------------------------- */

const EcqTable *ecq_table(const EcqCore *core)
{
    return core ? &core->table : NULL;
}

void ecq_module_label(const EcqCore *core, int idx, char *out, size_t outlen)
{
    if (!out || outlen == 0)
        return;
    out[0] = '\0';
    if (!core || idx < 0 || idx >= core->status.nmods)
        return;
    ewgui_ctrl_etiqueta_modulo(&core->status, idx, out, outlen);
}

int ecq_select_pid(EcqCore *core, int pid)
{
    if (!core)
        return -1;
    if (pid > 0 && ewgui_ctrl_idx_por_pid(&core->status, pid) >= 0) {
        core->selected_pid = pid;
        build_view(core);
        return 0;
    }
    core->selected_pid = 0;
    build_view(core);
    return -1;
}

int ecq_selected_pid(const EcqCore *core)
{
    return core ? core->selected_pid : 0;
}

const char *ecq_target_label(const EcqCore *core)
{
    return core ? core->target : "";
}

int ecq_can(const EcqCore *core, EcqAction action, int pid)
{
    int idx;
    int sel_ok;
    int is_startstop;
    const char *st;

    if (!core)
        return 0;

    if (action == ECQ_ACT_RECONFIG)
        return core->seen_status && !core->noreply;

    idx = ewgui_ctrl_idx_por_pid(&core->status, pid);
    sel_ok = (pid > 0 && idx >= 0);
    if (!sel_ok)
        return 0;
    /* Sin respuesta de startstop no se afirma sensibilidad (on_poll del GTK
     * apaga los botones a mano; aqui se refleja en la tabla). */
    if (core->noreply)
        return 0;
    is_startstop = strcmp(core->status.mods[idx].name, "startstop") == 0;
    if (is_startstop)
        return 0;
    st = core->status.mods[idx].status;

    switch (action) {
    case ECQ_ACT_STOP:
        return strcmp(st, "Alive") == 0;
    case ECQ_ACT_RESTART:
        return 1;
    case ECQ_ACT_START:
        return strcmp(st, "Stop") == 0 || strcmp(st, "Dead") == 0;
    default:
        return 0;
    }
}

const char *ecq_action_text(EcqAction action)
{
    switch (action) {
    case ECQ_ACT_STOP:     return "Stop sent...";
    case ECQ_ACT_RESTART:  return "Restart sent...";
    case ECQ_ACT_START:    return "Start sent...";
    case ECQ_ACT_RECONFIG: return "Reconfig sent...";
    }
    return "";
}

const char *ecq_action_message(EcqAction action, int pid, char *payload, size_t paylen)
{
    if (payload && paylen)
        payload[0] = '\0';

    switch (action) {
    case ECQ_ACT_STOP:
        snprintf(payload, paylen, "%d", pid);
        return "TYPE_STOP";
    case ECQ_ACT_RESTART:
    case ECQ_ACT_START:
        /* Start = restart: ew_controller_gui.c:248-254 no inventa un TYPE_START. */
        snprintf(payload, paylen, "%d", pid);
        return "TYPE_RESTART";
    case ECQ_ACT_RECONFIG:
        return "TYPE_RECONFIG";
    }
    return NULL;
}

int ecq_action(EcqCore *core, EcqAction action, int pid, char *detail, size_t detlen)
{
    char buf[32];
    const char *name;
    unsigned char type = 0;

    if (detail && detlen)
        detail[0] = '\0';
    if (!core || !core->ring_ok)
        return -1;

    name = ecq_action_message(action, pid, buf, sizeof(buf));
    if (!name)
        return -1;
    if (strcmp(name, "TYPE_STOP") == 0)
        type = core->type_stop;
    else if (strcmp(name, "TYPE_RESTART") == 0)
        type = core->type_restart;
    else if (strcmp(name, "TYPE_RECONFIG") == 0)
        type = core->type_reconfig;

    if (send_msg(core, type, buf) != 0) {
        ecq_logf("ERROR enviando %s (pid %d) al anillo %s\n",
                 name, pid, core->cfg.ring);
        return -1;
    }

    if (detail && detlen)
        copy_str(detail, detlen, ecq_action_text(action));
    ecq_logf("accion %s enviada (pid %d)\n", name, pid);
    (void)ecq_request_status(core);
    return 0;
}
