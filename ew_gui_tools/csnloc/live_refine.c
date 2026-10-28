#include "live_refine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnloc.h"
#include "earthworm.h"

#include "../csnhypodbp/csnhypodbp_group.h"

#define LIVE_ARC_MAX 65536

typedef struct {
    int                active;
    int                done;
    int                qid;
    double             otime;
    double             last_run;
    unsigned           version;
    int                no_new;
    int                seed_prio;      /* 0=asociador, 1=refinada, 2=confirmada */
    int                last_npicks;
    uint64_t           last_hash;
    unsigned long      last_pick_seq;
    int                reopens;
    CsnhypodbpSolution seed;
} LiveEvent;

/* Tope de reaperturas por evento: N_retry corta el gasto, pero un enjambre no
 * debe reactivar el motor sin fin. Una confirmada (prio 2) siempre reabre. */
#define LIVE_MAX_REOPEN 3

struct LiveRefine {
    LiveRefineOps ops;
    PickBuffer    picks;
    LiveEvent    *events;
    int           max_events;
    unsigned long pick_seq;
};

static LiveEvent *live_find(LiveRefine *r, int qid)
{
    int i;

    for (i = 0; i < r->max_events; i++)
        if (r->events[i].active && r->events[i].qid == qid)
            return &r->events[i];
    return NULL;
}

static LiveEvent *live_new(LiveRefine *r, int qid)
{
    int i;

    (void)qid;
    for (i = 0; i < r->max_events; i++)
        if (!r->events[i].active) return &r->events[i];
    for (i = 0; i < r->max_events; i++)
        if (r->events[i].done) return &r->events[i];
    /* Reutiliza el primero si se lleno (mismo criterio que hyp2000_ring). */
    return &r->events[0];
}

static const char *tag(LiveRefine *r)
{
    return (r && r->ops.tag && r->ops.tag[0]) ? r->ops.tag : "live";
}

LiveRefine *live_refine_new(const LiveRefineOps *ops)
{
    LiveRefine *r;
    int n;

    if (!ops || !ops->locate || !ops->rewrite || !ops->publish ||
        !ops->stations || !ops->tt)
        return NULL;
    r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->ops = *ops;
    n = (ops->max_events > 0) ? ops->max_events : 8;
    r->events = calloc((size_t)n, sizeof(*r->events));
    if (!r->events) {
        free(r);
        return NULL;
    }
    r->max_events = n;
    PickBuffer_Init(&r->picks, CSLOC_MAX_PICKS, 10.0);
    return r;
}

void live_refine_free(LiveRefine *r)
{
    if (!r) return;
    PickBuffer_Free(&r->picks);
    free(r->events);
    free(r);
}

void live_refine_seed(LiveRefine *r, const char *arc, int len, int mod)
{
    CsnhypodbpSolution sol;
    LiveEvent *ev;

    if (!r || !arc) return;
    if (csnhypodbp_parse_solution(arc, (size_t)len, mod, &sol) != 0) return;
    ev = live_find(r, sol.qid);

    /* Una confirmada (prio 2) no se pisa nunca. Un seed del asociador solo
       actualiza si trae MAS fases que el vigente: una correccion v2 del mismo
       qid si entra, y un seed repetido no reinicia el trabajo. */
    if (ev && (ev->seed_prio >= 2 || sol.nph <= ev->seed.nph)) {
        if (r->ops.debug >= 1)
            logit("t", "%s: semilla ev=%d ignorada (prio=%d nph=%d vs %d)\n",
                  tag(r), sol.qid, ev->seed_prio, sol.nph, ev->seed.nph);
        return;
    }
    if (!ev) {
        ev = live_new(r, sol.qid);
        memset(ev, 0, sizeof(*ev));
        ev->active = 1;
        ev->qid = sol.qid;
        ev->version = 0;
        ev->seed_prio = 0;
    }
    ev->seed = sol;
    ev->otime = sol.otime;
    ev->done = 0;
    ev->no_new = 0;
    ev->last_run = 0;          /* fuerza la primera corrida */
    ev->last_npicks = 0;
    ev->last_hash = 0;
    ev->last_pick_seq = r->pick_seq;
    if (r->ops.debug >= 1)
        logit("t", "%s: semilla ev=%d nph=%d\n", tag(r), sol.qid, sol.nph);
}

void live_refine_prefer(LiveRefine *r, const char *arc, int len, int mod)
{
    CsnhypodbpSolution sol;
    LiveEvent *ev;

    if (!r || !arc) return;
    if (csnhypodbp_parse_solution(arc, (size_t)len, mod, &sol) != 0) return;
    ev = live_find(r, sol.qid);
    if (!ev) {
        ev = live_new(r, sol.qid);
        memset(ev, 0, sizeof(*ev));
        ev->active = 1;
        ev->qid = sol.qid;
    }
    ev->seed = sol;
    ev->otime = sol.otime;
    ev->done = 0;
    ev->no_new = 0;
    ev->last_run = 0;
    ev->last_npicks = 0;
    ev->last_hash = 0;
    ev->last_pick_seq = r->pick_seq;
    ev->reopens = 0;           /* confirmada: puede reabrir siempre */
    ev->seed_prio = 2;         /* confirmada: maxima prioridad */
    if (r->ops.debug >= 1)
        logit("t", "%s: preferida ev=%d nph=%d\n", tag(r), sol.qid, sol.nph);
}

int live_refine_add_pick(LiveRefine *r, const Pick *p)
{
    if (!r || !p || p->manual) return -1;
    PickBuffer_AddOrReplace(&r->picks, p);
    r->pick_seq++;             /* evidencia nueva: puede reabrir un done */
    return 0;
}

int live_refine_add_pick_msg(LiveRefine *r, const char *msg, int len)
{
    Pick p;

    if (!r || !msg) return -1;
    if (PickSCNL_Parse(msg, len, &p) != 0) return -1;
    return live_refine_add_pick(r, &p);
}

void live_refine_prune(LiveRefine *r, time_t now)
{
    double ttl;

    if (!r) return;
    ttl = (r->ops.assoc.lookback_sec > 0.0)
              ? r->ops.assoc.lookback_sec + 60.0 : 300.0;
    PickBuffer_Prune(&r->picks, (double)now, ttl);
}

static int live_build(LiveRefine *r, LiveEvent *ev, char *arc, size_t arcn,
                      LivePickSet *set_out, int *np_out)
{
    LiveSeed s;

    if (!r->ops.tt) return -1;
    s.qid     = ev->seed.qid;
    s.version = ev->seed.version;
    s.t0      = ev->seed.otime;
    s.lat     = ev->seed.lat;
    s.lon     = ev->seed.lon;
    s.depth   = ev->seed.depth;

    return live_build_arc_seeded(&s, ev->seed.picks, ev->seed.npicks,
                                 r->picks.items, r->picks.n, r->ops.tt,
                                 r->ops.stations, &r->ops.assoc, arc, arcn,
                                 set_out, np_out);
}

static int live_event_retry(LiveRefine *r, LiveEvent *ev)
{
    if (live_retry_exhausted(ev->no_new, r->ops.n_retry)) {
        ev->done = 1;
        return 1;
    }
    return 0;
}

int live_refine_tick(LiveRefine *r, time_t now)
{
    int i, published = 0;
    int max_iter;

    if (!r) return 0;

    for (i = 0; i < r->max_events; i++) {
        LiveEvent *ev = &r->events[i];
        char arc[LIVE_ARC_MAX], out[LIVE_ARC_MAX], pruned[LIVE_ARC_MAX];
        LivePickSet set;
        LiveLocCheck chk;
        CsnhypodbpSolution refined;
        int np = 0, out_len = 0, fresh, valid = 0, iter;
        const char *why = "sin localizacion";

        if (!ev->active) continue;

        /* N_retry corta el gasto de CPU, NO sentencia el evento: si llego
           evidencia nueva y el sismo es reciente, se reabre. */
        if (ev->done) {
            if (r->pick_seq != ev->last_pick_seq &&
                (r->ops.event_ttl_sec <= 0.0 ||
                 (double)now - ev->otime <= r->ops.event_ttl_sec) &&
                (ev->seed_prio >= 2 || ev->reopens < LIVE_MAX_REOPEN)) {
                ev->done = 0;
                ev->no_new = 0;
                ev->reopens++;
                ev->last_pick_seq = r->pick_seq;
                ev->last_run = 0;      /* no esperar RecalcSec para reintentar */
                logit("t", "%s: ev=%d reabierto (picadas nuevas, %d/%d)\n",
                      tag(r), ev->qid, ev->reopens, LIVE_MAX_REOPEN);
            } else {
                continue;
            }
        }

        if (ev->last_run != 0.0 &&
            (double)now - ev->last_run < (double)r->ops.recalc_sec)
            continue;

        memset(&set, 0, sizeof(set));
        memset(&chk, 0, sizeof(chk));
        if (live_build(r, ev, arc, sizeof(arc), &set, &np) != 0) {
            ev->last_run = (double)now;
            ev->last_pick_seq = r->pick_seq;
            ev->no_new++;
            if (live_event_retry(r, ev))
                logit("t", "%s: ev=%d sin picadas asociadas tras %d intentos; "
                           "se detiene\n", tag(r), ev->qid, ev->no_new);
            continue;
        }

        fresh = (ev->last_run == 0.0) || set.hash != ev->last_hash ||
                np != ev->last_npicks;
        if (!fresh) {
            ev->last_run = (double)now;
            ev->last_pick_seq = r->pick_seq;
            ev->no_new++;
            if (live_event_retry(r, ev))
                logit("t", "%s: ev=%d sin picadas nuevas tras %d intentos; "
                           "se detiene\n", tag(r), ev->qid, ev->no_new);
            continue;
        }

        /* Refinamiento ITERATIVO: si la salida no es valida, se poda el ARC de
           entrada con los residuales del motor y se reintenta. */
        max_iter = (r->ops.max_iter > 0) ? r->ops.max_iter : 1;
        for (iter = 0; iter < max_iter; iter++) {
            valid = 0;
            why = "sin localizacion";

            if (r->ops.locate(r->ops.ud, arc, (int)strlen(arc), out,
                              (int)sizeof(out), &out_len) != 0 ||
                out_len <= 0)
                break;

            out_len = r->ops.rewrite(r->ops.ud, out, (int)sizeof(out), out_len,
                                     arc, (int)strlen(arc));
            if (out_len <= 0) {
                why = "ARC no reescrito";
                break;
            }

            if (live_check_arc(out, out_len, &r->ops.check, &chk) == 0 &&
                chk.ok &&
                csnhypodbp_parse_solution(out, (size_t)out_len, 0,
                                          &refined) == 0) {
                if (!r->ops.in_region ||
                    r->ops.in_region(r->ops.ud, refined.lat, refined.lon)) {
                    valid = 1;
                    break;
                }
                why = "fuera de region";
            } else {
                why = chk.why[0] ? chk.why : "salida invalida";
            }

            if (iter + 1 >= max_iter) break;
            {
                int np2 = 0;

                if (live_prune_arc(out, out_len, arc, (int)strlen(arc),
                                   r->ops.assoc.res_p_sec,
                                   r->ops.assoc.res_s_sec,
                                   pruned, sizeof(pruned), &np2) != 0)
                    break;
                if (np2 >= np) break;
                if (np2 < r->ops.check.min_phases) break;
                snprintf(arc, sizeof(arc), "%s", pruned);
                np = np2;
                if (r->ops.debug >= 1)
                    logit("t", "%s: ev=%d poda it=%d -> %d fases\n",
                          tag(r), ev->qid, iter + 1, np);
            }
        }

        if (valid) {
            if (r->ops.publish(r->ops.ud, arc, (int)strlen(arc), out,
                               out_len) != 0) {
                logit("et", "%s: fallo publicando el ARC refinado ev=%d\n",
                      tag(r), ev->qid);
                ev->last_run = (double)now;
                ev->last_pick_seq = r->pick_seq;
                ev->no_new++;
                if (live_event_retry(r, ev))
                    logit("t", "%s: ev=%d sin publicacion valida tras %d "
                               "intentos; se detiene\n",
                          tag(r), ev->qid, ev->no_new);
                continue;
            }
            published++;
            ev->seed = refined;
            ev->otime = refined.otime;
            ev->version = refined.version;
            if (ev->seed_prio < 2) ev->seed_prio = 1;
            ev->last_run = (double)now;
            ev->last_npicks = set.n;
            ev->last_hash = set.hash;
            ev->last_pick_seq = r->pick_seq;
            ev->no_new = 0;
        } else {
            ev->last_run = (double)now;
            ev->last_pick_seq = r->pick_seq;
            ev->no_new++;
            logit("t", "%s: LOC invalida ev=%d asoc=%d %s "
                       "(nph=%d gap=%.0f rms=%.2f)\n",
                  tag(r), ev->qid, np, why, chk.nph, chk.gap, chk.rms);
            if (live_event_retry(r, ev))
                logit("t", "%s: ev=%d sin solucion valida tras %d intentos; "
                           "se detiene\n", tag(r), ev->qid, ev->no_new);
        }
    }
    return published;
}
