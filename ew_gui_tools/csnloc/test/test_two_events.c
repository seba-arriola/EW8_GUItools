/*
 * CA3: dos eventos simultaneos en zonas distintas deben reportarse separados.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../csnloc.h"

#define KM_PER_DEG 111.195

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static double geoc(double lat)
{
    double l = lat * 0.017453292519943;
    return atan(0.993277 * tan(l)) / 0.017453292519943;
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
    g->sta_max_dist_km = 0.0;
    g->num_stations_per_node = 0;
    return Grid_BuildAxes(g);
}

int main(void)
{
    StationList st;
    TTModel tt;
    CSLocParams cfg;
    Grid grid;
    Pick picks[CSLOC_MAX_PICKS];
    HypoCandidate cand[CSLOC_MAX_EVENTS];
    double t0 = 1.8e9;
    double ev_lat[2] = { -23.5, -19.0 };
    double ev_lon[2] = { -70.0, -68.0 };
    double ev_dep[2] = { 30.0, 60.0 };
    int npick = 0, nev, e, i;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n"); return 1;
    }

    for (e = 0; e < 2; e++) {
        for (i = 0; i < st.n; i++) {
            double delta = TT_GreatCircleDeg(geoc(ev_lat[e]), ev_lon[e],
                                             st.lat_geoc_sta[i], st.st[i].lon);
            double tp;
            if (TTModel_Predict(&tt, delta, ev_dep[e], CSLOC_PHASE_P,
                                &tp, NULL) != 0)
                continue;
            memset(&picks[npick], 0, sizeof(Pick));
            strncpy(picks[npick].sta, st.st[i].sta, sizeof(picks[npick].sta)-1);
            strcpy(picks[npick].net, "C"); strcpy(picks[npick].chan, "BHZ");
            strcpy(picks[npick].loc, "--");
            picks[npick].phase = CSLOC_PHASE_P;
            strcpy(picks[npick].phase_name, "P");
            picks[npick].t_epoch = t0 + tp;
            picks[npick].weight = 1;
            npick++;
        }
    }
    CHECK(npick >= 10, "picks de dos eventos generados");

    memset(&cfg, 0, sizeof(cfg));
    cfg.DBSCAN_Eps = 40.0;
    cfg.DBSCAN_MinPts = 1;
    cfg.BackProjThreshold = 0.0;
    cfg.MinPhasesPerEvent = 3;
    cfg.MaxRMS = 3.0;
    cfg.PhaseWeightP = 1.0; cfg.PhaseWeightS = 0.8;

    if (build_grid(&grid, &st, 15.0, 120.0, 15.0) != 0) {
        printf("FAIL: build_grid\n"); return 1;
    }
    if (Grid_PrecomputeStations(&grid, &st) != 0) {
        printf("FAIL: Grid_PrecomputeStations\n"); return 1;
    }

    nev = BackProject(&grid, &st, picks, npick, &tt, &cfg,
                      cand, CSLOC_MAX_EVENTS, 4);
    printf("eventos detectados: %d\n", nev);
    for (i = 0; i < nev; i++) {
        printf("  ev%d lat=%.3f lon=%.3f z=%.1f nph=%d\n",
               i, cand[i].lat, cand[i].lon, cand[i].depth_km, cand[i].nphases);
    }
    CHECK(nev == 2, "CA3: se detectan 2 eventos independientes");

    if (nev == 2) {
        /* Cada evento detectado debe estar cerca de uno de los verdaderos. */
        int matched[2] = {0, 0};
        int c;
        for (c = 0; c < 2; c++) {
            double best = 1e9; int best_e = -1;
            for (e = 0; e < 2; e++) {
                double d = TT_GreatCircleDeg(geoc(ev_lat[e]), ev_lon[e],
                                             cand[c].lat, cand[c].lon) * KM_PER_DEG;
                if (d < best) { best = d; best_e = e; }
            }
            printf("  ev%d -> verdadero %d (%.1f km)\n", c, best_e, best);
            if (best <= 60.0) matched[best_e] = 1;
        }
        CHECK(matched[0] && matched[1], "CA3: ambos eventos correctamente asociados");
    }

    TTModel_Free(&tt);
    Grid_Free(&grid);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_two_events\n");
    return 0;
}
