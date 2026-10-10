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

int main(void)
{
    CsnhypodbpSolution base, sol, loaded[4];
    CsnhypodbpMagOut mag, mags[4];
    CsnhypodbpManualPick mp[1];
    char buf[8192];
    FILE *fp;
    int n, k;

    /* --- picks_write (base + overlay manual) --- */
    memset(&base, 0, sizeof(base));
    base.npicks = 1;
    snprintf(base.picks[0].sta, 8, "GO01");
    snprintf(base.picks[0].chan, 8, "HHZ");
    base.picks[0].phase = 'P'; base.picks[0].t_pick = 0.0;
    memset(mp, 0, sizeof(mp));
    snprintf(mp[0].sta, 16, "GO02");
    snprintf(mp[0].chan, 16, "HHN");
    mp[0].phase = 'S'; mp[0].t = 0.0;
    assert(csnhypodbp_picks_write("/tmp/opencode/loc_picks.txt", 8, 0, 0, &base, mp, 1) == 2);
    fp = fopen("/tmp/opencode/loc_picks.txt", "r");
    assert(fp);
    assert(fgets(buf, sizeof(buf), fp) && strstr(buf, "GO01.HHZ.--.--") && strstr(buf, " P A"));
    assert(fgets(buf, sizeof(buf), fp) && strstr(buf, "GO02.HHN.--.--") && strstr(buf, " S M"));
    fclose(fp);

    /* --- arc_extract --- */
    {
        const char *log = "basura\ncsnloc: --- HYP2000ARC evento 123-1 ---\nLINEA1\nLINEA2\n"
                          "csnloc: --- fin HYP2000ARC evento 123-1 ---\nmas basura\n";
        assert(csnhypodbp_arc_extract(log, buf, sizeof(buf)) == 0);
        assert(strstr(buf, "LINEA1") && strstr(buf, "LINEA2"));
        assert(!strstr(buf, "HYP2000ARC"));
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
        in.picks[0].phase = 'P'; in.picks[0].t_pick = 1600000010.0;
        snprintf(in.picks[1].sta, 8, "GO02"); snprintf(in.picks[1].chan, 8, "HHN");
        in.picks[1].phase = 'S'; in.picks[1].t_pick = 1600000020.0;
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
        assert(out.picks[0].phase == 'P');
        assert(strcmp(out.picks[1].sta, "GO02") == 0 && out.picks[1].phase == 'S');
    }

    printf("test_locate OK\n");
    return 0;
}
