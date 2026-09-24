/******************************************************************************
 * ttmodel.c                                                                  *
 *                                                                            *
 * Wrapper de las rutinas tau de EarthWorm/ATWC (taulib.c).                   *
 *                                                                            *
 * taulib mantiene estado global y depset/trtm no son reentrantes, por lo que *
 * la inicializacion se hace UNA vez en el hilo principal: se recorre una      *
 * malla de profundidades y se calcula la tabla de tiempos de viaje P y S.     *
 * A partir de ahi TTModel_Predict solo interpola (lectura pura) y es seguro  *
 * llamarla desde multiples hilos.                                            *
 *                                                                            *
 * Depende de: taulib_csnloc.c e iasplib.h (vendorizados en este directorio). *
 ******************************************************************************/
#include "csnloc.h"

#include "iasplib.h"

#ifndef RAD
#define RAD 0.017453292519943
#endif

/* Profundidades de la malla (km). Cubre hasta 700 km; mas alla se extrapola. */
static const double g_depths[] = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
    60, 70, 80, 90, 100, 120, 140, 160, 180, 200,
    250, 300, 400, 500, 600, 700
};
#define N_DEPTHS ((int)(sizeof(g_depths)/sizeof(g_depths[0])))

/* Distancia geocentrica entre dos puntos (grados). */
double TT_GreatCircleDeg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * RAD, p2 = lat2 * RAD;
    double dl = (lon2 - lon1) * RAD;
    double c = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
    if (c > 1.0)  c = 1.0;
    if (c < -1.0) c = -1.0;
    return acos(c) / RAD;
}

/* Azimuth del punto 1 al punto 2 (grados, 0=N, 90=E). */
double TT_AzimuthDeg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * RAD, p2 = lat2 * RAD;
    double dl = (lon2 - lon1) * RAD;
    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double az = atan2(y, x) / RAD;
    if (az < 0.0) az += 360.0;
    return az;
}

/* ------------------------------------------------------------------------- */
/* Calcula tt para un nodo de profundidad usando taulib (estado global).      */
/* ------------------------------------------------------------------------- */
/*
 * Calcula la fila de tiempos de viaje para una profundidad.
 *
 * Se replica EXACTAMENTE la secuencia probada de GetPhaseTime
 * (libsrc/geotools.c:410-414):
 *     tabin() una vez al inicio;
 *     brnset(1, {"basic"}, {0,0,1}) por cada profundidad;
 *     depset(depth, ...)   por cada profundidad;
 *     trtm(delta, ...)     por cada distancia.
 * De cada resultado se toma el primer arribo de la familia P y de la familia S.
 */
static int fill_depth_row(TTModel *m, int idep, FILE *err, FILE *tbl)
{
    double dep = g_depths[idep];
    double usrc[2];
    char   ph_out[CSLOC_TT_MAX][PHASE_LENGTH];
    double tt[CSLOC_TT_MAX], dtdd[CSLOC_TT_MAX];
    double dtdh[CSLOC_TT_MAX], dddp[CSLOC_TT_MAX];
    char   phlst[10][PHASE_LENGTH];
    int    iprint[3] = {0, 0, 1};
    int    n, k, j, n_rows = 0;

    strcpy(phlst[0], "basic");
    brnset(1, phlst, iprint, err);

    /* La correccion de profundidad de taulib lee integrales del .tbl. */
    depset(dep, usrc, err, tbl);

    for (k = 0; k < m->nk; k++) {
        double delta = k * m->dk_step;
        double best_p = -1.0, best_s = -1.0;

        trtm(delta, &n, tt, dtdd, dtdh, dddp, ph_out, err);
        if (n > CSLOC_TT_MAX) n = CSLOC_TT_MAX;   /* nunca deberia pasar   */
        for (j = 0; j < n; j++) {
            if (tt[j] <= 0.0) continue;
            /* Familia P: P, Pn, Pg, Pdiff, PKP... ; S: S, Sn, Sg, SKS... */
            if (ph_out[j][0] == 'P' || ph_out[j][0] == 'p') {
                if (best_p < 0.0 || tt[j] < best_p) best_p = tt[j];
            } else if (ph_out[j][0] == 'S' || ph_out[j][0] == 's') {
                if (best_s < 0.0 || tt[j] < best_s) best_s = tt[j];
            }
        }
        if (best_p >= 0.0) {
            m->tt[CSLOC_PHASE_P][idep * m->nk + k] = best_p;
            n_rows++;
        }
        if (best_s >= 0.0)
            m->tt[CSLOC_PHASE_S][idep * m->nk + k] = best_s;
    }
    return (n_rows > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
/* Inicializa la tabla. tau_dir es el cwd donde estan <model>.tbl/.hed.       */
/* ------------------------------------------------------------------------- */
int TTModel_Init(TTModel *m, const char *tau_dir, const char *model,
                 double dmin, double dmax, double dstep)
{
    FILE  *err, *tbl = NULL;
    int    i, ret = 0;
    char   modnam[64];

    if (!m || !model) return -1;
    memset(m, 0, sizeof(*m));
    strncpy(m->model, model, sizeof(m->model) - 1);

    if (dstep <= 0.0) dstep = 0.5;
    if (dmax <= dmin) dmax = 180.0;
    m->dk_step = dstep;
    m->k_max   = dmax;
    m->nk      = (int)floor(dmax / dstep) + 2;   /* +1 margen de interpolacion */
    m->nd      = N_DEPTHS;

    m->depth = (double *)malloc(sizeof(double) * m->nd);
    if (!m->depth) return -1;
    for (i = 0; i < m->nd; i++) m->depth[i] = g_depths[i];
    for (i = 0; i < CSLOC_NPHASE; i++) {
        m->tt[i] = (double *)calloc((size_t)(m->nd * m->nk), sizeof(double));
        if (!m->tt[i]) { TTModel_Free(m); return -1; }
    }

    /* taulib abre <model>.hed/.tbl desde el cwd. */
    err = fopen("/dev/null", "w");
    if (!err) err = stderr;

    strncpy(modnam, model, sizeof(modnam) - 1);
    modnam[sizeof(modnam) - 1] = '\0';

    tabin(&tbl, modnam, err);              /* carga .hed + .tbl            */

    /* Por cada profundidad se calculan P y S por separado (ver
       fill_family_row: trtm no soporta las dos familias juntas). */
    for (i = 0; i < m->nd; i++) {
        if (fill_depth_row(m, i, err, tbl) != 0)
            ret = -1;                      /* fila incompleta, se tolera   */
    }

    if (tbl) fclose(tbl);
    fclose(err);
    (void)tau_dir;
    return ret;
}

void TTModel_Free(TTModel *m)
{
    int i;
    if (!m) return;
    free(m->depth);
    m->depth = NULL;
    for (i = 0; i < CSLOC_NPHASE; i++) {
        free(m->tt[i]);
        m->tt[i] = NULL;
    }
    m->nd = m->nk = 0;
}

/* Interpola billinealmente en (profundidad, distancia). Thread-safe. */
int TTModel_Predict(TTModel *m, double delta_deg, double depth_km,
                    int phase, double *t_sec, double *dtdd)
{
    double ki, fd, fk;
    int    d0, d1, k0, k1;
    double v00, v01, v10, v11, t0, t1, d0v, d1v;

    if (!m || !m->tt[phase] || m->nd < 2 || m->nk < 2) return -1;
    if (phase < 0 || phase >= CSLOC_NPHASE) return -1;

    if (delta_deg < 0.0) delta_deg = -delta_deg;
    ki = delta_deg / m->dk_step;
    if (ki > (double)(m->nk - 2)) ki = (double)(m->nk - 2);

    if (depth_km < m->depth[0]) depth_km = m->depth[0];
    if (depth_km > m->depth[m->nd - 1]) depth_km = m->depth[m->nd - 1];

    d0 = 0;
    while (d0 < m->nd - 2 && m->depth[d0 + 1] <= depth_km) d0++;
    d1 = d0 + 1;

    k0 = (int)floor(ki);
    if (k0 > m->nk - 2) k0 = m->nk - 2;
    k1 = k0 + 1;

    fd = (depth_km - m->depth[d0]) / (m->depth[d1] - m->depth[d0]);
    fk = ki - (double)k0;
    if (fk < 0.0) fk = 0.0;

    v00 = m->tt[phase][d0 * m->nk + k0];
    v01 = m->tt[phase][d0 * m->nk + k1];
    v10 = m->tt[phase][d1 * m->nk + k0];
    v11 = m->tt[phase][d1 * m->nk + k1];

    t0  = v00 + (v01 - v00) * fk;          /* interpolacion en distancia   */
    t1  = v10 + (v11 - v10) * fk;
    d0v = t0;
    d1v = t1;
    *t_sec = d0v + (d1v - d0v) * fd;

    /* Derivada dT/dDelta por diferencias finitas (para residuales/refinado). */
    if (dtdd) {
        double dk = m->dk_step;
        double ka = ki - 1.0, kb = ki + 1.0;
        double ta, tb, va0, vb0, va1, vb1;
        if (ka < 0.0) ka = 0.0;
        if (kb > (double)(m->nk - 1)) kb = (double)(m->nk - 1);
        va0 = m->tt[phase][d0 * m->nk + (int)floor(ka)];
        vb0 = m->tt[phase][d0 * m->nk + (int)floor(kb)];
        va1 = m->tt[phase][d1 * m->nk + (int)floor(ka)];
        vb1 = m->tt[phase][d1 * m->nk + (int)floor(kb)];
        ta = (va0 + (va1 - va0) * fd);
        tb = (vb0 + (vb1 - vb0) * fd);
        *dtdd = (tb - ta) / ((kb - ka) * dk);
    }
    return 0;
}
