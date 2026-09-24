/******************************************************************************
 * stations.c                                                                 *
 *                                                                            *
 * Carga de metadata de estaciones desde el archivo de configuracion del      *
 * sistema (por defecto estaciones_107.txt):                                  *
 *                                                                            *
 *   Station  Network  Channel  Location  Lat  Lon  Elevation  Sensitivity    *
 *                                                                            *
 * Se guardan lat/lon GEOGRAFICAS y se deriva la latitud geocentrica que      *
 * necesita el modelo de tiempos de viaje (consistente con GeoCent de        *
 * geotools.c).                                                               *
 ******************************************************************************/
#include "csnloc.h"

#ifndef RAD
#define RAD 0.017453292519943
#endif

static double geocentric_lat_deg(double lat_deg)
{
    double l = lat_deg * RAD;
    double g = atan(0.993277 * tan(l));
    return (lat_deg >= 0.0) ? (g / RAD) : (g / RAD);
}

int Stations_Load(const char *sta_file, StationList *out)
{
    FILE *fp;
    char  line[512];

    if (!sta_file || !out) return -1;
    memset(out, 0, sizeof(*out));

    fp = fopen(sta_file, "r");
    if (!fp) return -1;

    while (fgets(line, sizeof(line), fp)) {
        char   sta[16], net[16], chan[16], loc[16];
        double lat, lon, elev, sens;
        int    f;
        char  *p = line;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#' || *p == '\n') continue;

        f = sscanf(p, "%15s %15s %15s %15s %lf %lf %lf %lf",
                   sta, net, chan, loc, &lat, &lon, &elev, &sens);
        if (f < 6) continue;
        if (out->n >= CSLOC_MAX_STATIONS) break;

        memset(&out->st[out->n], 0, sizeof(Station));
        strncpy(out->st[out->n].sta, sta, sizeof(out->st[0].sta) - 1);
        out->st[out->n].lat = lat;               /* geografica          */
        out->st[out->n].lon = lon;
        out->st[out->n].elev_km = (f >= 7) ? (elev / 1000.0) : 0.0;
        out->lat_geoc_sta[out->n] = geocentric_lat_deg(lat);
        out->n++;
    }
    fclose(fp);
    return (out->n > 0) ? 0 : -1;
}

int Stations_Find(const StationList *list, const char *sta)
{
    int i;
    if (!list || !sta) return -1;
    for (i = 0; i < list->n; i++) {
        if (strcmp(list->st[i].sta, sta) == 0)
            return i;
    }
    return -1;
}