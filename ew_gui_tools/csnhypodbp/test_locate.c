/*
 * test_locate - pipeline offline (partes puras): archivo de picks, hypos.jsonl,
 * extraccion del ARC del log, parseo del JSON de magnitudes y persistencia de
 * soluciones manuales.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_locate.h"
#include "csnhypodbp_statable.h"

int main(void)
{
    CsnhypodbpSolution base, sol, loaded[4];
    CsnhypodbpMagOut mag, mags[4];
    CsnhypodbpManualPick mp[1];
    char buf[8192];
    FILE *fp;
    int n, k;

    /* --- pick_on desde la tabla de magnitudes (`used`) --- */
    {
        const int map[3] = { 2, 0, 9 };   /* 9 = fuera de rango */
        const int used[3] = { 1, 0, 1 };
        int pon[4][2];

        assert(csnhypodbp_pick_on_from_used(map, used, 3, pon, 4) == 0);
        assert(pon[2][0] == 1 && pon[2][1] == 1);   /* usada -> entra */
        assert(pon[0][0] == 0 && pon[0][1] == 0);   /* no usada -> fuera */
        assert(pon[1][0] == 0 && pon[1][1] == 0);   /* sin fila de magnitudes */
        assert(pon[3][0] == 0);
        assert(csnhypodbp_pick_on_from_used(NULL, used, 3, pon, 4) == -1);
        assert(csnhypodbp_pick_on_from_used(map, used, 3, pon, 0) == -1);
    }

    /* --- mag_result_ok: una corrida que no calculo nada no es candidata --- */
    {
        CsnhypodbpMagOut m;

        memset(&m, 0, sizeof(m));
        assert(csnhypodbp_mag_result_ok(&m, 1u) == 0);
        snprintf(m.ml, sizeof(m.ml), "4.5-12");
        assert(csnhypodbp_mag_result_ok(&m, 1u) == 1);   /* mascara ML */
        assert(csnhypodbp_mag_result_ok(&m, 2u) == 0);   /* mascara Mwp */
        assert(csnhypodbp_mag_result_ok(&m, 0u) == 1);   /* 0 = las cuatro */
        assert(csnhypodbp_mag_result_ok(NULL, 1u) == 0);
    }

    /* --- picks_write (base + overlay manual) --- */
    memset(&base, 0, sizeof(base));
    base.npicks = 1;
    snprintf(base.picks[0].sta, 8, "GO01");
    snprintf(base.picks[0].chan, 8, "HHZ");
    /* net/loc REALES del ARC: HYPOINVERSE empareja por STA+NET+COMP3+LOC
       (LET 5 2 3 0 0) y con "--" rechaza TODAS las fases. */
    snprintf(base.picks[0].net, 4, "C1");
    snprintf(base.picks[0].loc, 4, "10");
    base.picks[0].phase = 'P'; base.picks[0].t_pick = 1600000300.0;
    memset(mp, 0, sizeof(mp));
    snprintf(mp[0].sta, 16, "GO02");
    snprintf(mp[0].chan, 16, "HHN");
    mp[0].phase = 'S'; mp[0].t = 1600000267.25;   /* seg = 07: ancho < 10 */
    assert(csnhypodbp_picks_write("/tmp/opencode/loc_picks.txt", 8, 0, 0, &base, mp, 1) == 2);
    fp = fopen("/tmp/opencode/loc_picks.txt", "r");
    assert(fp);
    assert(fgets(buf, sizeof(buf), fp) && strstr(buf, "GO01.HHZ.C1.10") && strstr(buf, " P A"));
    assert(strstr(buf, "20200913123140.000"));
    /* El overlay hereda net/loc de la base por estacion; GO02 no esta -> "--". */
    assert(fgets(buf, sizeof(buf), fp) && strstr(buf, "GO02.HHN.--.--") && strstr(buf, " S M"));
    /* Segundos < 10 -> UN solo token (nada de " 7.250" con espacio, que csnloc
       descarta porque su sscanf de YYYYMMDDHHMMSS.sss falla). */
    assert(strstr(buf, "20200913123107.250") && !strstr(buf, " 7.250"));
    fclose(fp);

    /* --- locate_picks -> picks_write: el canal de la tabla no se pierde --- */
    {
        CsnhypodbpStaRow rows[1];
        CsnhypodbpPickFilter f = { 0, 1, 0 };   /* solo manuales */
        CsnhypodbpPick ps[4];
        CsnhypodbpSolution s2;
        int pick_on[1][2] = { { 1, 1 } };
        int np;
        memset(rows, 0, sizeof(rows));
        snprintf(rows[0].sta, sizeof(rows[0].sta), "GO03");
        rows[0].idx = 0;
        rows[0].p.present = 1; rows[0].p.origin = 'M'; rows[0].p.t = 1600000300.0;
        snprintf(rows[0].p.chan, sizeof(rows[0].p.chan), "BHZ");
        rows[0].s.present = 1; rows[0].s.origin = 'M'; rows[0].s.t = 1600000340.0;
        snprintf(rows[0].s.chan, sizeof(rows[0].s.chan), "BHN");
        np = csnhypodbp_locate_picks(rows, 1, pick_on, &f, ps, 4);
        assert(np == 2);
        memset(&s2, 0, sizeof(s2));
        s2.npicks = np;
        memcpy(s2.picks, ps, sizeof(CsnhypodbpPick) * (size_t)np);
        assert(csnhypodbp_picks_write("/tmp/opencode/loc_picks2.txt", 8, 0, 0, &s2, NULL, 0) == 2);
        fp = fopen("/tmp/opencode/loc_picks2.txt", "r");
        assert(fp);
        assert(fgets(buf, sizeof(buf), fp) && strstr(buf, "GO03.BHZ.--.--"));
        assert(fgets(buf, sizeof(buf), fp) && strstr(buf, "GO03.BHN.--.--"));
        fclose(fp);
    }

    /* --- arc_extract --- */
    {
        const char *log = "basura\ncsnloc: --- HYP2000ARC evento 123-1 ---\nLINEA1\nLINEA2\n"
                          "csnloc: --- fin HYP2000ARC evento 123-1 ---\nmas basura\n";
        assert(csnhypodbp_arc_extract(log, buf, sizeof(buf)) == 0);
        assert(strstr(buf, "LINEA1") && strstr(buf, "LINEA2"));
        assert(!strstr(buf, "HYP2000ARC"));
    }

    /* --- arc_extract con VARIOS bloques: gana el ULTIMO (la solucion final de
       la ventana deslizante), no el primero. La primera suele ser parcial y con
       ella los refinadores se quedan sin fases utiles. --- */
    {
        const char *log2 = "csnloc: --- HYP2000ARC evento 5-1 ---\nVIEJO\n"
                           "csnloc: --- fin HYP2000ARC evento 5-1 ---\n"
                           "csnloc: --- HYP2000ARC evento 5-1 ---\nNUEVO\n"
                           "csnloc: --- fin HYP2000ARC evento 5-1 ---\n";
        assert(csnhypodbp_arc_extract(log2, buf, sizeof(buf)) == 0);
        assert(strstr(buf, "NUEVO") && !strstr(buf, "VIEJO"));
    }

    /* --- skip_preamble: HYPOINVERSE imprime "NNN STATIONS READ IN." antes del
       ARC en stdout; hay que saltarlo para parsear la cabecera. --- */
    {
        char out2[1024];
        char pre[256];
        char *arcp;
        memset(out2, ' ', sizeof(out2));
        /* cabecera sintetica valida: 197 chars, 14 digitos iniciales */
        memcpy(out2, "20260926002823", 14);
        out2[196] = '\0';
        snprintf(pre, sizeof(pre), "   387 STATIONS READ IN.\n%s", out2);
        arcp = csnhypodbp_arc_skip_preamble(pre);
        assert(arcp && strncmp(arcp, "20260926002823", 14) == 0);
        assert(csnhypodbp_arc_skip_preamble((char *)"solo basura\n") == NULL);
    }

    /* --- mag_parse --- */
    assert(csnhypodbp_mag_parse("{\"event\":\"x\",\"ML\":5.20,\"nML\":10,\"Mwp\":null,\"nMwp\":0,}", &mag) == 0);
    assert(strcmp(mag.ml, "5.2-10") == 0);
    assert(mag.mwp[0] == '\0');

    /* --- manual_state round-trip --- */
    memset(&sol, 0, sizeof(sol));
    sol.qid = 42; sol.version = 3; sol.otime = 100.0;
    sol.lat = -33.10; sol.lon = -70.20; sol.depth = 25.0; sol.rms = 0.4; sol.nph = 8;
    sol.manual = 1; sol.npicks = 1;
    snprintf(sol.picks[0].sta, 8, "GO01");
    snprintf(sol.picks[0].chan, 8, "HHZ");
    snprintf(sol.picks[0].net, 4, "C1");
    snprintf(sol.picks[0].loc, 4, "10");
    sol.picks[0].phase = 'P'; sol.picks[0].t_pick = 101.5;
    memset(&mag, 0, sizeof(mag));
    snprintf(mag.ml, 32, "5.2-10");
    snprintf(mag.mwp, 32, "6.0-3");

    remove("/tmp/opencode/loc_state.txt");
    assert(csnhypodbp_manual_state_save("/tmp/opencode/loc_state.txt", &sol, &mag) == 0);
    sol.qid = 43;
    assert(csnhypodbp_manual_state_save("/tmp/opencode/loc_state.txt", &sol, &mag) == 0);
    n = csnhypodbp_manual_state_load("/tmp/opencode/loc_state.txt", loaded, mags, 4);
    assert(n == 2);
    k = (loaded[0].qid == 42) ? 0 : 1;
    assert(loaded[k].lat < -33.0 && loaded[k].lat > -33.2);
    assert(loaded[k].nph == 8 && loaded[k].npicks == 1 && loaded[k].manual == 1);
    assert(strcmp(loaded[k].picks[0].sta, "GO01") == 0);
    /* El SCNL tambien viaja (sin canal, HYPOINVERSE rechaza la fase). */
    assert(strcmp(loaded[k].picks[0].chan, "HHZ") == 0);
    assert(strcmp(loaded[k].picks[0].net, "C1") == 0);
    assert(strcmp(loaded[k].picks[0].loc, "10") == 0);
    assert(strcmp(mags[k].ml, "5.2-10") == 0);
    assert(strcmp(mags[k].mwp, "6.0-3") == 0);

    /* Re-guardar el mismo qid reemplaza (no duplica). */
    assert(csnhypodbp_manual_state_save("/tmp/opencode/loc_state.txt", &sol, &mag) == 0);
    n = csnhypodbp_manual_state_load("/tmp/opencode/loc_state.txt", loaded, mags, 4);
    assert(n == 2);

    /* --- arc_build round-trip (lo usa confirm) --- */
    {
        CsnhypodbpSolution in, out;
        char arc[4096];
        memset(&in, 0, sizeof(in));
        in.qid = 987654; in.version = 7; in.otime = 1600000000.5;
        in.lat = -33.10; in.lon = -70.20; in.depth = 25.0; in.rms = 0.42; in.nph = 8; in.gap = 120;
        in.npicks = 2;
        snprintf(in.picks[0].sta, 8, "GO01"); snprintf(in.picks[0].chan, 8, "HHZ");
        snprintf(in.picks[0].net, 4, "C1"); snprintf(in.picks[0].loc, 4, "10");
        in.picks[0].phase = 'P'; in.picks[0].t_pick = 1600000010.0;
        in.picks[0].residual = 0.25;
        snprintf(in.picks[1].sta, 8, "GO02"); snprintf(in.picks[1].chan, 8, "HHN");
        snprintf(in.picks[1].net, 4, "CX");
        in.picks[1].phase = 'S'; in.picks[1].t_pick = 1600000020.0;
        in.picks[1].residual = -0.10;
        assert(csnhypodbp_arc_build(&in, 1, arc, sizeof(arc)) == 0);
        assert(csnhypodbp_parse_solution(arc, strlen(arc), 162, &out) == 0);
        assert(out.qid == 987654);
        assert(out.version == 7);
        assert(out.manual == 1);
        assert(out.nph == 8);
        assert(fabs(out.lat - (-33.10)) < 0.01);
        assert(fabs(out.lon - (-70.20)) < 0.01);
        assert(fabs(out.depth - 25.0) < 0.1);
        assert(out.npicks == 2);
        assert(strcmp(out.picks[0].sta, "GO01") == 0 && strcmp(out.picks[0].chan, "HHZ") == 0);
        /* Red y loc del ARC (antes se perdian: solo se escribia el canal). */
        assert(strcmp(out.picks[0].net, "C1") == 0);
        assert(strcmp(out.picks[0].loc, "10") == 0);
        assert(strcmp(out.picks[1].net, "CX") == 0);
        assert(out.picks[0].phase == 'P');
        assert(strcmp(out.picks[1].sta, "GO02") == 0 && out.picks[1].phase == 'S');
        /* El residual viaja en el ARC (round-trip). */
        assert(fabs(out.picks[0].residual - 0.25) < 1e-6);
        assert(fabs(out.picks[1].residual - (-0.10)) < 1e-6);
    }

    /* --- sol_apply_jsonl: residual del JSONL de csnloc (el ARC va en blanco) --- */
    {
        CsnhypodbpSolution js;
        const char *jsonl =
            "{\"event\":1,\"t0\":100.0,\"nphases\":2,\"phases\":["
            "{\"sta\":\"GO01\",\"net\":\"C1\",\"chan\":\"HHZ\",\"loc\":\"\","
            "\"phase\":\"P\",\"t_epoch\":101.0,\"residual\":0.123,\"weight\":0},"
            "{\"sta\":\"GO02\",\"net\":\"C1\",\"chan\":\"HHN\",\"loc\":\"\","
            "\"phase\":\"S\",\"t_epoch\":110.0,\"residual\":-0.45,\"weight\":0}"
            "]}\n";
        memset(&js, 0, sizeof(js));
        js.npicks = 2;
        snprintf(js.picks[0].sta, 8, "GO01"); snprintf(js.picks[0].chan, 8, "HHZ");
        js.picks[0].phase = 'P'; js.picks[0].residual = NAN;
        snprintf(js.picks[1].sta, 8, "GO02"); snprintf(js.picks[1].chan, 8, "HHN");
        js.picks[1].phase = 'S'; js.picks[1].residual = NAN;
        assert(csnhypodbp_sol_apply_jsonl(&js, jsonl) == 2);
        assert(fabs(js.picks[0].residual - 0.123) < 1e-6);
        assert(fabs(js.picks[1].residual - (-0.45)) < 1e-6);
        /* El JSONL manda SIEMPRE: es el residual de ESTA corrida. */
        js.picks[0].residual = 0.9;
        js.picks[1].residual = NAN;
        assert(csnhypodbp_sol_apply_jsonl(&js, jsonl) == 2);
        assert(fabs(js.picks[0].residual - 0.123) < 1e-6);
        assert(fabs(js.picks[1].residual - (-0.45)) < 1e-6);
    }

    /* --- cfg temporal: overrides por clave y FixedDepthKm --- */
    {
        const char *src = "/tmp/opencode/test_locate_cfg_src.d";
        const char *dst = "/tmp/opencode/test_locate_cfg_dst.d";
        const char *orig =
            "# config de prueba\n"
            "InRing PICK_RING\n"
            "DumpHypo 0\n"
            "DumpHypoFiles 7\n"
            "FixedDepthKm 1.0\n"
            "FixedDepthKm 2.0\n"
            "OutRing HYPO_RING\n";
        CsnhypodbpCfgOverride ovr[2];
        char *p;
        int hits;

        fp = fopen(src, "w");
        assert(fp != NULL);
        fputs(orig, fp);
        fclose(fp);

        /* km >= 0: reemplaza la 1.a y descarta la repetida. */
        assert(csnhypodbp_cfg_with_fixdepth(src, dst, 33.0) == 0);
        fp = fopen(dst, "r");
        assert(fp != NULL);
        n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        assert(strstr(buf, "FixedDepthKm 33.000\n") != NULL);
        assert(strstr(buf, "FixedDepthKm 1.0") == NULL);
        assert(strstr(buf, "FixedDepthKm 2.0") == NULL);
        assert(strstr(buf, "InRing PICK_RING") != NULL);
        assert(strstr(buf, "OutRing HYPO_RING") != NULL);
        assert(strstr(buf, "DumpHypo 0") != NULL);   /* sin override de DumpHypo */
        for (p = buf, hits = 0; (p = strstr(p, "FixedDepthKm ")) != NULL; p++, hits++)
            ;
        assert(hits == 1);

        /* Refinador offline: WorkDir propio + FixedDepthKm en la misma copia. */
        assert(csnhypodbp_cfg_with_refiner_opts(src, dst,
                                                "../nlloc_ring_run_123", 33.0) == 0);
        fp = fopen(dst, "r");
        assert(fp != NULL);
        n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        assert(strstr(buf, "WorkDir ../nlloc_ring_run_123\n") != NULL);
        assert(strstr(buf, "FixedDepthKm 33.000\n") != NULL);
        assert(strstr(buf, "FixedDepthKm 1.0") == NULL);
        assert(strstr(buf, "InRing PICK_RING") != NULL);

        /* km < 0: copia literal (las dos lineas tal cual). */
        assert(csnhypodbp_cfg_with_fixdepth(src, dst, -1.0) == 0);
        fp = fopen(dst, "r");
        assert(fp != NULL);
        n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        assert(strcmp(buf, orig) == 0);

        /* Dos overrides a la vez y frontera de token (DumpHypoFiles no casa). */
        ovr[0].key = "DumpHypo";   ovr[0].value = "1";
        ovr[1].key = "OutRing";    ovr[1].value = "HYPO_RING_REF";
        assert(csnhypodbp_cfg_copy(src, dst, ovr, 2) == 0);
        fp = fopen(dst, "r");
        assert(fp != NULL);
        n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        assert(strstr(buf, "DumpHypo 1\n") != NULL);
        assert(strstr(buf, "DumpHypoFiles 7") != NULL);
        assert(strstr(buf, "DumpHypo 0") == NULL);
        assert(strstr(buf, "OutRing HYPO_RING_REF") != NULL);
        assert(strstr(buf, "OutRing HYPO_RING\n") == NULL);

        remove(src);
        remove(dst);
    }

    printf("test_locate OK\n");
    return 0;
}
