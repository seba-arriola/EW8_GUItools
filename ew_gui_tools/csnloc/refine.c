/******************************************************************************
 * refine.c                                                                   *
 *                                                                            *
 * Refinamiento hipocentral MVP: busqueda local exhaustiva en una caja fina   *
 * alrededor del candidato. Para cada punto candidato se repite el mismo      *
 * esquema de histograma de t0 que la back-projection gruesa, pero con paso    *
 * pequeno. El resultado es una localizacion "muy decente" y determinista.    *
 *                                                                            *
 * El refinamiento se realiza en el hilo llamador con un TTModel ya           *
 * precomputado, por lo que no toca estado global de taulib.                  *
 ******************************************************************************/
#include "csnloc.h"

#ifndef RAD
#define RAD 0.017453292519943
#endif

#define KM_PER_DEG 111.195

static double pick_weight(const Pick *p, const CSLocParams *cfg)
{
    double base = (p->phase == CSLOC_PHASE_S) ? cfg->PhaseWeightS
                                              : cfg->PhaseWeightP;
    double w = base * (1.0 - 0.15 * (double)(p->weight));
    return (w < 0.1) ? 0.1 : w;
}

/* Evalua el apilamiento en (lat_geoc, lon, depth). Devuelve peso total y t0. */
static double score_at(const StationList *st, const Pick *picks, int npick,
                       TTModel *tt, const CSLocParams *cfg,
                       double lat_geoc, double lon, double depth,
                       double *out_t0)
{
    double best_t0 = 0.0, best_score = 0.0;
    double t0_lo, t0_hi, step = 1.0;
    int    np, nbins, b;

    /* Rango de t0 a explorar: min(t_pick - tt) con margen. */
    t0_lo = 1e18; t0_hi = -1e18;
    for (np = 0; np < npick; np++) {
        int    sidx = Stations_Find(st, picks[np].sta);
        double delta, tpred, implied;
        if (sidx < 0) continue;
        delta = TT_GreatCircleDeg(lat_geoc, lon,
                                  st->lat_geoc_sta[sidx], st->st[sidx].lon);
        if (TTModel_Predict(tt, delta, depth, picks[np].phase, &tpred, NULL) != 0)
            continue;
        implied = picks[np].t_epoch - tpred;
        if (implied < t0_lo) t0_lo = implied;
        if (implied > t0_hi) t0_hi = implied;
    }
    if (t0_hi < t0_lo) return 0.0;
    if ((t0_hi - t0_lo) > 600.0) t0_hi = t0_lo + 600.0;

    nbins = (int)floor((t0_hi - t0_lo) / step) + 2;
    if (nbins > 4096) nbins = 4096;

    {
        double *hist = (double *)calloc((size_t)nbins, sizeof(double));
        double *csum = (double *)calloc((size_t)nbins + 1, sizeof(double));
        int     half;
        if (!hist || !csum) { free(hist); free(csum); return 0.0; }

        for (np = 0; np < npick; np++) {
            int    sidx = Stations_Find(st, picks[np].sta);
            double delta, tpred, implied;
            int    bin;
            if (sidx < 0) continue;
            delta = TT_GreatCircleDeg(lat_geoc, lon,
                                      st->lat_geoc_sta[sidx], st->st[sidx].lon);
            if (TTModel_Predict(tt, delta, depth, picks[np].phase, &tpred, NULL) != 0)
                continue;
            implied = picks[np].t_epoch - tpred;
            bin = (int)floor((implied - t0_lo) / step + 0.5);
            if (bin < 0 || bin >= nbins) continue;
            hist[bin] += pick_weight(&picks[np], cfg);
        }

        /* Misma ventana de tolerancia que la back-projection gruesa. */
        half = (int)ceil((cfg->T0ToleranceSec > 0.0 ?
                          cfg->T0ToleranceSec : 2.0) / step);
        if (half < 0) half = 0;
        if (half > 100) half = 100;

        csum[0] = 0.0;
        for (b = 0; b < nbins; b++) csum[b + 1] = csum[b] + hist[b];
        for (b = 0; b < nbins; b++) {
            int lo = b - half; if (lo < 0) lo = 0;
            int hi = b + half + 1; if (hi > nbins) hi = nbins;
            double sum = csum[hi] - csum[lo];
            if (sum > best_score) {
                best_score = sum;
                best_t0 = t0_lo + (double)b * step;
            }
        }
        free(hist);
        free(csum);
    }

    /* Prior gaussiano de profundidad (opcional): penaliza alejarse del valor
       a priori. Desactivado por defecto (DepthPriorKm = 0). */
    if (cfg->DepthPriorKm > 0.0 && cfg->DepthPriorSigmaKm > 0.0) {
        double d = (depth - cfg->DepthPriorKm) / cfg->DepthPriorSigmaKm;
        best_score *= exp(-0.5 * d * d);
    }

    if (out_t0) *out_t0 = best_t0;
    return best_score;
}

/* ------------------------------------------------------------------------- */
/* Refina un candidato. Caja horizontal +-2*RefineNodeKm y vertical           */
/* +-RefineDepthKm (default 2*RefineNodeKm); se encoge a la mitad por         */
/* iteracion. El semiancho vertical debe superar el paso de nodos de la       */
/* grilla para que z no quede clavada en un nodo.                             */
/* ------------------------------------------------------------------------- */
int RefineHypo(HypoCandidate *h, const StationList *st, const Pick *picks,
               TTModel *tt, const CSLocParams *cfg)
{
    double node_km, dep_half, step_dep, lat_span, lon_span;
    double best_lat, best_lon, best_dep, best_t0, best_score;
    int    it;

    if (!h || !st || !picks || !tt || !cfg) return -1;
    if (h->nphases <= 0) return -1;

    node_km = (cfg->RefineNodeKm > 0.0) ? cfg->RefineNodeKm : 5.0;
    dep_half = (cfg->RefineDepthKm > 0.0) ? cfg->RefineDepthKm
                                          : (2.0 * node_km);
    lat_span = node_km * 2.0 / KM_PER_DEG;
    lon_span = node_km * 2.0 / (KM_PER_DEG * 0.8);
    /* Paso vertical FINO e independiente del tamano de la caja: si el paso
       fuera proporcional al span, una caja grande (para escapar del nodo de
       grilla) saltaria por encima del optimo local. */
    step_dep = node_km / 2.0;
    if (step_dep < 0.5) step_dep = 0.5;

    best_lat = h->lat;
    best_lon = h->lon;
    best_dep = h->depth_km;
    best_score = score_at(st, picks, h->nphases, tt, cfg,
                          best_lat, best_lon, best_dep, &best_t0);

    for (it = 0; it < cfg->RefineIterations && it < 5; it++) {
        double step_lat = lat_span / 4.0;
        double step_lon = lon_span / 4.0;
        int    nz = (int)ceil(dep_half / step_dep);
        int    a, b, c;
        double improved_lat = best_lat, improved_lon = best_lon;
        double improved_dep = best_dep, improved_t0 = best_t0, improved = best_score;

        if (nz < 2) nz = 2;
        if (nz > 100) nz = 100;

        for (a = -2; a <= 2; a++)
            for (b = -2; b <= 2; b++)
                for (c = -nz; c <= nz; c++) {
                    double la = best_lat + a * step_lat;
                    double lo = best_lon + b * step_lon;
                    double de = best_dep + c * step_dep;
                    double t0, s;
                    if (de < 0.0) continue;
                    if (la < -89.0 || la > 89.0) continue;
                    if (lo < -180.0) lo += 360.0;
                    if (lo > 180.0) lo -= 360.0;
                    s = score_at(st, picks, h->nphases, tt, cfg, la, lo, de, &t0);
                    if (s > improved) {
                        improved = s;
                        improved_lat = la; improved_lon = lo;
                        improved_dep = de; improved_t0 = t0;
                    }
                }

        best_lat = improved_lat; best_lon = improved_lon;
        best_dep = improved_dep; best_t0 = improved_t0; best_score = improved;
        lat_span *= 0.5;
        lon_span *= 0.5;
        dep_half *= 0.5;
        step_dep *= 0.5;
    }

    h->lat = best_lat;
    h->lon = best_lon;
    h->depth_km = best_dep;
    h->t0 = best_t0;

    /* Recalcular residuales, RMS y gap azimutal con la posicion refinada. */
    {
        double sum_sq = 0.0;
        int    nph = 0, i;
        double az[CSLOC_MAX_PHASES];
        double dmin = 1e9;
        for (i = 0; i < h->nphases; i++) {
            int    pi = h->phase_idx[i];
            int    sidx;
            double delta, tpred, resid, azdeg;
            if (pi < 0) continue;
            sidx = Stations_Find(st, picks[pi].sta);
            if (sidx < 0) continue;
            delta = TT_GreatCircleDeg(best_lat, best_lon,
                                      st->lat_geoc_sta[sidx], st->st[sidx].lon);
            if (TTModel_Predict(tt, delta, best_dep, picks[pi].phase,
                                &tpred, NULL) != 0)
                continue;
            resid = picks[pi].t_epoch - (best_t0 + tpred);
            h->residual[nph] = resid;
            sum_sq += resid * resid;
            azdeg = TT_AzimuthDeg(best_lat, best_lon,
                                  st->lat_geoc_sta[sidx], st->st[sidx].lon);
            az[nph] = azdeg;
            if (delta * KM_PER_DEG < dmin) dmin = delta * KM_PER_DEG;
            nph++;
        }
        h->nphases = nph;
        h->rms_sec = (nph > 0) ? sqrt(sum_sq / nph) : 0.0;
        h->dmin_km = (dmin < 1e8) ? dmin : 0.0;

        if (nph >= 2) {
            double maxgap = 0.0;
            int    a, b;
            for (a = 0; a < nph; a++) az[a] = fmod(az[a] + 360.0, 360.0);
            for (a = 0; a < nph; a++)
                for (b = a + 1; b < nph; b++)
                    if (az[b] < az[a]) { double t = az[a]; az[a] = az[b]; az[b] = t; }
            for (a = 0; a < nph; a++) {
                double gap = az[(a + 1) % nph] - az[a];
                if (gap < 0.0) gap += 360.0;
                if (gap > maxgap) maxgap = gap;
            }
            h->gap_deg = maxgap;
        } else {
            h->gap_deg = 360.0;
        }
    }

    return 0;
}