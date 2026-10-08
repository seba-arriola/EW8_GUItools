/******************************************************************************
 * backprojection.c                                                           *
 *                                                                            *
 * Back-projection 4D rapida.                                                 *
 *                                                                            *
 * Para cada nodo (lat, lon, depth) de la grilla y cada pick se calcula el     *
 * tiempo de viaje esperado y el origen implicito  t0 = t_pick - tt.  Todos    *
 * los t0 implicitos se acumulan en un histograma y el maximo del histograma   *
 * es el "stack" del nodo.  Un nodo con mucho apilamiento es una hipotesis de  *
 * evento.  El algoritmo es O(nodos * (npicks + nbins_t0)) y paraleliza por    *
 * rebanadas de profundidad sin condiciones de carrera.                        *
 *                                                                            *
 * Los maximos locales por encima del umbral se agrupan con DBSCAN en el       *
 * espacio (x_km, y_km, depth_km, v*t0) y se ensamblan las fases de cada       *
 * evento resultante.                                                          *
 ******************************************************************************/
#include "csnloc.h"
#include <pthread.h>

#ifndef RAD
#define RAD 0.017453292519943
#endif

#define KM_PER_DEG   111.195
#define T0_STEP      1.0      /* paso del histograma de t0 (s)              */
#define T0_MAX_BINS  4096
#define T0_MAX_TT    900.0    /* viaje maximo considerado (s)               */

/* ------------------------------------------------------------------------- */
/* Utilidades                                                                 */
/* ------------------------------------------------------------------------- */
double BProj_GeoToGeoc(double lat_deg);

/* Contexto compartido de un lote de back-projection. */
typedef struct {
    const Grid        *g;
    const StationList *st;
    const Pick        *picks;
    int                npick;
    const int         *pick_sidx;  /* indice de estacion por pick (-1)        */
    const int         *pick_head;  /* CSR inverso: estacion -> primer pick    */
    const int         *pick_next;  /* siguiente pick de la misma estacion     */
    TTModel           *tt;
    const CSLocParams *cfg;

    double  t0_lo;            /* origen minimo posible                      */
    int     nt0;
    double *stack;            /* nx*ny*nz                                    */
    double *t0_of_node;       /* nx*ny*nz                                    */

    int     nthreads;
    /* Reparto: cada worker procesa un rango de indices lineales de nodo. */
} BProjJob;

/* Factor de peso del pick (menor weight = mejor). */
static double pick_weight(const Pick *p, const CSLocParams *cfg)
{
    double base = (p->phase == CSLOC_PHASE_S) ? cfg->PhaseWeightS
                                              : cfg->PhaseWeightP;
    double w = base * (1.0 - 0.15 * (double)(p->weight));
    return (w < 0.1) ? 0.1 : w;
}

/* Tiempo de viaje pick->nodo. Devuelve -1 si no aplica. */
static int predict_tt(TTModel *tt, const StationList *st,
                      const Pick *p, double lat_geoc, double lon, double depth,
                      double *out_tt, double *out_delta)
{
    int    sidx = Stations_Find(st, p->sta);
    double delta;

    if (sidx < 0) return -1;
    /* p->chan/net/loc no se validan contra metadata (puede variar). */
    delta = TT_GreatCircleDeg(lat_geoc, lon,
                              st->lat_geoc_sta[sidx], st->st[sidx].lon);
    if (out_delta) *out_delta = delta;
    if (TTModel_Predict(tt, delta, depth, p->phase, out_tt, NULL) != 0)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Nucleacion: busca maximos locales del stack por encima del umbral.         */
/* ------------------------------------------------------------------------- */
static int find_nucleations(const Grid *g, const double *stack,
                            const double *t0_of_node,
                            const CSLocParams *cfg,
                            HypoCandidate *cand, int max_cand)
{
    int    iy, ix, iz, n = 0;
    double smax = 0.0, thresh;
    int    ny = g->ny, nx = g->nx, nz = g->nz;

    for (iz = 0; iz < nz; iz++)
        for (iy = 0; iy < ny; iy++)
            for (ix = 0; ix < nx; ix++) {
                double s = stack[((iz * ny) + iy) * nx + ix];
                if (s > smax) smax = s;
            }

    if (smax <= 0.0) return 0;

    thresh = cfg->BackProjThreshold;
    if (thresh <= 0.0 || thresh > 1.0) {
        /* default: 50% del maximo global, al menos MinPhasesPerEvent */
        thresh = 0.5 * smax;
        if (thresh < (double)cfg->MinPhasesPerEvent)
            thresh = (double)cfg->MinPhasesPerEvent;
    } else {
        thresh = thresh * smax;
        if (thresh < (double)cfg->MinPhasesPerEvent)
            thresh = (double)cfg->MinPhasesPerEvent;
    }

    for (iz = 0; iz < nz; iz++)
        for (iy = 0; iy < ny; iy++)
            for (ix = 0; ix < nx; ix++) {
                int    di, dj, dk;
                double s = stack[((iz * ny) + iy) * nx + ix];
                int    is_max = 1;

                if (s < thresh) continue;

                for (dk = -1; dk <= 1 && is_max; dk++)
                    for (dj = -1; dj <= 1 && is_max; dj++)
                        for (di = -1; di <= 1 && is_max; di++) {
                            int jz = iz + dk, jy = iy + dj, jx = ix + di;
                            if (jz < 0 || jz >= nz || jy < 0 || jy >= ny ||
                                jx < 0 || jx >= nx) continue;
                            if (dk == 0 && dj == 0 && di == 0) continue;
                            if (stack[((jz * ny) + jy) * nx + jx] > s) is_max = 0;
                        }
                if (!is_max) continue;
                if (n >= max_cand) return n;

                memset(&cand[n], 0, sizeof(HypoCandidate));
                cand[n].lat = g->lat[iy];
                cand[n].lon = g->lon[ix];
                cand[n].depth_km = g->depth[iz];
                cand[n].t0 = t0_of_node[((iz * ny) + iy) * nx + ix];
                cand[n].nphases = 0;
                cand[n].score = s;
                cand[n].grid_level = g->level;
                n++;
            }
    return n;
}

/* ------------------------------------------------------------------------- */
/* Merge de nucleaciones cercanas usando DBSCAN + ensamblado de fases.        */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* Instrumentacion de AssembleCandidates (solo diagnostico: tiempos y conteo  */
/* de llamadas a la tabla de tiempos).                                        */
/* ------------------------------------------------------------------------- */
static double g_asm_db, g_asm_av, g_asm_cl;
static long   g_tt_calls;
/* AssembleCandidates corre en varias grillas en paralelo, asi que estos
   acumuladores los escriben varios hilos: van con mutex. */
static pthread_mutex_t g_asm_mx = PTHREAD_MUTEX_INITIALIZER;

static double asm_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

void AssembleCandidates_Stats(double *dbscan, double *avg, double *claim,
                              long *tt_calls)
{
    if (dbscan)   *dbscan   = g_asm_db;
    if (avg)      *avg      = g_asm_av;
    if (claim)    *claim    = g_asm_cl;
    if (tt_calls) *tt_calls = g_tt_calls;
}

int AssembleCandidates(const HypoCandidate *cand, int ncand,
                       const StationList *st,
                       const Pick *picks, int npick, TTModel *tt,
                       const CSLocParams *cfg,
                       HypoCandidate *out, int max_out)
{
    double  *feat;
    int     *labels, nclusters = 0, i, c;
    double   eps_km;
    int     *counts;
    int      nout = 0;
    double   t0;
    HypoCandidate *avg = NULL;   /* promedio por cluster (nclusters slots) */

    if (ncand <= 0) return 0;

    feat   = (double *)malloc(sizeof(double) * ncand * 4);
    labels = (int *)malloc(sizeof(int) * ncand);
    if (!feat || !labels) { free(feat); free(labels); return 0; }

    for (i = 0; i < ncand; i++) {
        /* Diferencia de t0 respecto del primer candidato, para agrupar solo
           por simultaneidad (no por epoch absoluto, que domina la metrica). */
        double dt0 = cand[i].t0 - cand[0].t0;
        /* x,y en km; z=profundidad km; w=v*dt0 con v=8 km/s */
        feat[i * 4 + 0] = cand[i].lon * KM_PER_DEG * cos(cand[i].lat * RAD);
        feat[i * 4 + 1] = cand[i].lat * KM_PER_DEG;
        feat[i * 4 + 2] = cand[i].depth_km;
        feat[i * 4 + 3] = dt0 * 8.0;
    }

    eps_km = (cfg->DBSCAN_Eps > 0.0) ? cfg->DBSCAN_Eps : 50.0;
    t0 = asm_now();
    nclusters = DBSCAN_Cluster(feat, ncand, 4, eps_km, 1, labels);
    pthread_mutex_lock(&g_asm_mx);
    g_asm_db += asm_now() - t0;
    pthread_mutex_unlock(&g_asm_mx);
    if (nclusters <= 0) { free(feat); free(labels); return 0; }

    if (getenv("CSNLOC_DEBUG"))
        fprintf(stderr, "[dbg] nucleaciones=%d clusters=%d eps=%.1f\n",
                ncand, nclusters, eps_km);

    t0 = asm_now();
    counts = (int *)calloc((size_t)nclusters, sizeof(int));
    avg    = (HypoCandidate *)calloc((size_t)nclusters, sizeof(HypoCandidate));
    if (!counts || !avg) { free(avg); free(counts); free(feat); free(labels); return 0; }

    /* Promedio ponderado de las nucleaciones de cada cluster. El score del
       cluster es la suma de los scores de sus nucleaciones, lo que ordena
       las hipotesis por evidencia. */
    for (c = 0; c < nclusters; c++) {
        int n = 0;
        memset(&avg[c], 0, sizeof(HypoCandidate));
        for (i = 0; i < ncand; i++) {
            if (labels[i] != c) continue;
            n++;
            avg[c].lat += cand[i].lat;
            avg[c].lon += cand[i].lon;
            avg[c].depth_km += cand[i].depth_km;
            avg[c].t0 += cand[i].t0;
            avg[c].score += cand[i].score;
        }
        if (n > 0) {
            avg[c].lat /= n; avg[c].lon /= n;
            avg[c].depth_km /= n; avg[c].t0 /= n;
        }
        counts[c] = n;
        if (getenv("CSNLOC_DEBUG"))
            fprintf(stderr, "[dbg] cl%d n=%d score=%.2f lat=%.3f lon=%.3f "
                            "z=%.1f t0=%.1f\n",
                    c, n, avg[c].score, avg[c].lat, avg[c].lon,
                    avg[c].depth_km, avg[c].t0);
    }

    /*
     * FASE 1: ordenar los clusters por evidencia (score y nucleaciones) y
     * refinar su posicion con un promedio ponderado por score de sus
     * nucleaciones. Las mejores hipotesis se ensamblan primero.
     */
    {
        int order[CSLOC_MAX_CAND];
        int ncl2 = (nclusters < CSLOC_MAX_CAND) ? nclusters : CSLOC_MAX_CAND;
        HypoCandidate *cl = (HypoCandidate *)calloc((size_t)ncl2,
                                                    sizeof(HypoCandidate));
        if (!cl) { free(avg); free(counts); free(feat); free(labels); return 0; }

        for (c = 0; c < ncl2; c++) {
            /* El representante del cluster es la nucleacion con MAYOR score,
               no el promedio: una cresta de apilamiento amplia promediada
               daria una posicion intermedia inexistente. */
            int    best_i = -1;
            double best_s = -1.0;
            for (i = 0; i < ncand; i++) {
                if (labels[i] != c) continue;
                if (cand[i].score > best_s) { best_s = cand[i].score; best_i = i; }
            }
            if (best_i >= 0) cl[c] = cand[best_i];
            else             cl[c] = avg[c];
            cl[c].score = best_s;
            order[c] = c;
        }
        /* Ordenamiento por score descendente (insercion). */
        for (i = 1; i < ncl2; i++) {
            int key = order[i];
            int j = i - 1;
            while (j >= 0 && cl[order[j]].score < cl[key].score) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = key;
        }
        pthread_mutex_lock(&g_asm_mx);
        g_asm_av += asm_now() - t0;
        pthread_mutex_unlock(&g_asm_mx);

        /*
         * FASE 2: asignacion EXCLUSIVA y codiciosa.
         *
         * Se procesan las hipotesis en orden de score descendente. Cada
         * hipotesis reclama los picks libres que caen en su curva de tiempos
         * dentro de res_tol, respetando unicidad estacion+fase. Como las
         * hipotesis con mas evidencia reclaman primero, los eventos reales
         * se quedan con sus fases y los fragmentos espurios de la misma
         * cresta quedan por debajo de MinPhasesPerEvent y se descartan.
         */
        {
            int    *owner;
            int     p, k, a, b;
            double  res_tol = (cfg->MaxRMS > 0.0) ? cfg->MaxRMS * 1.5 : 3.0;
            if (res_tol < 1.5) res_tol = 1.5;

            owner = (int *)malloc(sizeof(int) * (size_t)npick);
            if (!owner) { free(cl); free(avg); free(counts); free(feat); free(labels); return 0; }
            for (p = 0; p < npick; p++) owner[p] = -1;
            t0 = asm_now();

            for (i = 0; i < ncl2; i++) {
                int cc = order[i];
                int claimed[CSLOC_MAX_PHASES];
                int nclaimed = 0;

                for (p = 0; p < npick && nclaimed < CSLOC_MAX_PHASES; p++) {
                    double tpred, delta, resid;
                    int    dup = 0;
                    if (owner[p] != -1) continue;
                    __atomic_fetch_add(&g_tt_calls, 1, __ATOMIC_RELAXED);
                    if (predict_tt(tt, st, &picks[p], cl[cc].lat, cl[cc].lon,
                                   cl[cc].depth_km, &tpred, &delta) != 0)
                        continue;
                    resid = picks[p].t_epoch - (cl[cc].t0 + tpred);
                    if (fabs(resid) > res_tol) continue;
                    for (k = 0; k < nclaimed; k++) {
                        if (strcmp(picks[claimed[k]].sta, picks[p].sta) == 0 &&
                            picks[claimed[k]].phase == picks[p].phase) {
                            dup = 1;
                            break;
                        }
                    }
                    if (dup) continue;
                    claimed[nclaimed++] = p;
                }

                if (nclaimed < cfg->MinPhasesPerEvent) continue;
                for (k = 0; k < nclaimed; k++) owner[claimed[k]] = cc;
            }

            /* Estadisticas por hipotesis a partir de los picks asignados. */
            {
                double *sum_sq = (double *)calloc((size_t)ncl2, sizeof(double));
                double *azs[CSLOC_MAX_CAND];
                int     naz[CSLOC_MAX_CAND];
                double  dmins[CSLOC_MAX_CAND];
                if (!sum_sq) { free(owner); free(cl); free(avg); free(counts); free(feat); free(labels); return 0; }

                for (i = 0; i < ncl2; i++) {
                    cl[i].nphases = 0;
                    cl[i].rms_sec = 0.0;
                    cl[i].gap_deg = 360.0;
                    cl[i].dmin_km = 0.0;
                    azs[i] = (double *)calloc(CSLOC_MAX_PHASES, sizeof(double));
                    naz[i] = 0;
                    dmins[i] = 1e9;
                }

                for (p = 0; p < npick; p++) {
                    int    cc = owner[p];
                    double tpred, delta, resid, azdeg;
                    int    sidx;
                    if (cc < 0 || cc >= ncl2) continue;
                    if (cl[cc].nphases >= CSLOC_MAX_PHASES) continue;
                    __atomic_fetch_add(&g_tt_calls, 1, __ATOMIC_RELAXED);
                    if (predict_tt(tt, st, &picks[p], cl[cc].lat, cl[cc].lon,
                                   cl[cc].depth_km, &tpred, &delta) != 0)
                        continue;
                    resid = picks[p].t_epoch - (cl[cc].t0 + tpred);
                    cl[cc].phase_idx[cl[cc].nphases] = p;
                    cl[cc].residual[cl[cc].nphases] = resid;
                    sum_sq[cc] += resid * resid;
                    sidx = Stations_Find(st, picks[p].sta);
                    if (sidx >= 0) {
                        azdeg = TT_AzimuthDeg(cl[cc].lat, cl[cc].lon,
                                             st->lat_geoc_sta[sidx],
                                             st->st[sidx].lon);
                        if (azs[cc] && naz[cc] < CSLOC_MAX_PHASES)
                            azs[cc][naz[cc]++] = azdeg;
                        if (delta * KM_PER_DEG < dmins[cc])
                            dmins[cc] = delta * KM_PER_DEG;
                    }
                    cl[cc].nphases++;
                }

                for (i = 0; i < ncl2; i++) {
                    if (cl[i].nphases > 0)
                        cl[i].rms_sec = sqrt(sum_sq[i] / cl[i].nphases);
                    cl[i].dmin_km = (dmins[i] < 1e8) ? dmins[i] : 0.0;
                    if (naz[i] >= 2) {
                        double maxgap = 0.0;
                        for (a = 0; a < naz[i]; a++)
                            azs[i][a] = fmod(azs[i][a] + 360.0, 360.0);
                        for (a = 0; a < naz[i]; a++)
                            for (b = a + 1; b < naz[i]; b++)
                                if (azs[i][b] < azs[i][a]) {
                                    double t = azs[i][a];
                                    azs[i][a] = azs[i][b];
                                    azs[i][b] = t;
                                }
                        for (a = 0; a < naz[i]; a++) {
                            double gap = azs[i][(a + 1) % naz[i]] - azs[i][a];
                            if (gap < 0.0) gap += 360.0;
                            if (gap > maxgap) maxgap = gap;
                        }
                        cl[i].gap_deg = maxgap;
                    }
                    free(azs[i]);
                }
                free(sum_sq);
            }
            pthread_mutex_lock(&g_asm_mx);
            g_asm_cl += asm_now() - t0;
            pthread_mutex_unlock(&g_asm_mx);

            /* Emitir en orden de evidencia con umbrales. */
            for (i = 0; i < ncl2 && nout < max_out; i++) {
                int cc = order[i];
                if (cl[cc].nphases < cfg->MinPhasesPerEvent) continue;
                if (cfg->MaxRMS > 0.0 && cl[cc].rms_sec > cfg->MaxRMS) continue;
                out[nout++] = cl[cc];
            }
            free(owner);
        }
        free(cl);
    }

    free(avg);
    free(counts);
    free(feat);
    free(labels);
    return nout;
}

/* ------------------------------------------------------------------------- */
/* Ejecucion del back-projection con hilos.                                   */
/* ------------------------------------------------------------------------- */
typedef struct {
    BProjJob *job;
    int       i0, i1;   /* rango lineal de nodos [i0, i1) */
} BProjSlice;

static void bproj_slice(BProjSlice *sl)
{
    BProjJob *j = sl->job;
    int      *hist;
    long     *csum;         /* suma acumulada para ventana deslizante */
    int       idx, half;
    int       ny = j->g->ny, nx = j->g->nx;

    hist = (int *)calloc((size_t)j->nt0, sizeof(int));
    csum = (long *)calloc((size_t)j->nt0 + 1, sizeof(long));
    if (!hist || !csum) { free(hist); free(csum); return; }

    /* Semiancho de la ventana de t0 (en bins): el stacking debe tolerar el
       error de discretizacion de la tabla de tiempos de viaje. */
    half = (int)ceil((j->cfg->T0ToleranceSec > 0.0 ?
                      j->cfg->T0ToleranceSec : 2.0) / T0_STEP);
    if (half < 0) half = 0;
    if (half > 50) half = 50;

    for (idx = sl->i0; idx < sl->i1; idx++) {
        int    iy = (idx / nx) % ny;
        int    iz = idx / (nx * ny);
        double lat, lon, depth;
        int    p, b;
        int    best_bin = -1;
        long   best_sum = 0;
        double best_t0 = j->t0_lo;

        /* Nodo sin estaciones asociadas: no puede apilar nada. Se evita el
           coste del histograma (critico en grillas grandes, p.ej. la global). */
        if (j->g->node_sta_off &&
            j->g->node_sta_off[idx] == j->g->node_sta_off[idx + 1]) {
            j->stack[idx] = 0.0;
            j->t0_of_node[idx] = j->t0_lo;
            continue;
        }

        lat = j->g->lat[iy];
        lon = j->g->lon[(idx % nx)];
        depth = j->g->depth[iz];

        memset(hist, 0, sizeof(int) * (size_t)j->nt0);

        if (j->g->node_sta_off) {
            /* Camino precomputado: por nodo, recorrer solo sus estaciones
               asociadas (CSR) y, por cada una, sus picks en la ventana. */
            int k0 = j->g->node_sta_off[idx];
            int k1 = j->g->node_sta_off[idx + 1];
            int k;
            for (k = k0; k < k1; k++) {
                int    s = j->g->node_sta_idx[k];
                double delta;
                int    q;
                if (s < 0 || s >= j->st->n) continue;
                if (j->pick_head[s] < 0) continue;
                delta = TT_GreatCircleDeg(lat, lon,
                                          j->st->lat_geoc_sta[s],
                                          j->st->st[s].lon);
                for (q = j->pick_head[s]; q != -1; q = j->pick_next[q]) {
                    double tt, implied;
                    int    bin;
                    if (TTModel_Predict(j->tt, delta, depth,
                                        j->picks[q].phase, &tt, NULL) != 0)
                        continue;
                    implied = j->picks[q].t_epoch - tt;
                    bin = (int)floor((implied - j->t0_lo) / T0_STEP + 0.5);
                    if (bin < 0 || bin >= j->nt0) continue;
                    hist[bin] += (int)(pick_weight(&j->picks[q], j->cfg) * 100.0);
                }
            }
        } else {
            /* Fallback sin CSR: todos los picks, sin Stations_Find. */
            for (p = 0; p < j->npick; p++) {
                double tt, implied, delta;
                int    bin, s = j->pick_sidx ? j->pick_sidx[p] : -1;
                if (s < 0 || s >= j->st->n) continue;
                delta = TT_GreatCircleDeg(lat, lon,
                                          j->st->lat_geoc_sta[s],
                                          j->st->st[s].lon);
                if (TTModel_Predict(j->tt, delta, depth,
                                    j->picks[p].phase, &tt, NULL) != 0)
                    continue;
                implied = j->picks[p].t_epoch - tt;
                bin = (int)floor((implied - j->t0_lo) / T0_STEP + 0.5);
                if (bin < 0 || bin >= j->nt0) continue;
                hist[bin] += (int)(pick_weight(&j->picks[p], j->cfg) * 100.0);
            }
        }

        /* Suma acumulada para evaluar ventanas [b-half, b+half]. */
        csum[0] = 0;
        for (b = 0; b < j->nt0; b++) csum[b + 1] = csum[b] + hist[b];

        for (b = 0; b < j->nt0; b++) {
            int lo = b - half; if (lo < 0) lo = 0;
            int hi = b + half + 1; if (hi > j->nt0) hi = j->nt0;
            long sum = csum[hi] - csum[lo];
            if (sum > best_sum) { best_sum = sum; best_bin = b; }
        }

        if (best_bin >= 0 && best_sum > 0) {
            j->stack[idx] = (double)best_sum / 100.0;
            best_t0 = j->t0_lo + (double)best_bin * T0_STEP;
        } else {
            j->stack[idx] = 0.0;
        }
        j->t0_of_node[idx] = best_t0;
    }

    free(hist);
    free(csum);
}

/* Hilo con argumento: usamos StartThreadWithArg (pthread). */
static void *slice_thread(void *arg)
{
    BProjSlice *sl = (BProjSlice *)arg;
    bproj_slice(sl);
    return NULL;
}

int BackProject_Nucleations(const Grid *g, const StationList *st,
                            const Pick *picks, int npick, TTModel *tt,
                            const CSLocParams *cfg, HypoCandidate *nuc,
                            int max_nuc, int nthreads)
{
    BProjJob  job;
    BProjSlice *slices;
    pthread_t *tids;
    double     min_t = 1e18;
    int        total, nthreads_eff, i, t, n_nuc = 0;
    int       *pick_sidx = NULL, *pick_head = NULL, *pick_next = NULL;
    CSLocParams cfg_eff;

    if (!g || !st || !picks || npick <= 0 || !tt || !cfg || !nuc) return 0;
    if (g->n_nodes <= 0) return 0;

    /* Overrides por grilla (0 / <0 significan "heredar"). */
    cfg_eff = *cfg;
    if (g->min_phases > 0) cfg_eff.MinPhasesPerEvent = g->min_phases;
    if (g->backproj_threshold >= 0.0)
        cfg_eff.BackProjThreshold = g->backproj_threshold;

    /* Mapa pick -> estacion y CSR inverso estacion -> picks. */
    pick_sidx = (int *)malloc(sizeof(int) * (size_t)npick);
    pick_head = (int *)malloc(sizeof(int) * (size_t)(st->n > 0 ? st->n : 1));
    pick_next = (int *)malloc(sizeof(int) * (size_t)npick);
    if (!pick_sidx || !pick_head || !pick_next) {
        free(pick_sidx); free(pick_head); free(pick_next);
        return 0;
    }
    for (i = 0; i < st->n; i++) pick_head[i] = -1;
    for (i = 0; i < npick; i++) {
        int s = Stations_Find(st, picks[i].sta);
        pick_sidx[i] = s;
        if (s >= 0 && s < st->n) {
            pick_next[i] = pick_head[s];
            pick_head[s] = i;
        } else {
            pick_next[i] = -1;
        }
    }

    memset(&job, 0, sizeof(job));
    job.g = g; job.st = st; job.picks = picks; job.npick = npick;
    job.pick_sidx = pick_sidx; job.pick_head = pick_head; job.pick_next = pick_next;
    job.tt = tt; job.cfg = &cfg_eff;

    for (i = 0; i < npick; i++)
        if (picks[i].t_epoch < min_t) min_t = picks[i].t_epoch;

    job.t0_lo = min_t - T0_MAX_TT;
    job.nt0 = (int)((T0_MAX_TT + 5.0) / T0_STEP);
    if (job.nt0 < 8) job.nt0 = 8;
    if (job.nt0 > T0_MAX_BINS) job.nt0 = T0_MAX_BINS;

    total = g->n_nodes;
    job.stack = (double *)calloc((size_t)total, sizeof(double));
    job.t0_of_node = (double *)calloc((size_t)total, sizeof(double));
    if (!job.stack || !job.t0_of_node) {
        free(job.stack); free(job.t0_of_node);
        free(pick_sidx); free(pick_head); free(pick_next);
        return 0;
    }

    nthreads_eff = (nthreads > 0) ? nthreads : 1;
    if (nthreads_eff > total) nthreads_eff = total;
    if (nthreads_eff < 1) nthreads_eff = 1;
    job.nthreads = nthreads_eff;

    slices = (BProjSlice *)calloc((size_t)nthreads_eff, sizeof(BProjSlice));
    tids   = (pthread_t *)calloc((size_t)nthreads_eff, sizeof(pthread_t));
    if (!slices || !tids) {
        free(slices); free(tids); free(job.stack); free(job.t0_of_node);
        free(pick_sidx); free(pick_head); free(pick_next);
        return 0;
    }

    {
        int chunk = (total + nthreads_eff - 1) / nthreads_eff;
        for (t = 0; t < nthreads_eff; t++) {
            slices[t].job = &job;
            slices[t].i0 = t * chunk;
            slices[t].i1 = slices[t].i0 + chunk;
            if (slices[t].i1 > total) slices[t].i1 = total;
        }
    }

    if (nthreads_eff == 1) {
        bproj_slice(&slices[0]);
    } else {
        for (t = 0; t < nthreads_eff; t++)
            pthread_create(&tids[t], NULL, slice_thread, &slices[t]);
        for (t = 0; t < nthreads_eff; t++)
            pthread_join(tids[t], NULL);
    }

    n_nuc = find_nucleations(g, job.stack, job.t0_of_node, &cfg_eff, nuc,
                             max_nuc);

    free(slices);
    free(tids);
    free(job.stack);
    free(job.t0_of_node);
    free(pick_sidx);
    free(pick_head);
    free(pick_next);
    return n_nuc;
}

int BackProject(const Grid *g, const StationList *st, const Pick *picks,
                int npick, TTModel *tt, const CSLocParams *cfg,
                HypoCandidate *cand, int max_cand, int nthreads)
{
    HypoCandidate *nuc;
    int            n_nuc, n_events = 0;

    if (!g || !cand || max_cand <= 0) return 0;

    nuc = (HypoCandidate *)calloc((size_t)CSLOC_MAX_NUC, sizeof(HypoCandidate));
    if (!nuc) return 0;

    n_nuc = BackProject_Nucleations(g, st, picks, npick, tt, cfg, nuc,
                                    CSLOC_MAX_NUC, nthreads);
    if (n_nuc > 0)
        n_events = AssembleCandidates(nuc, n_nuc, st, picks, npick, tt, cfg,
                                      cand, max_cand);

    free(nuc);
    return n_events;
}

/* Wrapper publico de conversion (evita warning de funcion no usada). */
double BProj_GeoToGeoc(double lat_deg)
{
    double l = lat_deg * RAD;
    return atan(0.993277 * tan(l)) / RAD;
}

/*
 * Diagnostico: calcula el stack de un nodo y devuelve el numero/identidad de
 * las fases que caen dentro de la tolerancia alrededor del mejor t0.
 * Tambien permite conocer el residual de cada pick en ese nodo.
 */
int BProj_NodeStack(const StationList *st, const Pick *picks, int npick,
                    TTModel *tt, const CSLocParams *cfg,
                    double lat_geoc, double lon, double depth,
                    double *out_t0, int *out_used, double *out_resid)
{
    int    p, nph = 0;
    double t0_lo = 1e18, t0_hi = -1e18;
    int    nbins, best_bin = -1, best_count = 0;
    double tol = (cfg->T0ToleranceSec > 0.0) ? cfg->T0ToleranceSec : 2.0;
    int   *hist;
    double step = 0.1;
    int    b;

    /* Rango de t0 implicito. */
    for (p = 0; p < npick; p++) {
        double tpred;
        if (predict_tt(tt, st, &picks[p], lat_geoc, lon, depth, &tpred, NULL) != 0)
            continue;
        {
            double implied = picks[p].t_epoch - tpred;
            if (implied < t0_lo) t0_lo = implied;
            if (implied > t0_hi) t0_hi = implied;
        }
    }
    if (t0_hi < t0_lo) return 0;

    nbins = (int)floor((t0_hi - t0_lo) / step) + 2;
    if (nbins > 20000) nbins = 20000;
    hist = (int *)calloc((size_t)nbins, sizeof(int));
    if (!hist) return 0;

    for (p = 0; p < npick; p++) {
        double tpred, implied;
        int    bin;
        if (predict_tt(tt, st, &picks[p], lat_geoc, lon, depth, &tpred, NULL) != 0)
            continue;
        implied = picks[p].t_epoch - tpred;
        bin = (int)floor((implied - t0_lo) / step + 0.5);
        if (bin < 0 || bin >= nbins) continue;
        hist[bin]++;
    }
    /* Pico con ventana de tolerancia. */
    {
        int half = (int)ceil(tol / step);
        if (half < 0) half = 0;
        for (b = 0; b < nbins; b++) {
            int lo = b - half; if (lo < 0) lo = 0;
            int hi = b + half + 1; if (hi > nbins) hi = nbins;
            {
                int c2 = 0, q;
                for (q = lo; q < hi; q++) c2 += hist[q];
                if (c2 > best_count) { best_count = c2; best_bin = b; }
            }
        }
    }
    if (best_bin < 0) { free(hist); return 0; }
    if (out_t0) *out_t0 = t0_lo + (double)best_bin * step;

    /* Fases dentro de la tolerancia y sus residuales. */
    for (p = 0; p < npick; p++) {
        double tpred, implied, resid;
        if (predict_tt(tt, st, &picks[p], lat_geoc, lon, depth, &tpred, NULL) != 0) {
            if (out_used) out_used[p] = 0;
            continue;
        }
        implied = picks[p].t_epoch - tpred;
        resid = implied - (t0_lo + (double)best_bin * step);
        if (out_resid) out_resid[p] = resid;
        if (fabs(resid) <= tol) {
            if (out_used) out_used[p] = 1;
            nph++;
        } else if (out_used) out_used[p] = 0;
    }
    free(hist);
    return nph;
}
