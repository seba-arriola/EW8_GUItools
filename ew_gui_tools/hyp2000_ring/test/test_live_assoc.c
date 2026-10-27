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
    int nph = 0, i;

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

    /* --- 6. ARC sembrado: las fases del seed son el nucleo del ARC --- */
    {
        StationList st3;
        CsnhypodbpPick sp[3];
        char arcs[8192];
        LivePickSet ss;
        int ns = 0;

        memset(&st3, 0, sizeof(st3));
        st3.n = 3;
        memset(sp, 0, sizeof(sp));
        for (i = 0; i < 3; i++) {
            snprintf(st3.st[i].sta, sizeof(st3.st[i].sta), "TS%d", i);
            st3.st[i].lat = 0.0;
            st3.st[i].lon = (double)i;
            st3.lat_geoc_sta[i] = 0.0;

            snprintf(sp[i].sta, sizeof(sp[i].sta), "TS%d", i);
            snprintf(sp[i].net, sizeof(sp[i].net), "CX");
            snprintf(sp[i].chan, sizeof(sp[i].chan), "HHZ");
            sp[i].phase = 'P';
            sp[i].t_pick = seed.t0 + tp + 0.5 * (double)i;
            sp[i].residual = NAN;
        }

        memset(&ss, 0, sizeof(ss));
        CHECK(live_build_arc_seeded(&seed, sp, 3, NULL, 0, &tt, &st3, &cfg,
                                    arcs, sizeof(arcs), &ss, &ns) == 0,
              "ARC sembrado se construye sin picadas del buffer");
        CHECK(ns == 3, "el ARC lleva las 3 fases del seed");

        /* Estaciones que NO estan en LiveStations: se conservan. El motor las
           salta con "SKIP PHASE CARD", pero descartarlas aqui puede dejar el
           ARC por debajo del minimo y el evento se pierde sin intentarlo. */
        {
            CsnhypodbpPick sx[2];
            int nx = 0;

            memset(sx, 0, sizeof(sx));
            snprintf(sx[0].sta, sizeof(sx[0].sta), "ZZ01");
            snprintf(sx[0].net, sizeof(sx[0].net), "CX");
            snprintf(sx[0].chan, sizeof(sx[0].chan), "HHZ");
            sx[0].phase = 'P';
            sx[0].t_pick = seed.t0 + tp + 1.0;
            sx[0].residual = NAN;
            snprintf(sx[1].sta, sizeof(sx[1].sta), "ZZ02");
            snprintf(sx[1].net, sizeof(sx[1].net), "CX");
            snprintf(sx[1].chan, sizeof(sx[1].chan), "HHN");
            sx[1].phase = 'S';
            sx[1].t_pick = seed.t0 + tp + 2.0;
            sx[1].residual = NAN;

            memset(&ss, 0, sizeof(ss));
            CHECK(live_build_arc_seeded(&seed, sx, 2, NULL, 0, &tt, &st3, &cfg,
                                        arcs, sizeof(arcs), &ss, &nx) == 0,
                  "seed con estaciones fuera de LiveStations se construye");
            CHECK(nx == 2, "esas fases no se descartan (el motor las salta)");
        }
    }

    /* --- 7. Poda: el veredicto del motor manda --- */
    {
        CsnhypodbpSolution in, so;
        char ain[8192], aout[8192], apr[8192];
        int n2 = 0;

        memset(&in, 0, sizeof(in));
        in.qid = 1;
        in.otime = 1000.0;
        in.lat = -20.0;
        in.lon = -70.0;
        in.depth = 10.0;
        in.npicks = 4;
        for (i = 0; i < 4; i++) {
            snprintf(in.picks[i].sta, sizeof(in.picks[i].sta), "TS%d", i);
            snprintf(in.picks[i].chan, sizeof(in.picks[i].chan), "HHZ");
            in.picks[i].phase = 'P';
            in.picks[i].t_pick = 1010.0 + (double)i;
            in.picks[i].residual = NAN;
        }
        in.nph = 4;
        CHECK(csnhypodbp_arc_build(&in, 0, ain, sizeof(ain)) == 0,
              "ARC de entrada con 4 fases");

        /* El motor uso solo 3: TS1 con residual 4 s y TS3 descartada. */
        so = in;
        so.npicks = 3;
        so.nph = 3;
        so.picks[0].residual = 0.2;
        so.picks[1].residual = 4.0;
        so.picks[2].residual = 0.1;
        CHECK(csnhypodbp_arc_build(&so, 0, aout, sizeof(aout)) == 0,
              "ARC de salida con 3 fases y residuales");

        CHECK(live_prune_arc(aout, (int)strlen(aout), ain, (int)strlen(ain),
                             3.0, 5.0, apr, sizeof(apr), &n2) == 0 && n2 == 2,
              "poda: sobreviven solo las 2 con residual <= 3 s");
        {
            CsnhypodbpSolution pr;

            CHECK(csnhypodbp_parse_solution(apr, strlen(apr), 163, &pr) == 0,
                  "ARC podado parseable");
            CHECK(pr.npicks == 2 &&
                  strcmp(pr.picks[0].sta, "TS0") == 0 &&
                  strcmp(pr.picks[1].sta, "TS2") == 0,
                  "poda: quedan TS0 y TS2 (fuera TS1 por residual y TS3 por descartada)");
        }
    }

    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_live_assoc\n");
    return 0;
}
