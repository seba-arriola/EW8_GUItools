/*
 * CA2 (Fase 1.1): la caja de refinamiento en PROFUNDIDAD debe desacoplarse del
 * paso horizontal. Con RefineDepthKm chico, z queda clavada cerca del nodo de
 * grilla; con RefineDepthKm ampliado, z alcanza la profundidad verdadera.
 *
 * Se genera un evento sintetico a 60 km (fuera de los nodos de una grilla de
 * paso grueso) y se refina el MISMO candidato (epicentro verdadero, z=0) con
 * dos configuraciones que solo difieren en RefineDepthKm.
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
        strncpy(picks[n].sta, st->st[i].sta, sizeof(picks[n].sta) - 1);
        strcpy(picks[n].net, "C"); strcpy(picks[n].chan, "BHZ");
        strcpy(picks[n].loc, "--");
        picks[n].phase = CSLOC_PHASE_P;
        strcpy(picks[n].phase_name, "P");
        picks[n].t_epoch = t0 + tp;
        picks[n].weight = 1;
        n++;
        if (delta < 10.0 &&
            TTModel_Predict(tt, delta, tr_depth, CSLOC_PHASE_S, &ts, NULL) == 0) {
            memset(&picks[n], 0, sizeof(Pick));
            strncpy(picks[n].sta, st->st[i].sta, sizeof(picks[n].sta) - 1);
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
    StationList   st;
    TTModel       tt;
    CSLocParams   cfg_small, cfg_big;
    Pick          picks[CSLOC_MAX_PICKS];
    HypoCandidate h_small, h_big;
    double tr_lat_geo = -23.5, tr_lon = -70.0, tr_depth = 60.0;
    double tr_lat_g, t0 = 1.8e9;
    int    npick = 0;

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

    memset(&cfg_small, 0, sizeof(cfg_small));
    cfg_small.PhaseWeightP = 1.0; cfg_small.PhaseWeightS = 0.8;
    cfg_small.RefineIterations = 3; cfg_small.RefineNodeKm = 5.0;
    cfg_small.RefineDepthKm = 5.0;     /* semiancho vertical chico */
    cfg_big = cfg_small;
    cfg_big.RefineDepthKm = 40.0;      /* semiancho vertical ampliado */

    /* Mismo candidato de partida: epicentro verdadero, z en un NODO (0). */
    memset(&h_small, 0, sizeof(h_small));
    h_small.lat = tr_lat_g; h_small.lon = tr_lon;
    h_small.depth_km = 0.0; h_small.t0 = t0; h_small.nphases = npick;
    h_big = h_small;

    RefineHypo(&h_small, &st, picks, &tt, &cfg_small);
    RefineHypo(&h_big, &st, picks, &tt, &cfg_big);

    printf("nodo de partida z=0.0  verdad z=%.1f\n", tr_depth);
    printf("small: z=%.1f  escape=%.1f  lat=%.4f lon=%.4f\n",
           h_small.depth_km, h_small.depth_km, h_small.lat, h_small.lon);
    printf("big  : z=%.1f  escape=%.1f  lat=%.4f lon=%.4f\n",
           h_big.depth_km, h_big.depth_km, h_big.lat, h_big.lon);

    CHECK(h_small.depth_km <= 8.76, "caja chica NO escapa del nodo (<=8.75 km)");
    CHECK(h_big.depth_km > 8.75, "caja ampliada escapa del nodo (>8.75 km)");
    CHECK(h_big.depth_km > h_small.depth_km, "caja ampliada mueve z mas lejos");
    CHECK(fabs(h_big.lat - h_small.lat) < 0.1,
          "lat no cambia significativamente al ampliar la caja vertical");
    CHECK(fabs(h_big.lon - h_small.lon) < 0.1,
          "lon no cambia significativamente al ampliar la caja vertical");

    TTModel_Free(&tt);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_refine_depth\n");
    return 0;
}
