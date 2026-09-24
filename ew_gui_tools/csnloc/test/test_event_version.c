/*
 * C1/C2/C12: versionado dinamico del registro de eventos.
 *
 * Las fases se generan con tiempos de viaje REALES (IASP91) a partir de un
 * hipocentro conocido, para que el refinado produzca RMS pequenos y la
 * politica de aceptacion se comporte como en produccion.
 *
 * - Un evento nuevo se crea con version=1.
 * - Un candidato que mejora (mas fases) emite version=2 con el MISMO id.
 * - Un candidato que empeora (re-pick desplazado) NO emite.
 * - Las fases se acumulan: la primera estacion nunca sale de la solucion.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static double geoc(double lat_geo)
{
    double l = lat_geo * 0.017453292519943;
    return atan(0.993277 * tan(l)) / 0.017453292519943;
}

/* Genera un pick P para la estacion i con el tiempo de viaje real. */
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
        p.t_epoch = t0;   /* fallback */
    else
        p.t_epoch = t0 + tp;
    return p;
}

static HypoCandidate mk_cand(const Pick *window, int nph, double t0,
                             double lat, double lon, double depth,
                             double rms, double gap)
{
    HypoCandidate c; int i;
    memset(&c, 0, sizeof(c));
    c.t0 = t0; c.lat = lat; c.lon = lon; c.depth_km = depth;
    c.nphases = nph; c.rms_sec = rms; c.gap_deg = gap; c.dmin_km = 50.0;
    c.score = 1.0; c.grid_level = GRID_LEVEL_LOCAL;
    for (i = 0; i < nph; i++) { c.phase_idx[i] = i; c.residual[i] = 0.1; }
    (void)window;
    return c;
}

int main(void)
{
    StationList st;
    TTModel tt;
    CSLocParams cfg;
    EventRegistry reg;
    Pick window[CSLOC_MAX_PHASES];
    HypoCandidate c;
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

    EventRegistry_Init(&reg);

    /* --- Evento nuevo con 3 fases reales -> version 1 --- */
    window[nwin++] = mk_pick(&st, &tt, 0, tr_lat_g, tr_lon, tr_depth, t0);
    window[nwin++] = mk_pick(&st, &tt, 1, tr_lat_g, tr_lon, tr_depth, t0);
    window[nwin++] = mk_pick(&st, &tt, 2, tr_lat_g, tr_lon, tr_depth, t0);
    c = mk_cand(window, 3, t0, tr_lat_g, tr_lon, tr_depth, 0.5, 120.0);
    r = EventRegistry_Upsert(&reg, &c, window, nwin, &cfg, &st, &tt, NULL,
                             1000UL, &id1, &v1);
    CHECK(r == 1 && v1 == 1, "evento nuevo emite version 1");
    CHECK(id1 == 1000UL, "id base asignado");
    CHECK(reg.n == 1, "registro con 1 evento");
    CHECK(reg.ev[0].nphases_stored == 3, "3 fases almacenadas");

    /* --- Mismo evento, misma ventana -> sin cambios, no emite --- */
    r = EventRegistry_Upsert(&reg, &c, window, nwin, &cfg, &st, &tt, NULL,
                             1000UL, &id2, &v2);
    CHECK(r == 0, "sin picks nuevos no emite");

    /* --- Llega una fase nueva (4a estacion) -> version 2, mismo id --- */
    window[nwin++] = mk_pick(&st, &tt, 3, tr_lat_g, tr_lon, tr_depth, t0);
    c = mk_cand(window, 4, t0, tr_lat_g, tr_lon, tr_depth, 0.4, 110.0);
    r = EventRegistry_Upsert(&reg, &c, window, nwin, &cfg, &st, &tt, NULL,
                             1000UL, &id2, &v2);
    CHECK(r == 1 && v2 == 2, "fase nueva emite version 2");
    CHECK(id2 == id1, "mismo id en la actualizacion");
    CHECK(reg.ev[0].nphases_stored == 4, "4 fases acumuladas");

    /* --- C12: la primera estacion sigue en la solucion --- */
    CHECK(strcmp(reg.ev[0].phases[0].sta, window[0].sta) == 0,
          "C12: primera estacion conservada");

    /* --- C2: re-pick que empeora (desplazado 2 s) no emite --- */
    {
        unsigned int v_before = reg.ev[0].version;
        window[0].t_epoch += 2.0;
        c = mk_cand(window, 4, t0, tr_lat_g, tr_lon, tr_depth, 0.4, 110.0);
        r = EventRegistry_Upsert(&reg, &c, window, nwin, &cfg, &st, &tt, NULL,
                                 1000UL, &id2, &v2);
        CHECK(r == 0, "C2: re-pick que empeora no emite");
        CHECK(reg.ev[0].version == v_before, "C2: version no incrementa");
    }

    /* --- Evento distinto (lejos en tiempo/espacio) -> id nuevo --- */
    {
        Pick w2[CSLOC_MAX_PHASES];
        int n2 = 0;
        HypoCandidate c2;
        unsigned long id3 = 0; unsigned int v3 = 0;
        double t2 = t0 + 5000.0;
        w2[n2++] = mk_pick(&st, &tt, 0, geoc(-30.0), -72.0, 20.0, t2);
        w2[n2++] = mk_pick(&st, &tt, 1, geoc(-30.0), -72.0, 20.0, t2);
        w2[n2++] = mk_pick(&st, &tt, 2, geoc(-30.0), -72.0, 20.0, t2);
        c2 = mk_cand(w2, 3, t2, geoc(-30.0), -72.0, 20.0, 0.5, 120.0);
        r = EventRegistry_Upsert(&reg, &c2, w2, n2, &cfg, &st, &tt, NULL,
                                 1001UL, &id3, &v3);
        CHECK(r == 1 && id3 == 1001UL && v3 == 1, "evento distinto -> id nuevo");
        CHECK(reg.n == 2, "registro con 2 eventos");
    }

    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_event_version\n");
    return 0;
}
