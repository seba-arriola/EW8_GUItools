/******************************************************************************
 * ttp.c                                                                      *
 *                                                                            *
 * Predicción de tiempos de viaje P/S reutilizando el motor IASP91 de csnloc. *
 * Sirve para validar picks (p. ej. los S de pickS) contra una solución       *
 * hipocentral CONOCIDA: tpred = t0 + TT(delta, prof).                        *
 *                                                                            *
 * Uso:                                                                       *
 *   ttp <estaciones_file> <events_tsv> [--phase P|S|both]                    *
 *                                                                            *
 *   events_tsv : líneas  id<TAB>lat<TAB>lon<TAB>depth_km                     *
 *   salida     : líneas  id<TAB>sta<TAB>delta_deg<TAB>ttP<TAB>ttS            *
 *                (ttP/ttS en segundos; -1 si no se pudo predecir)            *
 *                                                                            *
 * Requiere iasp91.tbl/.hed en el cwd (igual que el resto de csnloc).         *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "csnloc.h"

#define TTP_RAD 0.017453292519943295

/* Misma conversión que stations.c (geocentric_lat_deg, static allí). */
static double geocentric_lat_deg(double lat_deg)
{
    double l = lat_deg * TTP_RAD;
    double g = atan(0.993277 * tan(l));
    return g / TTP_RAD;
}

static void usage(const char *p)
{
    fprintf(stderr, "Uso: %s <estaciones_file> <events_tsv> [--phase P|S|both]\n", p);
    fprintf(stderr, "  events_tsv: id<TAB>lat<TAB>lon<TAB>depth_km\n");
    fprintf(stderr, "  salida    : id<TAB>sta<TAB>delta_deg<TAB>ttP<TAB>ttS\n");
}

int main(int argc, char **argv)
{
    const char *stafile, *events;
    TTModel     tt;
    StationList list;
    FILE       *fe;
    char        line[512];
    int         want_p = 1, want_s = 1;

    if (argc < 3) { usage(argv[0]); return 1; }
    stafile = argv[1];
    events  = argv[2];

    if (argc >= 5 && strcmp(argv[3], "--phase") == 0) {
        char c = argv[4][0];
        if (c == 'P' || c == 'p') { want_s = 0; }
        else if (c == 'S' || c == 's') { want_p = 0; }
    }

    if (Stations_Load(stafile, &list) != 0 || list.n <= 0) {
        fprintf(stderr, "ttp: no se pudieron cargar estaciones <%s>\n", stafile);
        return 1;
    }

    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        fprintf(stderr, "ttp: faltan iasp91.tbl/.hed en el cwd\n");
        return 1;
    }

    fe = fopen(events, "r");
    if (!fe) {
        perror(events);
        TTModel_Free(&tt);
        return 1;
    }

    while (fgets(line, sizeof(line), fe)) {
        int    id, i;
        double lat, lon, dep, hyp_geoc;
        char  *p = line;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        if (sscanf(p, "%d %lf %lf %lf", &id, &lat, &lon, &dep) != 4) continue;

        hyp_geoc = geocentric_lat_deg(lat);
        for (i = 0; i < list.n; i++) {
            double delta = TT_GreatCircleDeg(hyp_geoc, lon,
                                             list.lat_geoc_sta[i], list.st[i].lon);
            double tp = -1.0, ts = -1.0;
            if (want_p) {
                if (TTModel_Predict(&tt, delta, dep, CSLOC_PHASE_P, &tp, NULL) != 0)
                    tp = -1.0;
            }
            if (want_s) {
                if (TTModel_Predict(&tt, delta, dep, CSLOC_PHASE_S, &ts, NULL) != 0)
                    ts = -1.0;
            }
            printf("%d\t%s\t%.4f\t%.4f\t%.4f\n", id, list.st[i].sta, delta, tp, ts);
        }
    }

    fclose(fe);
    TTModel_Free(&tt);
    return 0;
}
