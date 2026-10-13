/******************************************************************************
 * csnhypodbp_statable.c                                                       *
 *                                                                             *
 * Tabla de estaciones: una P y una S por estacion (manual > automatica >      *
 * teorica) y residual de la solucion. Ver csnhypodbp_statable.h.              *
 ******************************************************************************/
#include "csnhypodbp_statable.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define STAPICK_MAX 64

/* Ranking del origen: manual > automatica > teorica. -1 = desconocido. */
static int rank_of(char origin)
{
    if (origin == 'M' || origin == 'm') return 2;
    if (origin == 'A' || origin == 'a') return 1;
    if (origin == 'T' || origin == 't') return 0;
    return -1;
}

int csnhypodbp_stapick_one(const CsnhypodbpStaPick *cand, int n, double t_ref,
                           CsnhypodbpStaPick *dst)
{
    int best = -1, i;

    if (!cand || n <= 0) return 0;
    for (i = 0; i < n; i++) {
        if (!cand[i].present) continue;
        if (best < 0) { best = i; continue; }
        {
            int rb = rank_of(cand[best].origin), ri = rank_of(cand[i].origin);
            if (ri > rb) { best = i; continue; }
            if (ri < rb) continue;
            if (t_ref > 0.0) {
                /* Empate: la mas cercana a la referencia (p. ej. la teorica). */
                double db = fabs(cand[best].t - t_ref);
                double di = fabs(cand[i].t - t_ref);
                if (di < db) best = i;
            } else if (cand[i].t > cand[best].t) {
                best = i;   /* sin referencia: la mas reciente */
            }
        }
    }
    if (best < 0) return 0;
    if (dst) {
        dst->present = 1;
        dst->origin = cand[best].origin;
        dst->t = cand[best].t;
        snprintf(dst->chan, sizeof(dst->chan), "%s", cand[best].chan);
        snprintf(dst->net,  sizeof(dst->net),  "%s", cand[best].net);
        snprintf(dst->loc,  sizeof(dst->loc),  "%s", cand[best].loc);
    }
    return 1;
}

/* Copia el SCNL de una picada candidata completando con el de la ESTACION
 * (metadata de StaFile) cuando la picada no lo trae. Sin canal, HYPOINVERSE
 * rechaza la fase (empareja STA+NET+COMP3+LOC) y el OBS de NLLoc sale con 13
 * campos en vez de 14. */
static void set_scl(CsnhypodbpStaPick *c, const char *chan, const char *net,
                    const char *loc, const CsnhypodbpStaInput *st)
{
    snprintf(c->chan, sizeof(c->chan), "%s",
             (chan && chan[0]) ? chan : st->chan);
    snprintf(c->net, sizeof(c->net), "%s",
             (net && net[0]) ? net : st->net);
    snprintf(c->loc, sizeof(c->loc), "%s",
             (loc && loc[0]) ? loc : st->loc);
}

/* Residual de la solucion para (estacion, fase): el primer valor no NAN. */
static double resid_for(const CsnhypodbpSolution *sol, const char *sta, char phase)
{
    int i;

    if (!sol) return NAN;
    for (i = 0; i < sol->npicks; i++) {
        const CsnhypodbpPick *pk = &sol->picks[i];
        if (strcmp(pk->sta, sta) != 0) continue;
        if (pk->phase != phase) continue;
        if (!isnan(pk->residual)) return pk->residual;
    }
    return NAN;
}

/* Fuente de residuales de la tabla de Ondas: la candidata de `locate` si esta
 * vigente; si no, la solucion elegida en el detalle (trae los residuales que el
 * localizador ya calculo). NULL si ninguna tiene picadas. */
const CsnhypodbpSolution *csnhypodbp_resid_source(const CsnhypodbpSolution *preview,
                                                  int preview_valid,
                                                  const CsnhypodbpSolution *base)
{
    if (preview_valid && preview && preview->npicks > 0) return preview;
    if (base && base->npicks > 0) return base;
    return NULL;
}

int csnhypodbp_statable_build(CsnhypodbpStaRow *out, int max,
                              const CsnhypodbpStaInput *in, int n,
                              const CsnhypodbpManualPick *man, int nman,
                              const CsnhypodbpSolution *sol)
{
    int i, k, nrows = 0;

    if (!out || !in || max <= 0) return 0;
    for (i = 0; i < n && nrows < max; i++) {
        const CsnhypodbpStaInput *st = &in[i];
        CsnhypodbpStaPick cp[STAPICK_MAX], cs[STAPICK_MAX];
        int ncp = 0, ncs = 0;
        CsnhypodbpStaRow *row = &out[nrows];

        memset(row, 0, sizeof(*row));
        memset(cp, 0, sizeof(cp));
        memset(cs, 0, sizeof(cs));
        row->p.resid = NAN;
        row->s.resid = NAN;
        snprintf(row->sta, sizeof(row->sta), "%s", st->sta);
        row->idx = st->idx;
        row->dist_km = st->dist_deg * 111.19;
    row->dist_deg = st->dist_deg;
        row->has_data = st->has_data;

        for (k = 0; k < st->ncap && st->caps; k++) {
            if (st->caps[k].phase == 'P' && ncp < STAPICK_MAX) {
                cp[ncp].present = 1;
                cp[ncp].origin = st->caps[k].origin ? st->caps[k].origin : 'A';
                cp[ncp].t = st->caps[k].t;
                set_scl(&cp[ncp], st->caps[k].chan, NULL, NULL, st);
                ncp++;
            } else if (st->caps[k].phase == 'S' && ncs < STAPICK_MAX) {
                cs[ncs].present = 1;
                cs[ncs].origin = st->caps[k].origin ? st->caps[k].origin : 'A';
                cs[ncs].t = st->caps[k].t;
                set_scl(&cs[ncs], st->caps[k].chan, NULL, NULL, st);
                ncs++;
            }
        }
        for (k = 0; k < st->nsol && st->sol; k++) {
            if (strcmp(st->sol[k].sta, st->sta) != 0) continue;
            if (st->sol[k].phase == 'P' && ncp < STAPICK_MAX) {
                cp[ncp].present = 1;
                cp[ncp].origin = st->sol[k].manual ? 'M' : 'A';
                cp[ncp].t = st->sol[k].t_pick;
                set_scl(&cp[ncp], st->sol[k].chan, st->sol[k].net,
                        st->sol[k].loc, st);
                ncp++;
            } else if (st->sol[k].phase == 'S' && ncs < STAPICK_MAX) {
                cs[ncs].present = 1;
                cs[ncs].origin = st->sol[k].manual ? 'M' : 'A';
                cs[ncs].t = st->sol[k].t_pick;
                set_scl(&cs[ncs], st->sol[k].chan, st->sol[k].net,
                        st->sol[k].loc, st);
                ncs++;
            }
        }
        for (k = 0; k < nman && man; k++) {
            if (strcmp(man[k].sta, st->sta) != 0) continue;
            if (man[k].phase == 'P' && ncp < STAPICK_MAX) {
                cp[ncp].present = 1; cp[ncp].origin = 'M'; cp[ncp].t = man[k].t;
                set_scl(&cp[ncp], man[k].chan, NULL, NULL, st);
                ncp++;
            } else if (man[k].phase == 'S' && ncs < STAPICK_MAX) {
                cs[ncs].present = 1; cs[ncs].origin = 'M'; cs[ncs].t = man[k].t;
                set_scl(&cs[ncs], man[k].chan, NULL, NULL, st);
                ncs++;
            }
        }
        if (st->theo_p > 0.0 && ncp < STAPICK_MAX) {
            cp[ncp].present = 1; cp[ncp].origin = 'T'; cp[ncp].t = st->theo_p;
            /* La teorica no trae SCNL: se usa el de la estacion. */
            set_scl(&cp[ncp], NULL, NULL, NULL, st);
            ncp++;
        }

        if (csnhypodbp_stapick_one(cp, ncp, st->theo_p, &row->p)) {
            row->p.resid = resid_for(sol, st->sta, 'P');
        }
        if (csnhypodbp_stapick_one(cs, ncs, 0.0, &row->s)) {
            row->s.resid = resid_for(sol, st->sta, 'S');
        }
        nrows++;
    }
    return nrows;
}

int csnhypodbp_pick_allowed(char origin, int idx, int ph,
                            const int (*pick_on)[2],
                            const CsnhypodbpPickFilter *f)
{
    if (idx >= 0) {
        if (pick_on && ph >= 0 && ph < 2 && !pick_on[idx][ph]) return 0;
    }
    switch (origin) {
        case 'M': case 'm': return f ? f->manual_on : 1;
        case 'A': case 'a': return f ? f->auto_on : 0;
        case 'T': case 't': return f ? f->theo_on : 0;
        default:            return 1;
    }
}

int csnhypodbp_pick_on_from_used(const int *mag_sta_idx, const int *used, int nmag,
                                 int (*pick_on)[2], int nrows)
{
    int i;

    if (!mag_sta_idx || !used || !pick_on || nrows <= 0) return -1;
    for (i = 0; i < nrows; i++) pick_on[i][0] = pick_on[i][1] = 0;
    for (i = 0; i < nmag; i++) {
        int s = mag_sta_idx[i];
        if (s < 0 || s >= nrows) continue;
        if (!used[i]) continue;
        pick_on[s][0] = pick_on[s][1] = 1;
    }
    return 0;
}

int csnhypodbp_locate_picks(const CsnhypodbpStaRow *rows, int nrows,
                            const int (*pick_on)[2],
                            const CsnhypodbpPickFilter *f,
                            CsnhypodbpPick *out, int max_out)
{
    int i, n = 0;

    if (!rows || !out || max_out <= 0) return 0;
    for (i = 0; i < nrows; i++) {
        const CsnhypodbpStaRow *r = &rows[i];
        if (r->p.present && n < max_out &&
            csnhypodbp_pick_allowed(r->p.origin, r->idx, 0, pick_on, f)) {
            CsnhypodbpPick *pk = &out[n++];
            memset(pk, 0, sizeof(*pk));
            snprintf(pk->sta, sizeof(pk->sta), "%s", r->sta);
            snprintf(pk->chan, sizeof(pk->chan), "%s", r->p.chan);
            snprintf(pk->net, sizeof(pk->net), "%s", r->p.net);
            snprintf(pk->loc, sizeof(pk->loc), "%s", r->p.loc);
            pk->phase = 'P'; pk->t_pick = r->p.t;
            pk->manual = (r->p.origin == 'M') ? 1 : 0;
            pk->residual = NAN;   /* lo aporta el localizador, no se hereda */
        }
        if (r->s.present && n < max_out &&
            csnhypodbp_pick_allowed(r->s.origin, r->idx, 1, pick_on, f)) {
            CsnhypodbpPick *pk = &out[n++];
            memset(pk, 0, sizeof(*pk));
            snprintf(pk->sta, sizeof(pk->sta), "%s", r->sta);
            snprintf(pk->chan, sizeof(pk->chan), "%s", r->s.chan);
            snprintf(pk->net, sizeof(pk->net), "%s", r->s.net);
            snprintf(pk->loc, sizeof(pk->loc), "%s", r->s.loc);
            pk->phase = 'S'; pk->t_pick = r->s.t;
            pk->manual = (r->s.origin == 'M') ? 1 : 0;
            pk->residual = NAN;   /* lo aporta el localizador, no se hereda */
        }
    }
    return n;
}
