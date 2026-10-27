/*
 * test_trigger_gate.c - puertas de gatillo de csnloc.
 *
 *   1. CSLoc_EventMatch: ventana fija y ventana que crece con la distancia.
 *   2. AssembleCandidates: MinPPhases rechaza un candidato S-only.
 *   3. AssembleCandidates: MaxAssocDistKm no usa estaciones lejanas.
 *
 * No corre csnloc completo ni anillos: usa iasp91 y test_stations.txt.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static double geoc(double lat_geo)
{
    double l = lat_geo * 0.017453292519943;
    return atan(0.993277 * tan(l)) / 0.017453292519943;
}

static Pick mk_pick(const StationList *st, TTModel *tt, int i, int phase,
                    double tr_lat_g, double tr_lon, double tr_depth, double t0)
{
    Pick p;
    double delta, tp;

    memset(&p, 0, sizeof(p));
    snprintf(p.sta, sizeof(p.sta), "%s", st->st[i].sta);
    snprintf(p.net, sizeof(p.net), "C");
    snprintf(p.chan, sizeof(p.chan), "BHZ");
    snprintf(p.loc, sizeof(p.loc), "--");
    p.phase = phase;
    snprintf(p.phase_name, sizeof(p.phase_name), "%s",
             phase == CSLOC_PHASE_S ? "S" : "P");
    p.weight = 1;
    delta = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                              st->lat_geoc_sta[i], st->st[i].lon);
    if (TTModel_Predict(tt, delta, tr_depth, phase, &tp, NULL) != 0)
        p.t_epoch = t0;
    else
        p.t_epoch = t0 + tp;
    return p;
}

int main(void)
{
    StationList st;
    TTModel tt;
    CSLocParams cfg;
    HypoCandidate nuc, out[4];
    Pick picks[3];
    double tr_lat_geo = -23.5, tr_lon = -70.0, tr_depth = 30.0;
    double tr_lat_g = geoc(tr_lat_geo), t0 = 1.8e9;
    int i, n;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n");
        return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n");
        return 1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.MinPhasesPerEvent = 3;
    cfg.MaxRMS = 2.0;
    cfg.DBSCAN_Eps = 60.0;
    cfg.DBSCAN_MinPts = 1;
    cfg.NumThreads = 1;
    cfg.EventMatchTimeSec = 60.0;
    cfg.EventMatchDistKm = 100.0;
    cfg.EventMatchVelocityKmS = 0.0;

    memset(&nuc, 0, sizeof(nuc));
    nuc.t0 = t0;
    nuc.lat = tr_lat_g;
    nuc.lon = tr_lon;
    nuc.depth_km = tr_depth;
    nuc.nphases = 3;
    nuc.score = 1.0;
    nuc.grid_level = GRID_LEVEL_LOCAL;

    /* --- 1. CSLoc_EventMatch --- */
    CHECK(CSLoc_EventMatch(&cfg, t0, tr_lat_g, tr_lon,
                           t0 + 45.0, geoc(tr_lat_geo + 0.8), tr_lon,
                           NULL, NULL) == 1,
          "match: dt=45 s y d~89 km dentro de 60 s/100 km");
    CHECK(CSLoc_EventMatch(&cfg, t0, tr_lat_g, tr_lon,
                           t0 + 45.0, geoc(tr_lat_geo + 2.0), tr_lon,
                           NULL, NULL) == 0,
          "no match: d~222 km > 100 km");
    cfg.EventMatchVelocityKmS = 6.0;
    CHECK(CSLoc_EventMatch(&cfg, t0, tr_lat_g, tr_lon,
                           t0 + 70.0, geoc(tr_lat_geo + 0.8), tr_lon,
                           NULL, NULL) == 1,
          "ventana creciente: dt=70 s y 89 km pasa con V=6 km/s");
    cfg.EventMatchVelocityKmS = 0.0;
    CHECK(CSLoc_EventMatch(&cfg, t0, tr_lat_g, tr_lon,
                           t0 + 70.0, geoc(tr_lat_geo + 0.8), tr_lon,
                           NULL, NULL) == 0,
          "ventana fija: dt=70 s > 60 s no pasa");

    /* --- 2. MinPPhases: S-only no gatilla --- */
    for (i = 0; i < 3; i++)
        picks[i] = mk_pick(&st, &tt, i, CSLOC_PHASE_S,
                           tr_lat_g, tr_lon, tr_depth, t0);

    cfg.MinPPhases = 1;
    n = AssembleCandidates(&nuc, 1, &st, picks, 3, &tt, &cfg, out, 4);
    CHECK(n == 0, "MinPPhases=1: candidato S-only rechazado");

    cfg.MinPPhases = 0;
    n = AssembleCandidates(&nuc, 1, &st, picks, 3, &tt, &cfg, out, 4);
    CHECK(n == 1, "MinPPhases=0: el mismo S-only se acepta");

    /* --- 3. MaxAssocDistKm: FAR1 (~10 grados) queda fuera --- */
    for (i = 0; i < 3; i++)
        picks[i] = mk_pick(&st, &tt, i, CSLOC_PHASE_P,
                           tr_lat_g, tr_lon, tr_depth, t0);

    cfg.MaxAssocDistKm = 1000.0;
    n = AssembleCandidates(&nuc, 1, &st, picks, 3, &tt, &cfg, out, 4);
    CHECK(n == 0, "MaxAssocDistKm=1000: FAR1 (~1112 km) deja solo 2 fases");

    cfg.MaxAssocDistKm = 0.0;
    n = AssembleCandidates(&nuc, 1, &st, picks, 3, &tt, &cfg, out, 4);
    CHECK(n == 1, "MaxAssocDistKm=0: las 3 estaciones entran");

    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_trigger_gate\n");
    return 0;
}
