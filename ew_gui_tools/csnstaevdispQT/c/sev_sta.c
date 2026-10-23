/* Carga de estaciones para el mapa: port de `LoadStations`
 * (csnstaevdisp.c:316-351).
 *
 * Solo se guardan sta/lat/lon/elev: el visor dibuja triangulos, no usa canal
 * ni sensibilidad. El parser real vive en `ewgui_sta_parse_line`.
 */
#include <stdio.h>
#include <string.h>

#include "ewgui/sta.h"

#include "sev_capi.h"

size_t sev_stations_load(const char *path, SevStation *arr, size_t max)
{
    FILE *f;
    char line[512];
    size_t n = 0;

    if (!path || !*path || !arr || max == 0)
        return 0;

    f = fopen(path, "r");
    if (!f) {
        /* Paridad: el GTK avisa por stderr y sigue con 0 estaciones. */
        fprintf(stderr, ">> Error opening station file <%s>.\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), f) && n < max) {
        const char *p = line;
        EwStation st;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '#' || *p == '\n')
            continue;
        if (ewgui_sta_parse_line(p, &st) != 0)
            continue;

        snprintf(arr[n].sta, sizeof(arr[n].sta), "%s", st.sta);
        arr[n].lat = st.lat;
        arr[n].lon = st.lon;
        arr[n].elev_m = st.elev_m;
        n++;
    }
    fclose(f);
    return n;
}

void sev_stations_range(const SevStation *arr, size_t n,
                        double *elev_min, double *elev_max)
{
    double lo = 99999.0;
    double hi = -99999.0;
    size_t i;

    if (arr) {
        for (i = 0; i < n; i++) {
            if (arr[i].elev_m < lo)
                lo = arr[i].elev_m;
            if (arr[i].elev_m > hi)
                hi = arr[i].elev_m;
        }
    }
    /* Fallback si no hay estaciones o todas tienen la misma elevacion. */
    if (lo >= hi) {
        lo = 0.0;
        hi = 1000.0;
    }
    if (elev_min)
        *elev_min = lo;
    if (elev_max)
        *elev_max = hi;
}
