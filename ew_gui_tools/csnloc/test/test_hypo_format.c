/*
 * Test de FormatHYP2000ARC: verifica el layout exacto que espera glass2ew
 * (linea 1 de 162 chars + "$1" + lineas de fase de 114 chars + linea final).
 */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

/* Definido en pick_scln.c; no esta en el header publico. */
long long CSLoc_YmdHmsToEpoch(int y, int m, int d, int hh, int mm, double ss);

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static double t0_epoch(void)
{
    return (double)CSLoc_YmdHmsToEpoch(2026, 3, 9, 16, 37, 36.0);
}

static Pick mk(const char *sta, const char *net, const char *chan,
               const char *loc, int phase, double t)
{
    Pick p; memset(&p, 0, sizeof(p));
    strncpy(p.sta, sta, sizeof(p.sta)-1);
    strncpy(p.net, net, sizeof(p.net)-1);
    strncpy(p.chan, chan, sizeof(p.chan)-1);
    strncpy(p.loc, loc, sizeof(p.loc)-1);
    p.phase = phase; p.t_epoch = t; p.weight = 1;
    strncpy(p.phase_name, (phase == CSLOC_PHASE_S) ? "S" : "P",
            sizeof(p.phase_name)-1);
    return p;
}

int main(void)
{
    StationList st;
    CSLocParams cfg;
    HypoCandidate h;
    Pick picks[2];
    char buf[8192];
    const char *nl1, *nl2, *nl3;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar test_stations.txt\n");
        return 1;
    }

    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.AgencyID, "CL"); strcpy(cfg.Author, "csnloc");

    picks[0] = mk("BO03", "C1", "HHZ", "--", CSLOC_PHASE_P, t0_epoch() + 20.0);
    picks[1] = mk("GO01", "C1", "BHZ", "--", CSLOC_PHASE_S, t0_epoch() + 40.0);

    memset(&h, 0, sizeof(h));
    h.lat = 0.0; h.lon = -70.0; h.depth_km = 33.33;
    h.t0 = t0_epoch();
    h.nphases = 2;
    h.phase_idx[0] = 0; h.phase_idx[1] = 1;
    h.residual[0] = 0.5; h.residual[1] = -0.5;
    h.rms_sec = 0.5; h.gap_deg = 120.0; h.dmin_km = 50.0;

    CHECK(FormatHYP2000ARC(&h, &st, picks, &cfg, 42UL, 3U, buf, sizeof(buf)) == 0,
          "formatea sin error");

    nl1 = strchr(buf, '\n');
    CHECK(nl1 != NULL, "linea 1 con salto");
    CHECK(nl1 && (nl1 - buf) == 197, "linea 1 mide 197 chars (layout canonico)");
    CHECK(strncmp(buf, "2026030916373600", 16) == 0,
          "prefijo YYYYMMDDHHMM + ss*100");
    CHECK(strncmp(buf + 39, "  2", 3) == 0 ||
          strncmp(buf + 39, "  2", 3) == 0, "nps en offset 39");
    /* nps=2 (%3d) en 39..41, gap=120 en 42..44, dmin=50 en 45..47, rms=50 en 48..51 */
    CHECK(buf[39] == ' ' && buf[40] == ' ' && buf[41] == '2', "nps=2");
    CHECK(strncmp(buf + 136, "0000000042", 10) == 0, "event id en offset 136");
    CHECK(buf[161] == '3', "version[1] en offset 161");
    CHECK(strncmp(buf + 178, "0003", 4) == 0, "eventVersion en offset 178");

    nl2 = nl1 ? strchr(nl1 + 1, '\n') : NULL;
    CHECK(nl2 != NULL && (nl2 - (nl1 + 1)) == 197, "linea $1 mide 197 chars");
    CHECK(nl1 && nl1[1] == '$' && nl1[2] == '1', "linea $1 inicia con $1");

    nl3 = nl2 ? strchr(nl2 + 1, '\n') : NULL;
    CHECK(nl3 != NULL && (nl3 - (nl2 + 1)) == 114, "linea de fase mide 114 chars");
    CHECK(nl2 && strncmp(nl2 + 1, "BO03 ", 5) == 0, "station BO03");
    CHECK(nl2 && strncmp(nl2 + 1 + 5, "C1", 2) == 0, "network C1");
    CHECK(nl2 && strncmp(nl2 + 1 + 9, "HHZ", 3) == 0, "channel HHZ");
    CHECK(nl2 && nl2[1 + 14] == 'P', "fase P");
    CHECK(nl2 && strncmp(nl2 + 1 + 17, "20260309163756.00", 17) == 0,
          "hora de fase + 20s");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_hypo_format\n");
    return 0;
}
