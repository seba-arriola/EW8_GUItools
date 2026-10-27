/*
 * test_emit_gate.c - retencion de la emision y correccion con el mismo id.
 *
 *   1. CSLoc_Emittable: nph/rms/gap con 0 = off.
 *   2. Un evento que dispara el gatillo con pocas fases NO se publica
 *      (queda en el registro para el dedup), y uno publicable si.
 *   3. Ya publicado: solo se corrige (v2, mismo id) si el candidato trae
 *      EmitImproveMinPicks fases mas.
 *
 * No corre csnloc completo ni anillos.
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

static Pick mk_pick(const StationList *st, TTModel *tt, int i,
                    double tr_lat_g, double tr_lon, double tr_depth, double t0)
{
    Pick p;
    double delta, tp;

    memset(&p, 0, sizeof(p));
    snprintf(p.sta, sizeof(p.sta), "%s", st->st[i].sta);
    snprintf(p.net, sizeof(p.net), "C");
    snprintf(p.chan, sizeof(p.chan), "BHZ");
    snprintf(p.loc, sizeof(p.loc), "--");
    p.phase = CSLOC_PHASE_P;
    snprintf(p.phase_name, sizeof(p.phase_name), "P");
    p.weight = 1;
    delta = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                              st->lat_geoc_sta[i], st->st[i].lon);
    if (TTModel_Predict(tt, delta, tr_depth, CSLOC_PHASE_P, &tp, NULL) != 0)
        p.t_epoch = t0;
    else
        p.t_epoch = t0 + tp;
    return p;
}

static HypoCandidate mk_cand(const Pick *w, int nph, double t0, double lat,
                             double lon, double depth, double rms, double gap)
{
    HypoCandidate c;
    int i;

    memset(&c, 0, sizeof(c));
    c.t0 = t0;
    c.lat = lat;
    c.lon = lon;
    c.depth_km = depth;
    c.nphases = nph;
    c.rms_sec = rms;
    c.gap_deg = gap;
    c.score = 1.0;
    for (i = 0; i < nph; i++) {
        c.phase_idx[i] = i;
        c.residual[i] = 0.1;
        (void)w;
    }
    return c;
}

int main(void)
{
    StationList st;
    TTModel tt;
    CSLocParams cfg;
    EventRegistry reg;
    Pick w[CSLOC_MAX_PHASES];
    HypoCandidate c;
    double tr_lat_geo = -23.5, tr_lon = -70.0, tr_depth = 30.0;
    double tr_lat_g, t0 = 1.9e9;
    unsigned long id = 0, id_a = 0;
    unsigned int ver = 0;
    int i, n = 0, r;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n");
        return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init\n");
        return 1;
    }
    tr_lat_g = geoc(tr_lat_geo);

    memset(&cfg, 0, sizeof(cfg));
    cfg.EventMatchTimeSec = 60.0;
    cfg.EventMatchDistKm = 100.0;
    cfg.EventMatchVelocityKmS = 0.0;
    cfg.EventPickTolSec = 0.1;
    cfg.EventMatchSharedPicks = 3;
    cfg.MinPPhases = 0;
    cfg.MaxAssocDistKm = 0.0;
    cfg.MaxGapDeg = 360.0;
    cfg.MaxRMS = 2.0;
    cfg.MinEmitPhases = 5;
    cfg.EmitImproveMinPicks = 3;
    cfg.AssociateOnly = 1;

    for (i = 0; i < 10; i++)
        w[n++] = mk_pick(&st, &tt, i, tr_lat_g, tr_lon, tr_depth, t0);

    /* --- 1. CSLoc_Emittable --- */
    c = mk_cand(w, 4, t0, tr_lat_g, tr_lon, tr_depth, 0.5, 100.0);
    CHECK(CSLoc_Emittable(&c, &cfg) == 0, "4 fases con MinEmitPhases=5: no");
    c.nphases = 5;
    CHECK(CSLoc_Emittable(&c, &cfg) == 1, "5 fases: si");
    c.rms_sec = 3.0;
    CHECK(CSLoc_Emittable(&c, &cfg) == 0, "rms 3.0 > MaxRMS 2.0: no");
    c.rms_sec = 1.0;
    c.gap_deg = 359.0;
    CHECK(CSLoc_Emittable(&c, &cfg) == 1, "gap 359 <= 360: si");
    c.gap_deg = 361.0;
    CHECK(CSLoc_Emittable(&c, &cfg) == 0, "gap 361 > 360: no");
    c.gap_deg = 100.0;
    {
        CSLocParams z = cfg;

        z.MinEmitPhases = 0;
        z.MaxRMS = 0.0;
        z.MaxGapDeg = 0.0;
        c.nphases = 3;
        c.gap_deg = 390.0;
        CHECK(CSLoc_Emittable(&c, &z) == 1,
              "filtros en 0: 3 fases y gap 390 pasan (0 = off)");
    }

    /* --- 2. Retencion: 4 fases no se publica, 6 si --- */
    EventRegistry_Init(&reg);
    c = mk_cand(w, 4, t0, tr_lat_g, tr_lon, tr_depth, 1.0, 200.0);
    r = EventRegistry_Upsert(&reg, &c, w, n, &cfg, &st, &tt, NULL,
                             5000UL, t0, &id, &ver, NULL);
    CHECK(r == 0, "trigger de 4 fases: NO se publica");
    CHECK(reg.n == 1, "el evento queda en el registro para el dedup");
    CHECK(reg.ev[0].emitted == 0, "emitted=0 (retenido)");

    EventRegistry_Init(&reg);
    c = mk_cand(w, 6, t0, tr_lat_g, tr_lon, tr_depth, 1.0, 200.0);
    r = EventRegistry_Upsert(&reg, &c, w, n, &cfg, &st, &tt, NULL,
                             5100UL, t0, &id_a, &ver, NULL);
    CHECK(r == 1 && ver == 1, "trigger de 6 fases: publica v1");
    CHECK(id_a == 5100UL, "el id es el del registro");

    /* --- 3. Correccion: +3 fases con el MISMO id --- */
    c = mk_cand(w, 8, t0, tr_lat_g, tr_lon, tr_depth, 1.0, 200.0);
    r = EventRegistry_Upsert(&reg, &c, w, n, &cfg, &st, &tt, NULL,
                             5104UL, t0 + 5.0, &id, &ver, NULL);
    CHECK(r == 0, "6 -> 8 fases (< +3): no corrige");

    c = mk_cand(w, 9, t0, tr_lat_g, tr_lon, tr_depth, 1.0, 200.0);
    r = EventRegistry_Upsert(&reg, &c, w, n, &cfg, &st, &tt, NULL,
                             5108UL, t0 + 10.0, &id, &ver, NULL);
    CHECK(r == 1 && ver == 2, "6 -> 9 fases (+3): corrige a v2");
    CHECK(id == id_a, "la correccion conserva el qid");
    CHECK(reg.n == 1, "sigue habiendo un solo evento");

    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_emit_gate\n");
    return 0;
}
