#include <stdio.h>
#include <string.h>

#include "ewgui/sta.h"

int ewgui_sta_parse_line(const char *line, EwStation *out)
{
    char sta[16], net[16], chan[16], loc[16];
    double lat = 0, lon = 0, elev = 0, sens = 0;
    int n;

    if (!line || !out)
        return -1;

    n = sscanf(line, "%15s %15s %15s %15s %lf %lf %lf %lf",
               sta, net, chan, loc, &lat, &lon, &elev, &sens);
    if (n < 7)
        return -1;

    memset(out, 0, sizeof(*out));
    snprintf(out->sta, sizeof(out->sta), "%s", sta);
    if (n >= 8) {
        snprintf(out->net, sizeof(out->net), "%s", net);
        snprintf(out->chan, sizeof(out->chan), "%s", chan);
        snprintf(out->loc, sizeof(out->loc), "%s", loc);
        out->sens = sens;
    }
    out->lat = lat;
    out->lon = lon;
    out->elev_m = elev;
    return 0;
}

size_t ewgui_sta_load(const char *path, EwStation *arr, size_t max)
{
    FILE *f;
    char line[512];
    size_t n = 0;

    if (!path || !arr || max == 0)
        return 0;

    f = fopen(path, "r");
    if (!f)
        return 0;

    while (n < max && fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#' || *p == '\n' || *p == '\r')
            continue;
        if (ewgui_sta_parse_line(p, &arr[n]) == 0)
            n++;
    }
    fclose(f);
    return n;
}
