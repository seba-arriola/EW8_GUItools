/*
 * CA4: el camino con precómputo CSR (estaciones asociadas por nodo) debe dar
 * exactamente el mismo resultado que el camino sin CSR (todos los picks).
 * Con StaMaxDistKm=0 y NumStationsPerNode=0 el CSR es denso, asi que la
 * unica diferencia es el orden de recorrido, que no afecta (sumas enteras).
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

static int build_grid(Grid *g, const StationList *st, double node_km)
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
    g->depth_min = 0.0; g->depth_max = 120.0; g->depth_step = 10.0;
    g->sta_max_dist_km = 0.0;      /* denso: todas las estaciones */
    g->num_stations_per_node = 0;  /* sin tope                    */
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
    Grid          ga, gb;
    Pick          picks[CSLOC_MAX_PICKS];
    HypoCandidate ca[CSLOC_MAX_EVENTS], cb[CSLOC_MAX_EVENTS];
    double        tr_lat_g, t0 = 1.8e9;
    int           npick = 0, na, nb, i;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n"); return 1;
    }

    tr_lat_g = geoc(-23.5);
    make_picks(&st, &tt, tr_lat_g, -70.0, 30.0, t0, picks, &npick);
    CHECK(npick >= 5, "picks sinteticos generados");

    memset(&cfg, 0, sizeof(cfg));
    cfg.DBSCAN_Eps = 40.0;
    cfg.MinPhasesPerEvent = 3;
    cfg.MaxRMS = 2.0;
    cfg.PhaseWeightP = 1.0; cfg.PhaseWeightS = 0.8;

    if (build_grid(&ga, &st, 15.0) != 0 || build_grid(&gb, &st, 15.0) != 0) {
        printf("FAIL: build_grid\n"); return 1;
    }
    if (Grid_PrecomputeStations(&ga, &st) != 0) {
        printf("FAIL: Grid_PrecomputeStations\n"); return 1;
    }
    /* gb queda SIN precómputo: ejercita el camino fallback. */

    na = BackProject(&ga, &st, picks, npick, &tt, &cfg, ca, CSLOC_MAX_EVENTS, 1);
    nb = BackProject(&gb, &st, picks, npick, &tt, &cfg, cb, CSLOC_MAX_EVENTS, 1);
    CHECK(na >= 1 && nb == na, "CA4: mismo numero de eventos");

    if (na >= 1 && nb == na) {
        for (i = 0; i < na; i++) {
            CHECK(fabs(ca[i].lat - cb[i].lat) < 1e-9 &&
                  fabs(ca[i].lon - cb[i].lon) < 1e-9 &&
                  fabs(ca[i].depth_km - cb[i].depth_km) < 1e-9 &&
                  fabs(ca[i].t0 - cb[i].t0) < 1e-9 &&
                  ca[i].nphases == cb[i].nphases,
                  "CA4: candidatos identicos con y sin CSR");
        }
    }

    TTModel_Free(&tt);
    Grid_Free(&ga);
    Grid_Free(&gb);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_precompute_equiv\n");
    return 0;
}
