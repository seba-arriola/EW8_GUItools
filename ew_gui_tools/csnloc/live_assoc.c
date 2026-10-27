/******************************************************************************
 * live_assoc.c                                                               *
 *                                                                            *
 * Asociacion de picadas P/S al hipocentro de un evento vivo y validacion    *
 * de la cabecera del ARC de salida. Logica pura: sin EarthWorm ni GTK.      *
 ******************************************************************************/
#include "live_assoc.h"

#include "../csnhypodbp/csnhypodbp_group.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RAD
#define RAD 0.017453292519943
#endif

/* Misma conversion que grid.c/stations.c: el modelo tau y StationList usan
 * latitud geocentrica; el ARC (y el seed) traen latitud geografica. */
static double geo_to_geoc(double lat_deg)
{
    double l = lat_deg * RAD;
    return atan(0.993277 * tan(l)) / RAD;
}

int live_assoc_pick(const LiveSeed *s, const Pick *p, TTModel *tt,
                    const StationList *st, const LiveAssocCfg *cfg,
                    double *resid_out)
{
    int    sidx;
    double slat, delta, tpred, resid;

    if (!s || !p || !tt || !st || !cfg) return -1;
    if (p->manual) return 0;
    if (p->phase != CSLOC_PHASE_P && p->phase != CSLOC_PHASE_S) return 0;

    /* Una picada no puede ser anterior al origen mas alla de la tolerancia.
       lookback_sec acota solo el futuro (P/S de estaciones lejanas). */
    if (p->t_epoch < s->t0 - cfg->res_p_sec) return 0;
    if (cfg->lookback_sec > 0.0 && (p->t_epoch - s->t0) > cfg->lookback_sec)
        return 0;

    sidx = Stations_Find(st, p->sta);
    if (sidx < 0) return 0;

    slat  = geo_to_geoc(s->lat);
    delta = TT_GreatCircleDeg(slat, s->lon,
                              st->lat_geoc_sta[sidx], st->st[sidx].lon);
    if (TTModel_Predict(tt, delta, s->depth, p->phase, &tpred, NULL) != 0)
        return 0;

    resid = p->t_epoch - (s->t0 + tpred);
    if (resid_out) *resid_out = resid;

    if (p->phase == CSLOC_PHASE_P) {
        if (cfg->res_p_sec > 0.0 && fabs(resid) > cfg->res_p_sec) return 0;
    } else {
        if (cfg->res_s_sec > 0.0 && fabs(resid) > cfg->res_s_sec) return 0;
    }
    return 1;
}

/* FNV-1a de 64 bits; el hash identifica el conjunto de picadas asociadas
 * (estacion + fase + tiempo) para detectar "sin novedad" sin ambiguedad. */
static uint64_t fnv1a(uint64_t h, const void *buf, size_t n)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Agrega una fase al ARC deduplicando por (estacion, fase): la ultima gana.
   Devuelve -1 si no cabe. */
static int arc_add_pick(CsnhypodbpSolution *sol, int *n,
                        const char *sta, const char *net, const char *chan,
                        const char *loc, char phase, double t_epoch)
{
    int k, found = -1;

    /* Una fase con estacion o canal en blanco es el TERMINADOR para el motor
       (hyphs.for:430): cortaria la lectura del resto y el evento se perderia.
       No entra nunca, venga del seed o del buffer. */
    if (!sta || !sta[0] || !chan || !chan[0]) return -1;

    for (k = 0; k < *n; k++) {
        if (strcmp(sol->picks[k].sta, sta) == 0 &&
            sol->picks[k].phase == phase) {
            found = k;
            break;
        }
    }
    if (found < 0) {
        if (*n >= CSNHYPODBP_MAX_PICKS) return -1;
        found = (*n)++;
    }
    memset(&sol->picks[found], 0, sizeof(sol->picks[found]));
    snprintf(sol->picks[found].sta, sizeof(sol->picks[found].sta), "%s", sta);
    snprintf(sol->picks[found].net, sizeof(sol->picks[found].net), "%s", net);
    snprintf(sol->picks[found].chan, sizeof(sol->picks[found].chan), "%s", chan);
    snprintf(sol->picks[found].loc, sizeof(sol->picks[found].loc), "%s", loc);
    sol->picks[found].phase = phase;
    sol->picks[found].t_pick = t_epoch;
    sol->picks[found].manual = 0;
    sol->picks[found].residual = NAN;   /* el residual lo produce el motor */
    return 0;
}

static int build_arc_impl(const LiveSeed *s,
                          const CsnhypodbpPick *seed_picks, int nseed,
                          const Pick *picks, int np,
                          TTModel *tt, const StationList *st,
                          const LiveAssocCfg *cfg, char *arc, size_t arcn,
                          LivePickSet *set_out, int *nph_out)
{
    CsnhypodbpSolution sol;
    uint64_t h = 1469598103934665603ULL;
    int i, n = 0;

    if (!s || !tt || !st || !cfg || !arc || arcn < 512) return -1;

    memset(&sol, 0, sizeof(sol));
    sol.qid     = s->qid;
    sol.otime   = s->t0;
    sol.lat     = s->lat;
    sol.lon     = s->lon;
    sol.depth   = s->depth;
    sol.version = s->version + 1;
    sol.manual  = 0;
    sol.confirmed = 0;
    sol.pref_mag = 0;

    /* 1) Fases del seed: csnloc ya las asocio contra este hipocentro, asi que
       son el nucleo coherente del ARC. NO se filtran por LiveStations: una
       estacion fuera de la lista de metadata no rompe al motor (la salta con
       "SKIP PHASE CARD"), pero descartarla aqui puede dejar el ARC por debajo
       del minimo para localizar. Medido: un seed de 6 fases quedaba en 3. */
    for (i = 0; i < nseed; i++) {
        const CsnhypodbpPick *sp = &seed_picks[i];

        if (sp->phase != 'P' && sp->phase != 'S') continue;
        (void)arc_add_pick(&sol, &n, sp->sta, sp->net, sp->chan, sp->loc,
                           sp->phase, sp->t_pick);
    }

    /* 2) Picadas del buffer asociadas por tiempo de viaje al seed. */
    for (i = 0; i < np; i++) {
        const Pick *p = &picks[i];
        double resid = NAN;
        char   phase;

        if (live_assoc_pick(s, p, tt, st, cfg, &resid) != 1) continue;
        phase = p->phase_name[0] ? p->phase_name[0] : 'P';
        (void)arc_add_pick(&sol, &n, p->sta, p->net, p->chan, p->loc,
                           phase, p->t_epoch);
    }

    if (n <= 0) return -1;

    /* Hash del conjunto EFECTIVO (despues del dedup por sta+fase), no de cada
       pick visitado: dos canales de la misma estacion/fase no deben simular
       una picada nueva. XOR lo hace independiente del orden de la ventana. */
    for (i = 0; i < n; i++) {
        long long ms = (long long)llround(sol.picks[i].t_pick * 1000.0);
        uint64_t ph = 1469598103934665603ULL;

        ph = fnv1a(ph, sol.picks[i].sta, strlen(sol.picks[i].sta));
        ph = fnv1a(ph, &sol.picks[i].phase, sizeof(sol.picks[i].phase));
        ph = fnv1a(ph, &ms, sizeof(ms));
        h ^= ph;
    }
    if (h == 0) h = 1;   /* 0 es el centinela de "sin hash" en LiveEvent */

    sol.npicks = n;
    sol.nph    = n;
    sol.rms    = 0.0;
    sol.gap    = 0;

    if (csnhypodbp_arc_build(&sol, 0, arc, arcn) != 0) return -1;

    if (set_out) {
        set_out->hash = h;
        set_out->n    = n;
    }
    if (nph_out) *nph_out = n;
    return 0;
}

int live_build_arc(const LiveSeed *s, const Pick *picks, int np,
                   TTModel *tt, const StationList *st,
                   const LiveAssocCfg *cfg, char *arc, size_t arcn,
                   LivePickSet *set_out, int *nph_out)
{
    return build_arc_impl(s, NULL, 0, picks, np, tt, st, cfg, arc, arcn,
                          set_out, nph_out);
}

int live_build_arc_seeded(const LiveSeed *s,
                          const CsnhypodbpPick *seed_picks, int nseed,
                          const Pick *picks, int np,
                          TTModel *tt, const StationList *st,
                          const LiveAssocCfg *cfg, char *arc, size_t arcn,
                          LivePickSet *set_out, int *nph_out)
{
    if (!seed_picks || nseed <= 0)
        return build_arc_impl(s, NULL, 0, picks, np, tt, st, cfg, arc, arcn,
                              set_out, nph_out);
    return build_arc_impl(s, seed_picks, nseed, picks, np, tt, st, cfg,
                          arc, arcn, set_out, nph_out);
}

int live_prune_arc(const char *arc_out, int len_out,
                   const char *arc_in, int len_in,
                   double tol_p, double tol_s,
                   char *arc_pruned, size_t arcn, int *nph_out)
{
    CsnhypodbpSolution so, si;
    int i, j, n = 0;

    if (!arc_out || !arc_in || !arc_pruned) return -1;
    if (csnhypodbp_parse_solution(arc_out, (size_t)len_out, 163, &so) != 0)
        return -1;
    if (csnhypodbp_parse_solution(arc_in, (size_t)len_in, 163, &si) != 0)
        return -1;

    for (i = 0; i < si.npicks; i++) {
        double tol = (si.picks[i].phase == 'S') ? tol_s : tol_p;
        int    keep = 0;

        for (j = 0; j < so.npicks; j++) {
            if (strcmp(si.picks[i].sta, so.picks[j].sta) != 0) continue;
            if (si.picks[i].phase != so.picks[j].phase) continue;
            /* Estacion presente en la salida: sobrevive solo si el motor le
               asigno un residual dentro de la tolerancia. */
            if (!isnan(so.picks[j].residual) &&
                fabs(so.picks[j].residual) <= tol)
                keep = 1;
            break;
        }
        if (!keep) continue;
        si.picks[n++] = si.picks[i];
    }

    if (n <= 0) return -1;

    si.npicks = n;
    si.nph    = n;
    si.rms    = 0.0;
    si.gap    = 0;
    if (csnhypodbp_arc_build(&si, 0, arc_pruned, arcn) != 0) return -1;
    if (nph_out) *nph_out = n;
    return 0;
}
