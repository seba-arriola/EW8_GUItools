/*
 * C16: re-nucleo con back-projection cuando el cambio de fases es grande.
 *
 * Crea un evento con 3 fases, luego incorpora >= RenucleateMinNewPhases fases
 * nuevas. El re-nucleo debe re-localizar el evento (no quedar en el minimo
 * local del hipocentro inicial).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../csnloc.h"

#define KM_PER_DEG 111.195

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static double geoc(double lat_geo)
{
    double l = lat_geo * 0.017453292519943;
    return atan(0.993277 * tan(l)) / 0.017453292519943;
}

static Pick mk_pick(const StationList *st, TTModel *tt, int i,
                    double tr_lat_g, double tr_lon, double tr_depth, double t0)
{
    Pick p; double delta, tp;
    memset(&p, 0, sizeof(p));
    strncpy(p.sta, st->st[i].sta, sizeof(p.sta)-1);
    strcpy(p.net, "C"); strcpy(p.chan, "BHZ"); strcpy(p.loc, "--");
    p.phase = CSLOC_PHASE_P; strcpy(p.phase_name, "P"); p.weight = 1;
    delta = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                              st->lat_geoc_sta[i], st->st[i].lon);
    if (TTModel_Predict(tt, delta, tr_depth, CSLOC_PHASE_P, &tp, NULL) != 0)
        p.t_epoch = t0;
    else
        p.t_epoch = t0 + tp;
    return p;
}

static int build_grid(Grid *g, const StationList *st, double node_km,
                      double dmax, double dstep)
{
    double margin = 2.0;
    int    j;
    memset(g, 0, sizeof(*g));
    g->lat_min = g->lat_max = st->st[0].lat;
    g->lon_min = g->lon_max = st->st[0].lon;
    for (j = 1; j < st->n; j++) {
        if (st->st[j].lat < g->lat_min) g->lat_min = st->st[j].lat;
        if (st->st[j].lat > g->lat_max) g->lat_max = st->st[j].lat;
        if (st->st[j].lon < g->lon_min) g->lon_min = st->st[j].lon;
        if (st->st[j].lon > g->lon_max) g->lon_max = st->st[j].lon;
    }
    g->lat_min -= margin; g->lat_max += margin;
    g->lon_min -= margin; g->lon_max += margin;
    g->node_km = node_km;
    g->depth_min = 0.0; g->depth_max = dmax; g->depth_step = dstep;
    g->sta_max_dist_km = 0.0; g->num_stations_per_node = 0;
    g->level = GRID_LEVEL_LOCAL;
    return Grid_BuildAxes(g);
}

int main(void)
{
    StationList st;
    TTModel tt;
    CSLocParams cfg;
    GridSet grids;
    EventRegistry reg;
    Pick window[CSLOC_MAX_PHASES];
    unsigned long id1 = 0, id2 = 0;
    unsigned int v1 = 0, v2 = 0;
    double tr_lat_geo = -23.5, tr_lon = -70.0, tr_depth = 30.0;
    double tr_lat_g, t0 = 1.8e9;
    int nwin = 0, r;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n"); return 1;
    }
    tr_lat_g = geoc(tr_lat_geo);

    memset(&cfg, 0, sizeof(cfg));
    cfg.EventDedupSec = 30.0; cfg.EventDedupKm = 100.0;
    cfg.MaxRMSDegrade = 0.10; cfg.MaxGapDegradeDeg = 10.0;
    cfg.PhaseAssocTolSec = 2.0; cfg.PhaseAssocTolSecS = 4.0;
    cfg.PhaseResidualMaxSec = 3.0; cfg.PhaseResidualMaxSecS = 5.0;
    cfg.RefineIterations = 4; cfg.RefineNodeKm = 5.0;
    cfg.T0ToleranceSec = 2.0;
    cfg.PhaseWeightP = 1.0; cfg.PhaseWeightS = 0.8;
    cfg.MinPhasesPerEvent = 3; cfg.MaxRMS = 2.0;
    cfg.DBSCAN_Eps = 40.0; cfg.DBSCAN_MinPts = 1;
    cfg.BackProjThreshold = 0.0;
    cfg.NumThreads = 1;
    cfg.RenucleateMinNewPhases = 3;

    memset(&grids, 0, sizeof(grids));
    if (build_grid(&grids.g[0], &st, 15.0, 120.0, 10.0) != 0) {
        printf("FAIL: build_grid\n"); return 1;
    }
    if (Grid_PrecomputeStations(&grids.g[0], &st) != 0) {
        printf("FAIL: Grid_PrecomputeStations\n"); return 1;
    }
    grids.n = 1;

    EventRegistry_Init(&reg);

    /* Evento inicial con 3 fases. */
    window[nwin++] = mk_pick(&st, &tt, 0, tr_lat_g, tr_lon, tr_depth, t0);
    window[nwin++] = mk_pick(&st, &tt, 1, tr_lat_g, tr_lon, tr_depth, t0);
    window[nwin++] = mk_pick(&st, &tt, 2, tr_lat_g, tr_lon, tr_depth, t0);
    {
        HypoCandidate cc; int i;
        memset(&cc, 0, sizeof(cc));
        cc.t0 = t0; cc.lat = tr_lat_g; cc.lon = tr_lon; cc.depth_km = tr_depth;
        cc.nphases = 3; cc.rms_sec = 0.5; cc.gap_deg = 120.0; cc.dmin_km = 50.0;
        cc.score = 1.0; cc.grid_level = GRID_LEVEL_LOCAL;
        for (i = 0; i < 3; i++) { cc.phase_idx[i] = i; cc.residual[i] = 0.1; }
        r = EventRegistry_Upsert(&reg, &cc, window, nwin, &cfg, &st, &tt,
                                 &grids, 1000UL, &id1, &v1);
    }
    CHECK(r == 1 && v1 == 1, "evento inicial creado");

    /* Incorporar 3 fases nuevas -> dispara re-nucleo. */
    window[nwin++] = mk_pick(&st, &tt, 3, tr_lat_g, tr_lon, tr_depth, t0);
    window[nwin++] = mk_pick(&st, &tt, 4, tr_lat_g, tr_lon, tr_depth, t0);
    window[nwin++] = mk_pick(&st, &tt, 5, tr_lat_g, tr_lon, tr_depth, t0);
    {
        HypoCandidate cc; int i;
        memset(&cc, 0, sizeof(cc));
        cc.t0 = t0; cc.lat = tr_lat_g; cc.lon = tr_lon; cc.depth_km = tr_depth;
        cc.nphases = 6; cc.rms_sec = 0.4; cc.gap_deg = 110.0; cc.dmin_km = 50.0;
        cc.score = 1.0; cc.grid_level = GRID_LEVEL_LOCAL;
        for (i = 0; i < 6; i++) { cc.phase_idx[i] = i; cc.residual[i] = 0.1; }
        r = EventRegistry_Upsert(&reg, &cc, window, nwin, &cfg, &st, &tt,
                                 &grids, 1000UL, &id2, &v2);
    }
    CHECK(r == 1 && v2 == 2, "re-nucleo emite version 2");
    CHECK(id2 == id1, "mismo id tras re-nucleo");
    CHECK(reg.ev[0].nphases_stored == 6, "6 fases acumuladas");

    /* El hipocentro re-nucleado debe estar cerca del verdadero. */
    {
        double dist = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                                        reg.ev[0].lat, reg.ev[0].lon) * KM_PER_DEG;
        printf("re-nucleado lat=%.3f lon=%.3f z=%.1f (dist=%.1f km)\n",
               reg.ev[0].lat, reg.ev[0].lon, reg.ev[0].depth_km, dist);
        CHECK(dist <= 50.0, "re-nucleo cerca del hipocentro verdadero");
    }

    TTModel_Free(&tt);
    Grid_Free(&grids.g[0]);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_renucleate\n");
    return 0;
}
