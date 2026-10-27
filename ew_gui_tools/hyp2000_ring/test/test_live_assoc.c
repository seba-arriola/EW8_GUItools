/*
 * test_live_assoc.c - contrato de live_assoc + validacion de salida.
 *
 * No necesita anillos ni motor: usa iasp91 de $EW_PARAMS y estaciones
 * sinteticas. Cubre:
 *   1. P y S de un evento profundo (>60 s de P-S) se aceptan.
 *   2. Una picada 30 s fuera del tiempo predicho se rechaza.
 *   3. El ARC se construye solo con las picadas asociadas (no con el seed).
 *   4. live_check_arc rechaza nph/rms/gap invalidos y acepta uno valido.
 *   5. live_retry_exhausted aplica N_retry.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../csnloc/csnloc.h"
#include "../csnloc/live_assoc.h"
#include "../csnhypodbp/csnhypodbp_group.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static void set_station(StationList *st, const char *sta, double lat, double lon)
{
    memset(st, 0, sizeof(*st));
    st->n = 1;
    snprintf(st->st[0].sta, sizeof(st->st[0].sta), "%s", sta);
    st->st[0].lat = lat;
    st->st[0].lon = lon;
    /* Para lat=0 la geocentrica coincide; el test usa el ecuador. */
    st->lat_geoc_sta[0] = lat;
}

int main(void)
{
    const char *dir = getenv("EW_PARAMS");
    StationList st;
    TTModel tt;
    LiveSeed seed;
    LiveAssocCfg cfg;
    Pick picks[2];
    double tp = 0.0, ts = 0.0, resid = 0.0;
    char arc[8192];
    LivePickSet set;
    int nph = 0;

    if (!dir || !dir[0]) dir = "../../run_working_v8/params";
    if (TTModel_Init(&tt, dir, "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init (%s)\n", dir);
        return 1;
    }

    set_station(&st, "TST01", 0.0, 0.0);
    memset(&seed, 0, sizeof(seed));
    seed.qid = 123;
    seed.version = 1;
    seed.t0 = 1000.0;
    seed.lat = 0.0;
    seed.lon = 0.0;
    seed.depth = 300.0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.lookback_sec = 300.0;
    cfg.res_p_sec = 3.0;
    cfg.res_s_sec = 5.0;

    if (TTModel_Predict(&tt, 0.0, seed.depth, CSLOC_PHASE_P, &tp, NULL) != 0 ||
        TTModel_Predict(&tt, 0.0, seed.depth, CSLOC_PHASE_S, &ts, NULL) != 0) {
        printf("FAIL: TTModel_Predict P/S\n");
        TTModel_Free(&tt);
        return 1;
    }

    /* --- 1. P y S del mismo evento profundo: ambas se aceptan --- */
    memset(picks, 0, sizeof(picks));
    snprintf(picks[0].sta, sizeof(picks[0].sta), "TST01");
    snprintf(picks[0].net, sizeof(picks[0].net), "CX");
    snprintf(picks[0].chan, sizeof(picks[0].chan), "HHZ");
    picks[0].phase = CSLOC_PHASE_P;
    snprintf(picks[0].phase_name, sizeof(picks[0].phase_name), "P");
    picks[0].t_epoch = seed.t0 + tp;
    CHECK(live_assoc_pick(&seed, &picks[0], &tt, &st, &cfg, &resid) == 1,
          "P en el tiempo predicho se acepta");
    CHECK(fabs(resid) < 1e-6, "residual P ~ 0");

    snprintf(picks[1].sta, sizeof(picks[1].sta), "TST01");
    snprintf(picks[1].net, sizeof(picks[1].net), "CX");
    snprintf(picks[1].chan, sizeof(picks[1].chan), "HHN");
    picks[1].phase = CSLOC_PHASE_S;
    snprintf(picks[1].phase_name, sizeof(picks[1].phase_name), "S");
    picks[1].t_epoch = seed.t0 + ts;
    CHECK(live_assoc_pick(&seed, &picks[1], &tt, &st, &cfg, &resid) == 1,
          "S del mismo evento profundo se acepta (P-S puede superar 60 s)");
    CHECK(fabs(resid) < 1e-6, "residual S ~ 0");

    /* --- 2. Picada de otro evento: 30 s fuera del predicho --- */
    {
        Pick bad = picks[0];

        bad.t_epoch = seed.t0 + tp + 30.0;
        CHECK(live_assoc_pick(&seed, &bad, &tt, &st, &cfg, &resid) == 0,
              "P 30 s fuera del predicho se rechaza");
    }

    /* --- 3. El ARC sale solo de las picadas asociadas --- */
    memset(&set, 0, sizeof(set));
    CHECK(live_build_arc(&seed, picks, 2, &tt, &st, &cfg,
                         arc, sizeof(arc), &set, &nph) == 0,
          "live_build_arc construye el ARC");
    CHECK(nph == 2, "ARC con 2 fases (P y S)");
    CHECK(set.n == 2 && set.hash != 0, "hash de picadas asociadas no nulo");
    {
        CsnhypodbpSolution out;

        CHECK(csnhypodbp_parse_solution(arc, strlen(arc), 163, &out) == 0,
              "ARC parseable");
        CHECK(out.npicks == 2, "el seed no aporta fases: solo las 2 asociadas");
        CHECK(out.manual == 0, "la version viva es automatica (manual=0)");
        CHECK((out.picks[0].phase == 'P' && out.picks[1].phase == 'S') ||
              (out.picks[0].phase == 'S' && out.picks[1].phase == 'P'),
              "el ARC lleva P y S");
    }

    /* --- 4. Validacion de la salida --- */
    {
        CsnhypodbpSolution s;
        LiveLocCheckCfg lc;
        LiveLocCheck chk;
        char arc2[8192];

        memset(&s, 0, sizeof(s));
        s.qid = 1;
        s.otime = 1000.0;
        s.lat = -20.0;
        s.lon = -70.0;
        s.depth = 10.0;
        s.npicks = 1;
        snprintf(s.picks[0].sta, sizeof(s.picks[0].sta), "TST01");
        snprintf(s.picks[0].chan, sizeof(s.picks[0].chan), "HHZ");
        s.picks[0].phase = 'P';
        s.picks[0].t_pick = 1010.0;

        memset(&lc, 0, sizeof(lc));
        lc.min_phases = 4;
        lc.max_rms = 2.0;
        lc.max_gap_deg = 300.0;

        s.nph = 3; s.rms = 0.8; s.gap = 200;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC invalido por nph construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              !chk.ok && strstr(chk.why, "nph") != NULL,
              "rechaza nph < MinPhases");

        s.nph = 10; s.rms = 99.99; s.gap = 200;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC invalido por rms construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              !chk.ok && strstr(chk.why, "rms") != NULL,
              "rechaza rms=99.99 (centinela de HYPOINVERSE)");

        s.nph = 10; s.rms = 0.8; s.gap = 350;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC invalido por gap construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              !chk.ok && strstr(chk.why, "gap") != NULL,
              "rechaza gap > MaxGapDeg");

        s.nph = 10; s.rms = 0.8; s.gap = 200;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC valido construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              chk.ok, "acepta rms/nph/gap validos");

        /* Con los filtros en 0, nph<=0 y el centinela 99.99 siguen invalidos. */
        lc.min_phases = 0; lc.max_rms = 0.0; lc.max_gap_deg = 0.0;

        s.nph = 0; s.rms = 0.8; s.gap = 200;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC nph=0 construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              !chk.ok && strstr(chk.why, "nph") != NULL,
              "nph=0 se rechaza aunque min_phases=0");

        s.nph = 10; s.rms = 99.99; s.gap = 200;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC centinela construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              !chk.ok && strstr(chk.why, "centinela") != NULL,
              "rms=99.99 se rechaza aunque max_rms=0");

        s.nph = 10; s.rms = 0.8; s.gap = 350;
        CHECK(csnhypodbp_arc_build(&s, 0, arc2, sizeof(arc2)) == 0,
              "ARC gap=350 construido");
        CHECK(live_check_arc(arc2, (int)strlen(arc2), &lc, &chk) == 0 &&
              chk.ok, "gap=350 se acepta con max_gap_deg=0 (filtro off)");
    }

    /* --- 5. N_retry --- */
    CHECK(live_retry_exhausted(4, 5) == 0, "4 de 5 intentos: continua");
    CHECK(live_retry_exhausted(5, 5) == 1, "5 de 5 intentos: se detiene");
    CHECK(live_retry_exhausted(1, 0) == 1, "N_retry=0: se detiene al primer fallo");

    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_live_assoc\n");
    return 0;
}
