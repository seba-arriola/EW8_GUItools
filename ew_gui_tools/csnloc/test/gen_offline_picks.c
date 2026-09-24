/*
 * gen_offline_picks.c
 *
 * Genera por stdout un fichero de picks en formato TYPE_PICK_SCNL a partir de
 * un hipocentro sintetico conocido, usando la MISMA tabla de tiempos de viaje
 * que csnloc. Sirve para ejercitar el modo offline de csnloc sin anillos:
 *
 *     ./test/gen_offline_picks > test/test_offline.picks
 *     ./csnloc test/csnloc_offline.d test/test_offline.picks
 *
 * Formato de linea (igual que pick_FP):
 *     8 151 255 <seq> STA.CHAN.NET.LOC <fm><wt> YYYYMMDDHHMMSS.sss amp 0 0
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../csnloc.h"

#define KM_PER_DEG 111.195

static double geoc(double lat_geo)
{
    double l = lat_geo * 0.017453292519943;
    return atan(0.993277 * tan(l)) / 0.017453292519943;
}

/* Epoch -> "YYYYMMDDHHMMSS.sss" (UTC). */
static void epoch_to_stamp(double t, char *buf, int len)
{
    time_t    sec = (time_t)t;
    struct tm tm;
    double    frac = t - (double)sec;
    char      base[20];
    int       ms;

    if (frac < 0.0) { frac += 1.0; sec -= 1; }
    ms = (int)(frac * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; sec += 1; }
    if (ms < 0)   ms = 0;
    if (ms > 999) ms = 999;
    gmtime_r(&sec, &tm);
    if (strftime(base, sizeof(base), "%Y%m%d%H%M%S", &tm) == 0) {
        snprintf(buf, len, "19700101000000.000");
        return;
    }
    snprintf(buf, len, "%s.%03d", base, ms);
}

int main(void)
{
    StationList st;
    TTModel     tt;
    double tr_lat_geo = -23.5, tr_lon = -70.0, tr_depth = 30.0, t0 = 1.8e9;
    double tr_lat_g;
    int    i, seq = 1;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        fprintf(stderr, "gen_offline_picks: no se pudo cargar estaciones\n");
        return 1;
    }
    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        fprintf(stderr, "gen_offline_picks: TTModel_Init fallo\n");
        return 1;
    }

    tr_lat_g = geoc(tr_lat_geo);

    for (i = 0; i < st.n; i++) {
        double delta = TT_GreatCircleDeg(tr_lat_g, tr_lon,
                                         st.lat_geoc_sta[i], st.st[i].lon);
        double tp, ts;
        char   stamp[32];

        if (TTModel_Predict(&tt, delta, tr_depth, CSLOC_PHASE_P, &tp, NULL) == 0) {
            epoch_to_stamp(t0 + tp, stamp, sizeof(stamp));
            printf("8 151 255 %d %s.BHZ.C.-- U1 %s 10 0 0\n",
                   seq++, st.st[i].sta, stamp);
        }
        /* Fase S para estaciones cercanas (igual que test_synth_locate). */
        if (delta < 10.0 &&
            TTModel_Predict(&tt, delta, tr_depth, CSLOC_PHASE_S, &ts, NULL) == 0) {
            epoch_to_stamp(t0 + ts, stamp, sizeof(stamp));
            printf("8 151 255 %d %s.BHZ.C.-- U1 %s 10 0 0\n",
                   seq++, st.st[i].sta, stamp);
        }
    }

    TTModel_Free(&tt);
    return 0;
}
