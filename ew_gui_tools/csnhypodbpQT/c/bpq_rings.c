/*
 * bpq_rings.c - Conexion a los anillos de EarthWorm y publicacion de mensajes.
 *
 * Port de ConnectToEarthworm()/drain_ring()/drain_pick_ring()/
 * ew_background_tasks() de csnhypodbp_main.c:761-859, y de los emisores
 * csnhypodbp_ws.c:1679-1783, sin GTK y sin globales.
 *
 * Contrato de drenaje (AGENTS §7):
 *   - GET_NONE  -> no habia nada: fin del bucle.
 *   - GET_OK / GET_MISS / GET_NOTRACK -> el mensaje YA esta copiado en el buffer
 *     del llamante: hay que parsearlo y SEGUIR drenando (si no, un anillo
 *     desincronizado se vacia a un mensaje por tick).
 *   - GET_TOOBIG -> no hubo copia (`*outlen` = tamano real): NUNCA escribir el NUL.
 */
#include "bpq_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <earthworm.h>
#include <transport.h>

#include "ewgui/ring.h"

struct BpqRings {
    EwGuiRing in;        /* InRing      (lectura + publicacion de magnitudes) */
    EwGuiRing in_raw;    /* InRingRaw   (lectura, opcional) */
    EwGuiRing in_pick;   /* InRingPick  (lectura de picadas) */
    EwGuiRing out;       /* OutRing     (picadas manuales) */
    EwGuiRing conf;      /* ConfRing    (ARC confirmado, opcional) */
    int have_raw, have_pick, have_conf;

    unsigned char type_arc, type_pick, type_mag, type_hb, type_err;
    unsigned char instid, modid;

    MSG_LOGO logos[2];       /* ARC + MAGNITUDE (filtro de lectura) */
    MSG_LOGO pick_logo[1];   /* PICK_SCNL */

    BpqConfig cfg;           /* copia: el anillo no depende de la vida del llamador */
    EwGuiHeartbeat hb;
    int  pid;
};

/* ------------------------------------------------------------------------- */
/* Apertura                                                                  */
/* ------------------------------------------------------------------------- */

static int need_ring(const char *name, long key, const char *what,
                     char *err, size_t errn)
{
    if (key != -1) return 0;
    if (err) snprintf(err, errn, "el anillo %s de %s no existe (revisa %s y earthworm.d)",
                      name, what, "el .d");
    return -1;
}

BpqRings *bpq_rings_open(const BpqConfig *cfg, char *err, size_t errn)
{
    BpqRings *r;
    long key_in, key_out, key_raw = -1, key_pick = -1, key_conf = -1;
    unsigned char inst, mod, wild_i, wild_m;
    const char *pick_name;

    if (err && errn) err[0] = '\0';
    if (!cfg) {
        if (err) snprintf(err, errn, "config nula");
        return NULL;
    }

    /* 1) RESOLVER TODO ANTES DE ADJUNTAR NADA: un .d malo no debe crear/adjuntar
     *    memoria compartida a medias (ni dejar el proceso a medio conectar). */
    key_in  = GetKey((char *)cfg->in_ring);
    key_out = GetKey((char *)cfg->out_ring);
    if (need_ring(cfg->in_ring, key_in, "InRing", err, errn) != 0) return NULL;
    if (need_ring(cfg->out_ring, key_out, "OutRing", err, errn) != 0) return NULL;

    if (cfg->in_ring_raw[0]) {
        key_raw = GetKey((char *)cfg->in_ring_raw);
        if (key_raw == -1) key_raw = -1;   /* opcional: se ignora con aviso */
    }
    pick_name = cfg->in_ring_pick[0] ? cfg->in_ring_pick : cfg->out_ring;
    key_pick = GetKey((char *)pick_name);
    if (cfg->conf_ring[0]) key_conf = GetKey((char *)cfg->conf_ring);

    if (GetLocalInst(&inst) != 0) {
        if (err) snprintf(err, errn, "GetLocalInst fallo (revisa earthworm.d/INST_*)");
        return NULL;
    }
    if (GetModId((char *)cfg->my_mod_name, &mod) != 0) {
        if (err) snprintf(err, errn,
                          "GetModId %s fallo: falta 'Module %s' en earthworm.d",
                          cfg->my_mod_name, cfg->my_mod_name);
        return NULL;
    }

    r = (BpqRings *)calloc(1, sizeof(*r));
    if (!r) {
        if (err) snprintf(err, errn, "sin memoria para BpqRings");
        return NULL;
    }
    r->cfg = *cfg;
    r->instid = inst;
    r->modid  = mod;
    r->pid    = (int)getpid();

    /* 2) Tipos de mensaje (fatal, como csnhypodbp_main.c:765-767). */
    if (GetType("TYPE_HYP2000ARC", &r->type_arc) != 0 ||
        GetType("TYPE_PICK_SCNL", &r->type_pick) != 0 ||
        GetType("TYPE_HEARTBEAT", &r->type_hb) != 0 ||
        GetType("TYPE_ERROR", &r->type_err) != 0 ||
        GetType("TYPE_MAGNITUDE", &r->type_mag) != 0) {
        if (err) snprintf(err, errn, "GetType fallo (revisa earthworm.d: TYPE_*)");
        free(r);
        return NULL;
    }

    /* 3) Logos de lectura con comodines (csnhypodbp_main.c:769-772). */
    wild_i = 0; wild_m = 0;
    (void)GetInst("INST_WILDCARD", &wild_i);
    (void)GetModId("MOD_WILDCARD", &wild_m);
    r->logos[0].instid = wild_i; r->logos[0].mod = wild_m; r->logos[0].type = r->type_arc;
    r->logos[1].instid = wild_i; r->logos[1].mod = wild_m; r->logos[1].type = r->type_mag;
    r->pick_logo[0].instid = wild_i;
    r->pick_logo[0].mod    = wild_m;
    r->pick_logo[0].type   = r->type_pick;

    /* 4) Adjuntar. */
    ewgui_ring_attach(&r->in,  key_in,  inst, mod);
    ewgui_ring_attach(&r->out, key_out, inst, mod);

    if (key_raw != -1) {
        ewgui_ring_attach(&r->in_raw, key_raw, inst, mod);
        r->have_raw = 1;
    }
    if (key_pick != -1) {
        ewgui_ring_attach(&r->in_pick, key_pick, inst, mod);
        r->have_pick = 1;
    }
    if (key_conf != -1) {
        ewgui_ring_attach(&r->conf, key_conf, inst, mod);
        r->have_conf = 1;
    }
    return r;
}

void bpq_rings_close(BpqRings *r)
{
    if (!r) return;
    if (r->in.attached)       ewgui_ring_detach(&r->in);
    if (r->out.attached)      ewgui_ring_detach(&r->out);
    if (r->have_raw)          ewgui_ring_detach(&r->in_raw);
    if (r->have_pick)         ewgui_ring_detach(&r->in_pick);
    if (r->have_conf)         ewgui_ring_detach(&r->conf);
    free(r);
}

/* ------------------------------------------------------------------------- */
/* Drenaje                                                                   */
/* ------------------------------------------------------------------------- */

static int drain_one(BpqRings *r, EwGuiRing *ring, const char *ring_name,
                     BpqArcFn arc, BpqMagFn mag, BpqPickFn pick, void *ud,
                     char *err, size_t errn)
{
    char     msg[65000];
    MSG_LOGO logo;
    long     size = 0;
    int      res, n = 0;
    short    nlogo = (ring == &r->in_pick) ? 1 : 2;
    MSG_LOGO *logos = (ring == &r->in_pick) ? r->pick_logo : r->logos;

    for (;;) {
        res = ewgui_ring_drain(ring, logos, nlogo, &logo, &size, msg, sizeof(msg) - 1);
        if (res == GET_NONE) break;

        if (res == GET_TOOBIG) {
            /* El mensaje NO se copio: `size` es el tamano real. Descartarlo. */
            if (err) snprintf(err, errn,
                              "mensaje de %s demasiado grande (%ld > %zu): descartado",
                              ring_name, size, sizeof(msg) - 1);
            continue;
        }
        if (res != GET_OK && res != GET_MISS && res != GET_NOTRACK) continue;
        if (size < 0 || (size_t)size >= sizeof(msg)) continue;

        msg[size] = '\0';
        n++;

        if (logo.type == r->type_arc && arc) {
            CsnhypodbpSolution sol;
            /* El `mod` del logo identifica al productor (GetModId de su .d). */
            if (csnhypodbp_parse_solution(msg, (size_t)size, (int)logo.mod, &sol) == 0)
                arc(&sol, msg, (size_t)size, ud);
        } else if (logo.type == r->type_mag && mag) {
            BpqMagMsg m;
            if (bpq_magmsg_parse(msg, (size_t)size, &m) == 0)
                mag(&m, ud);
        } else if (logo.type == r->type_pick && pick) {
            CsnhypodbpTracePick pk;
            if (csnhypodbp_pick_parse(msg, &pk))
                pick(&pk, ud);
        }
    }
    return n;
}

int bpq_rings_drain(BpqRings *r, BpqArcFn arc, BpqMagFn mag, BpqPickFn pick,
                    void *ud, char *err, size_t errn)
{
    int n = 0;
    if (!r) {
        if (err) snprintf(err, errn, "BpqRings nulo");
        return -1;
    }
    if (err && errn) err[0] = '\0';

    n += drain_one(r, &r->in, r->cfg.in_ring, arc, mag, pick, ud, err, errn);
    if (r->have_raw)
        n += drain_one(r, &r->in_raw, r->cfg.in_ring_raw, arc, mag, pick, ud, err, errn);
    if (r->have_pick)
        n += drain_one(r, &r->in_pick, r->cfg.in_ring_pick, arc, mag, pick, ud, err, errn);
    return n;
}

int bpq_rings_heartbeat(BpqRings *r, double now_epoch)
{
    if (!r) return 0;
    return ewgui_heartbeat_due(&r->hb, now_epoch, r->cfg.heartbeat_s);
}

int bpq_rings_should_quit(BpqRings *r)
{
    if (!r) return 0;
    return ewgui_ring_should_quit(&r->in.region, r->pid);
}

/* ------------------------------------------------------------------------- */
/* Publicacion                                                               */
/* ------------------------------------------------------------------------- */

int bpq_emit_pick(BpqRings *r, const char *sta, const char *chan,
                  const char *net, const char *loc, char phase, double t,
                  char pref)
{
    /* Paridad con csnhypodbp_ws.c:1709: un unico contador de secuencia. */
    static unsigned char seq = 0;
    char     out[256];
    MSG_LOGO logo;

    if (!r) return -1;
    if (bpq_pick_msg_format(out, sizeof(out), r->type_pick, r->modid, r->instid,
                            seq++, sta, chan, net, loc, phase, t, pref) != 0)
        return -1;

    logo.instid = r->instid;
    logo.mod    = r->modid;
    logo.type   = r->type_pick;
    return (ewgui_ring_put(&r->out, &logo, (long)strlen(out), out) == PUT_OK) ? 0 : -1;
}

int bpq_emit_mag(BpqRings *r, const BpqMagMsg *m, const char *text)
{
    char     out[1024];
    MSG_LOGO logo;

    if (!r || !m) return -1;
    if (bpq_mag_msg_format(out, sizeof(out), m, text) != 0) return -1;

    logo.instid = r->instid;
    logo.mod    = r->modid;
    logo.type   = r->type_mag;
    /* Las magnitudes propias van al anillo de ENTRADA (paridad ws.c:1701). */
    return (ewgui_ring_put(&r->in, &logo, (long)strlen(out), out) == PUT_OK) ? 0 : -1;
}

int bpq_emit_confirm(BpqRings *r, const CsnhypodbpSolution *sol,
                     char *arc, size_t arcn)
{
    MSG_LOGO logo;

    if (!r || !sol || !arc || arcn == 0) return -1;
    /* Exige qid>0 y version 1..9999 (csnhypodbp_confirmation_arc). */
    if (csnhypodbp_confirmation_arc(sol, arc, arcn) != 0) return -1;
    /* Sin mod no hay origin_id valido (<event>:<mod>:<version>): no publicar
     * una solucion que hypodb no podra identificar. */
    if (sol->mod <= 0) return -4;
    if (!r->have_conf) return -2;

    /* El `mod` del logo es el de LA SOLUCION (no el de la GUI): asi el
     * origin_id <event>:<mod>:<version> que arma hypodb no depende de que GUI
     * confirmo (csnhypodbp_ws.c:1771-1773). */
    logo.instid = r->instid;
    logo.mod    = (unsigned char)sol->mod;
    logo.type   = r->type_arc;
    return (ewgui_ring_put(&r->conf, &logo, (long)strlen(arc), arc) == PUT_OK) ? 0 : -3;
}
