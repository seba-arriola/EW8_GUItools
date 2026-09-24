/*
 * CA6: dos grillas anidadas (gruesa + fina) que cubren el mismo sismo deben
 * producir UN solo evento tras unir nucleaciones y ensamblar.
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

static int build_grid(Grid *g, int level, const StationList *st, double node_km)
{
    double margin = 2.0;
    int    j;
    memset(g, 0, sizeof(*g));
    g->level = level;
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
    g->depth_min = 0.0; g->depth_max = 120.0; g->depth_step = 10.0;
    g->sta_max_dist_km = 0.0;
    g->num_stations_per_node = 0;
    return Grid_BuildAxes(g);
}

static void make_picks(const StationList *st, TTModel *tt,
                       double tr_lat_g, double tr_lon, double tr_depth,
                       double t0, Pick *picks, int *npick)
{
    int i, n = 0;
    for (i = 0; i < st->n; i++) {
        double delta = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                                         st->lat_geoc_sta[i], st->st[i].lon);
        double tp;
        if (TTModel_Predict(tt, delta, tr_depth, CSLOC_PHASE_P, &tp, NULL) != 0)
            continue;
        memset(&picks[n], 0, sizeof(Pick));
        strncpy(picks[n].sta, st->st[i].sta, sizeof(picks[n].sta) - 1);
        strcpy(picks[n].net, "C"); strcpy(picks[n].chan, "BHZ");
        strcpy(picks[n].loc, "--");
        picks[n].phase = CSLOC_PHASE_P;
        strcpy(picks[n].phase_name, "P");
        picks[n].t_epoch = t0 + tp;
        picks[n].weight = 1;
        n++;
    }
    *npick = n;
}

int main(void)
{
    StationList   st;
    TTModel       tt;
    CSLocParams   cfg;
    Grid          gcoarse, gfine;
    Pick          picks[CSLOC_MAX_PICKS];
    HypoCandidate *nuc, cand[CSLOC_MAX_EVENTS];
    double        tr_lat_g, t0 = 1.8e9;
    int           npick = 0, n1, n2, nev;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n"); return 1;
    }

    tr_lat_g = geoc(-23.5);
    make_picks(&st, &tt, tr_lat_g, -70.0, 30.0, t0, picks, &npick);

    memset(&cfg, 0, sizeof(cfg));
    cfg.DBSCAN_Eps = 60.0;
    cfg.MinPhasesPerEvent = 3;
    cfg.MaxRMS = 2.0;
    cfg.PhaseWeightP = 1.0; cfg.PhaseWeightS = 0.8;

    if (build_grid(&gcoarse, GRID_LEVEL_REGIONAL, &st, 25.0) != 0 ||
        build_grid(&gfine,   GRID_LEVEL_LOCAL,    &st, 10.0) != 0) {
        printf("FAIL: build_grid\n"); return 1;
    }
    Grid_PrecomputeStations(&gcoarse, &st);
    Grid_PrecomputeStations(&gfine, &st);

    nuc = (HypoCandidate *)calloc(CSLOC_MAX_NUC, sizeof(HypoCandidate));
    if (!nuc) { printf("FAIL: calloc\n"); return 1; }

    n1 = BackProject_Nucleations(&gcoarse, &st, picks, npick, &tt, &cfg,
                                 nuc, CSLOC_MAX_NUC, 1);
    n2 = BackProject_Nucleations(&gfine, &st, picks, npick, &tt, &cfg,
                                 nuc + n1, CSLOC_MAX_NUC - n1, 1);
    printf("nucleaciones: gruesa=%d fina=%d\n", n1, n2);
    CHECK(n1 > 0, "CA6: la grilla gruesa nuclea");
    CHECK(n2 > 0, "CA6: la grilla fina nuclea");

    nev = AssembleCandidates(nuc, n1 + n2, &st, picks, npick, &tt, &cfg,
                             cand, CSLOC_MAX_EVENTS);
    printf("eventos tras unir nucleaciones: %d\n", nev);
    CHECK(nev == 1, "CA6: un solo evento pese a dos grillas");

    free(nuc);
    TTModel_Free(&tt);
    Grid_Free(&gcoarse);
    Grid_Free(&gfine);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_nested_dedup\n");
    return 0;
}
