/*
 * test_statable - tabla de estaciones: dedup M>A>T (C1), fila con residual de
 * la solucion (C2) y color de traza (C3).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_statable.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    /* --- C1: stapick_one (una P / una S, ranking M > A > T) --- */
    {
        CsnhypodbpStaPick c[3], w;

        memset(c, 0, sizeof(c));
        c[0].present = 1; c[0].origin = 'A'; c[0].t = 10.0;
        snprintf(c[0].chan, sizeof(c[0].chan), "HHZ");
        snprintf(c[0].net, sizeof(c[0].net), "C1");
        c[1].present = 1; c[1].origin = 'T'; c[1].t = 10.5;
        c[2].present = 1; c[2].origin = 'M'; c[2].t = 11.0;
        snprintf(c[2].chan, sizeof(c[2].chan), "HHN");
        snprintf(c[2].loc, sizeof(c[2].loc), "10");
        memset(&w, 0, sizeof(w));
        CHECK(csnhypodbp_stapick_one(c, 3, 10.5, &w) == 1, "hay picada");
        CHECK(w.origin == 'M' && w.t == 11.0, "M gana sobre A y T");
        CHECK(strcmp(w.chan, "HHN") == 0, "el canal viaja con la picada elegida");
        CHECK(strcmp(w.loc, "10") == 0, "la localizacion tambien");

        c[2].present = 0;   /* sin M: A gana sobre T */
        CHECK(csnhypodbp_stapick_one(c, 3, 10.5, &w) == 1, "hay picada (sin M)");
        CHECK(w.origin == 'A', "A gana sobre T");

        /* Empate A/A -> la mas cercana a t_ref. */
        c[0].present = 1; c[0].origin = 'A'; c[0].t = 10.0;
        c[1].present = 1; c[1].origin = 'A'; c[1].t = 14.0;
        c[2].present = 0;
        CHECK(csnhypodbp_stapick_one(c, 3, 13.0, &w) == 1, "hay picada (empate)");
        CHECK(w.t == 14.0, "empate -> la mas cercana a t_ref");

        c[0].present = 0; c[1].present = 0;
        CHECK(csnhypodbp_stapick_one(c, 3, 0.0, &w) == 0, "sin candidatas -> 0");
    }

    /* --- C2: statable_build con residual de la solucion --- */
    {
        CsnhypodbpStaInput in[2];
        CsnhypodbpTracePick caps[2];
        CsnhypodbpManualPick man[1];
        CsnhypodbpSolution sol;
        CsnhypodbpStaRow rows[4];
        int nrows;

        memset(in, 0, sizeof(in));
        memset(caps, 0, sizeof(caps));
        memset(man, 0, sizeof(man));
        memset(&sol, 0, sizeof(sol));

        /* GO01: A(P) + manual S; con datos. */
        snprintf(in[0].sta, sizeof(in[0].sta), "GO01");
        in[0].dist_deg = 1.0; in[0].has_data = 1;
        /* SCNL de la estacion (metadata): respaldo de las picadas sin canal/red. */
        snprintf(in[0].chan, sizeof(in[0].chan), "HHZ");
        snprintf(in[0].net, sizeof(in[0].net), "C1");
        snprintf(in[0].loc, sizeof(in[0].loc), "--");
        in[0].caps = caps; in[0].ncap = 1;
        snprintf(caps[0].sta, sizeof(caps[0].sta), "GO01");
        caps[0].phase = 'P'; caps[0].origin = 'A'; caps[0].t = 100.0;
        snprintf(caps[0].chan, sizeof(caps[0].chan), "HHZ");
        snprintf(man[0].sta, sizeof(man[0].sta), "GO01");
        man[0].phase = 'S'; man[0].t = 130.0;
        snprintf(man[0].chan, sizeof(man[0].chan), "HHE");

        /* GO02: solo teorica P; sin datos. */
        snprintf(in[1].sta, sizeof(in[1].sta), "GO02");
        in[1].dist_deg = 2.0; in[1].has_data = 0;
        in[1].theo_p = 110.0;
        snprintf(in[1].chan, sizeof(in[1].chan), "HHZ");
        snprintf(in[1].net, sizeof(in[1].net), "CX");
        snprintf(in[1].loc, sizeof(in[1].loc), "--");

        /* Solucion con residuales para GO01 (P y S). */
        sol.npicks = 2;
        snprintf(sol.picks[0].sta, sizeof(sol.picks[0].sta), "GO01");
        sol.picks[0].phase = 'P'; sol.picks[0].residual = 0.12;
        snprintf(sol.picks[1].sta, sizeof(sol.picks[1].sta), "GO01");
        sol.picks[1].phase = 'S'; sol.picks[1].residual = -0.30;

        nrows = csnhypodbp_statable_build(rows, 4, in, 2, man, 1, &sol);
        CHECK(nrows == 2, "2 filas");
        CHECK(strcmp(rows[0].sta, "GO01") == 0, "fila 0 GO01");
        CHECK(fabs(rows[0].dist_km - 111.19) < 0.01, "distancia km");
        CHECK(rows[0].p.present && rows[0].p.origin == 'A', "GO01 P automatica");
        CHECK(strcmp(rows[0].p.chan, "HHZ") == 0, "canal de la P automatica");
        CHECK(strcmp(rows[0].p.net, "C1") == 0, "red de la P (de la estacion)");
        CHECK(fabs(rows[0].p.resid - 0.12) < 1e-6, "GO01 residual P");
        CHECK(rows[0].s.present && rows[0].s.origin == 'M', "GO01 S manual");
        CHECK(strcmp(rows[0].s.chan, "HHE") == 0, "canal de la S manual");
        CHECK(strcmp(rows[0].s.net, "C1") == 0, "red de la S (de la estacion)");
        CHECK(fabs(rows[0].s.resid - (-0.30)) < 1e-6, "GO01 residual S");

        CHECK(rows[1].p.present && rows[1].p.origin == 'T', "GO02 P teorica");
        CHECK(strcmp(rows[1].p.chan, "HHZ") == 0, "teorica con canal de la estacion");
        CHECK(strcmp(rows[1].p.net, "CX") == 0, "teorica con red de la estacion");
        CHECK(rows[1].has_data == 0, "GO02 sin datos");
        CHECK(isnan(rows[1].p.resid), "GO02 sin residual -> NAN");
        CHECK(!rows[1].s.present, "GO02 sin S");

        nrows = csnhypodbp_statable_build(rows, 4, in, 2, man, 1, NULL);
        CHECK(nrows == 2 && isnan(rows[0].p.resid), "sin solucion -> residual NAN");
    }

    /* --- C8: las automaticas P y S llegan a la fila (pick_FP y pickS) --- */
    {
        CsnhypodbpStaInput in[1];
        CsnhypodbpTracePick caps[2];
        CsnhypodbpManualPick man[1];
        CsnhypodbpStaRow rows[1];
        int nrows;

        memset(in, 0, sizeof(in));
        memset(caps, 0, sizeof(caps));
        memset(man, 0, sizeof(man));
        snprintf(in[0].sta, sizeof(in[0].sta), "GO10");
        in[0].dist_deg = 1.5; in[0].has_data = 1;
        snprintf(in[0].chan, sizeof(in[0].chan), "HHZ");
        snprintf(in[0].net, sizeof(in[0].net), "C1");
        snprintf(in[0].loc, sizeof(in[0].loc), "--");
        in[0].caps = caps; in[0].ncap = 2;
        snprintf(caps[0].sta, sizeof(caps[0].sta), "GO10");
        caps[0].phase = 'P'; caps[0].origin = 'A'; caps[0].t = 100.0;
        snprintf(caps[0].chan, sizeof(caps[0].chan), "HHZ");
        snprintf(caps[1].sta, sizeof(caps[1].sta), "GO10");
        caps[1].phase = 'S'; caps[1].origin = 'A'; caps[1].t = 130.0;
        snprintf(caps[1].chan, sizeof(caps[1].chan), "HHN");

        nrows = csnhypodbp_statable_build(rows, 1, in, 1, NULL, 0, NULL);
        CHECK(nrows == 1, "C8: 1 fila");
        CHECK(rows[0].p.present && rows[0].p.origin == 'A', "C8: P automatica (pick_FP)");
        CHECK(fabs(rows[0].p.t - 100.0) < 1e-9, "C8: tiempo de la P automatica");
        CHECK(rows[0].s.present && rows[0].s.origin == 'A', "C8: S automatica (pickS)");
        CHECK(fabs(rows[0].s.t - 130.0) < 1e-9, "C8: tiempo de la S automatica");
        CHECK(strcmp(rows[0].s.chan, "HHN") == 0, "C8: canal de la S automatica");

        /* La manual desplaza a la automatica (M > A) y no toca la S. */
        snprintf(man[0].sta, sizeof(man[0].sta), "GO10");
        man[0].phase = 'P'; man[0].t = 101.5;
        snprintf(man[0].chan, sizeof(man[0].chan), "HHZ");
        nrows = csnhypodbp_statable_build(rows, 1, in, 1, man, 1, NULL);
        CHECK(nrows == 1 && rows[0].p.origin == 'M' && fabs(rows[0].p.t - 101.5) < 1e-9,
              "C8: la P manual desplaza a la automatica");
        CHECK(rows[0].s.origin == 'A' && fabs(rows[0].s.t - 130.0) < 1e-9,
              "C8: la S automatica sigue igual");
    }

    /* --- C6: filtro de picadas para el localizador --- */
    {
        CsnhypodbpStaRow rows[2];
        CsnhypodbpPickFilter f;
        int pick_on[2][2] = { { 1, 1 }, { 1, 1 } };
        CsnhypodbpPick out[8];
        int n;

        memset(rows, 0, sizeof(rows));
        snprintf(rows[0].sta, sizeof(rows[0].sta), "GO01"); rows[0].idx = 0;
        rows[0].p.present = 1; rows[0].p.origin = 'A'; rows[0].p.t = 100.0; rows[0].p.resid = 0.1;
        snprintf(rows[0].p.chan, sizeof(rows[0].p.chan), "HHZ");
        rows[0].s.present = 1; rows[0].s.origin = 'M'; rows[0].s.t = 130.0; rows[0].s.resid = -0.2;
        snprintf(rows[0].s.chan, sizeof(rows[0].s.chan), "HHE");
        snprintf(rows[1].sta, sizeof(rows[1].sta), "GO02"); rows[1].idx = 1;
        rows[1].p.present = 1; rows[1].p.origin = 'T'; rows[1].p.t = 110.0;

        f.auto_on = 0; f.manual_on = 1; f.theo_on = 0;
        n = csnhypodbp_locate_picks(rows, 2, pick_on, &f, out, 8);
        CHECK(n == 1 && out[0].phase == 'S' && out[0].manual == 1, "solo M por defecto");
        CHECK(fabs(out[0].t_pick - 130.0) < 1e-9, "tiempo de la M");
        CHECK(strcmp(out[0].chan, "HHE") == 0, "locate_picks copia el canal (no sale STA..--.--)");
        CHECK(isnan(out[0].residual), "locate_picks NO hereda residual (lo aporta el localizador)");

        f.auto_on = 1; f.theo_on = 1;
        n = csnhypodbp_locate_picks(rows, 2, pick_on, &f, out, 8);
        CHECK(n == 3, "A+M+Teo -> 3 picadas");

        pick_on[1][0] = 0;   /* sin P ni S: la estacion no aporta nada */
        n = csnhypodbp_locate_picks(rows, 2, pick_on, &f, out, 8);
        CHECK(n == 2, "estacion sin ningun pick marcado excluida");
        pick_on[1][0] = 1;

        pick_on[0][1] = 0;
        n = csnhypodbp_locate_picks(rows, 2, pick_on, &f, out, 8);
        CHECK(n == 2, "pick S desmarcado excluido");
        pick_on[0][1] = 1;

        CHECK(csnhypodbp_pick_allowed('A', -1, 0, pick_on, &f) == 1,
              "idx<0 (fuera de la tabla) no lo filtra la seleccion");
        f.auto_on = 0;
        CHECK(csnhypodbp_pick_allowed('A', -1, 0, pick_on, &f) == 0, "A off");
    }

    /* --- C7: fuente de residuales (candidata de locate > solucion elegida) --- */
    {
        CsnhypodbpSolution prev, base;

        memset(&prev, 0, sizeof(prev));
        memset(&base, 0, sizeof(base));
        CHECK(csnhypodbp_resid_source(&prev, 1, &base) == NULL, "sin picadas -> NULL");
        base.npicks = 1;
        CHECK(csnhypodbp_resid_source(&prev, 1, &base) == &base, "sin preview -> la elegida");
        prev.npicks = 1;
        CHECK(csnhypodbp_resid_source(&prev, 1, &base) == &prev, "preview vigente gana");
        CHECK(csnhypodbp_resid_source(&prev, 0, &base) == &base, "preview no vigente -> la elegida");
        CHECK(csnhypodbp_resid_source(NULL, 1, NULL) == NULL, "sin ninguna -> NULL");
    }

    /* --- C3: color de traza (siempre negra; el origen va en la marca) --- */
    {
        double rgb[3] = { 9.0, 9.0, 9.0 };
        csnhypodbp_trace_rgb(rgb);
        CHECK(rgb[0] == 0.0 && rgb[1] == 0.0 && rgb[2] == 0.0, "traza siempre negra");
    }

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_statable\n");
    return 0;
}
