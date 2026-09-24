/*
 * CA2/CA3/CA6: localizacion sintetica end-to-end.
 *
 * Genera picks a partir de un hipocentro conocido usando la MISMA tabla TT,
 * corre BackProject + RefineHypo, y verifica que recupera el evento.
 * Tambien corre con 1 y 4 hilos y verifica determinismo.
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

/* Construye una grilla con bbox derivado de las estaciones + 2 grados. */
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
    g->sta_max_dist_km = 0.0;        /* todas las estaciones */
    g->num_stations_per_node = 0;    /* sin tope             */
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
        double tp, ts;
        if (TTModel_Predict(tt, delta, tr_depth, CSLOC_PHASE_P, &tp, NULL) != 0)
            continue;
        memset(&picks[n], 0, sizeof(Pick));
        strncpy(picks[n].sta, st->st[i].sta, sizeof(picks[n].sta)-1);
        strcpy(picks[n].net, "C"); strcpy(picks[n].chan, "BHZ");
        strcpy(picks[n].loc, "--");
        picks[n].phase = CSLOC_PHASE_P;
        strcpy(picks[n].phase_name, "P");
        picks[n].t_epoch = t0 + tp;
        picks[n].weight = 1;
        n++;
        /* Fase S para las estaciones cercanas (mejora la profundidad). */
        if (delta < 10.0 &&
            TTModel_Predict(tt, delta, tr_depth, CSLOC_PHASE_S, &ts, NULL) == 0) {
            memset(&picks[n], 0, sizeof(Pick));
            strncpy(picks[n].sta, st->st[i].sta, sizeof(picks[n].sta)-1);
            strcpy(picks[n].net, "C"); strcpy(picks[n].chan, "BHZ");
            strcpy(picks[n].loc, "--");
            picks[n].phase = CSLOC_PHASE_S;
            strcpy(picks[n].phase_name, "S");
            picks[n].t_epoch = t0 + ts;
            picks[n].weight = 1;
            n++;
        }
    }
    *npick = n;
}

int main(void)
{
    StationList st;
    TTModel tt;
    CSLocParams cfg;
    Grid grid;
    Pick picks[CSLOC_MAX_PICKS];
    HypoCandidate cand[CSLOC_MAX_EVENTS];
    double tr_lat_geo = -23.5, tr_lon = -70.0, tr_depth = 30.0;
    double tr_lat_g, t0 = 1.8e9;
    int npick = 0, nev, nev4;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n"); return 1;
    }

    tr_lat_g = geoc(tr_lat_geo);
    make_picks(&st, &tt, tr_lat_g, tr_lon, tr_depth, t0, picks, &npick);
    CHECK(npick >= 5, "se generaron >=5 picks sinteticos");

    memset(&cfg, 0, sizeof(cfg));
    cfg.DBSCAN_Eps = 40.0;
    cfg.DBSCAN_MinPts = 1;
    cfg.BackProjThreshold = 0.0;
    cfg.MinPhasesPerEvent = 3;
    cfg.MaxRMS = 2.0;
    cfg.PhaseWeightP = 1.0; cfg.PhaseWeightS = 0.8;
    cfg.RefineIterations = 4;
    cfg.RefineNodeKm = 5.0;

    if (build_grid(&grid, &st, 15.0, 120.0, 10.0) != 0) {
        printf("FAIL: build_grid\n"); return 1;
    }
    if (Grid_PrecomputeStations(&grid, &st) != 0) {
        printf("FAIL: Grid_PrecomputeStations\n"); return 1;
    }
    printf("grilla %dx%dx%d\n", grid.nx, grid.ny, grid.nz);

    nev = BackProject(&grid, &st, picks, npick, &tt, &cfg,
                      cand, CSLOC_MAX_EVENTS, 1);
    CHECK(nev >= 1, "back-projection genera >=1 evento");

    if (nev >= 1) {
        int i;
        for (i = 0; i < nev; i++) RefineHypo(&cand[i], &st, picks, &tt, &cfg);
        {
            double dist = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                                            cand[0].lat, cand[0].lon) * KM_PER_DEG;
            double dt0 = fabs(cand[0].t0 - t0);
            double dz = fabs(cand[0].depth_km - tr_depth);
            printf("localizado lat=%.3f lon=%.3f z=%.1f t0=%.1f (nph=%d rms=%.2f)\n",
                   cand[0].lat, cand[0].lon, cand[0].depth_km, cand[0].t0,
                   cand[0].nphases, cand[0].rms_sec);
            printf("error: dist=%.1f km dz=%.1f km dt0=%.1f s\n", dist, dz, dt0);
            CHECK(dist <= 30.0, "CA2: epicentro dentro de 30 km");
            CHECK(dz <= 40.0, "CA2: profundidad dentro de 40 km");
            CHECK(dt0 <= 4.0, "CA2: origen dentro de 4 s");
            CHECK(cand[0].rms_sec <= cfg.MaxRMS, "RMS dentro del limite");
        }
    }

    /* CA6: determinismo 1 vs 4 hilos. */
    nev4 = BackProject(&grid, &st, picks, npick, &tt, &cfg,
                       cand, CSLOC_MAX_EVENTS, 4);
    CHECK(nev4 == nev, "CA6: mismo numero de eventos con 4 hilos");

    TTModel_Free(&tt);
    Grid_Free(&grid);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_synth_locate\n");
    return 0;
}
