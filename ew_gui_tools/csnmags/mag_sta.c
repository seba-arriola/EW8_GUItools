#include "csnmags.h"

#include <string.h>

/*
 * mag_sta.c - Carga de estaciones (STA NET CHAN LOC lat lon elev gain) y de las
 * correcciones por estación. El `gain` es el fallback si no hay PZ.
 */

static void norm_loc(const char *in, char *out, size_t osz)
{
    if (!in || in[0] == '\0' || (in[0] == '-' && in[1] == '-')) snprintf(out, osz, "--");
    else snprintf(out, osz, "%s", in);
}

int mag_sta_load(const char *path, MagStationList *out)
{
    FILE *f;
    char  line[512];

    out->n = 0;
    f = fopen(path, "r");
    if (!f) return -1;

    while (fgets(line, sizeof(line), f)) {
        char sta[32], net[32], chan[32], loc[32];
        double lat, lon, elev, gain;
        int n;
        if (line[0] == '#' || line[0] == '\n') continue;
        n = sscanf(line, "%31s %31s %31s %31s %lf %lf %lf %lf",
                   sta, net, chan, loc, &lat, &lon, &elev, &gain);
        if (n < 7) continue;
        if (out->n >= MAG_MAX_STA) break;
        MagStation *s = &out->st[out->n];
        memset(s, 0, sizeof(*s));
        snprintf(s->sta, sizeof(s->sta), "%s", sta);
        snprintf(s->net, sizeof(s->net), "%s", net);
        snprintf(s->chan, sizeof(s->chan), "%s", chan);
        norm_loc(loc, s->loc, sizeof(s->loc));
        s->lat = lat; s->lon = lon; s->elev_m = elev;
        s->gain = (n >= 8) ? gain : 1.0e9;
        s->corr_ml = s->corr_mb = s->corr_ms = 0.0;
        out->n++;
    }
    fclose(f);
    return out->n;
}

int mag_sta_find(const MagStationList *list, const char *sta, const char *net,
                 const char *chan, const char *loc)
{
    int i, fallback = -1;
    if (!list || !sta) return -1;

    for (i = 0; i < list->n; i++) {
        const MagStation *s = &list->st[i];
        if (strcmp(s->sta, sta) != 0) continue;
        if (fallback < 0) fallback = i;
        if (net  && net[0]  && strcmp(s->net,  net)  != 0) continue;
        if (chan && chan[0] && strcmp(s->chan, chan) != 0) continue;
        return i;
    }
    /* Reintento ignorando el canal (configuraciones HHZ/BHZ divergentes). */
    for (i = 0; i < list->n; i++) {
        const MagStation *s = &list->st[i];
        if (strcmp(s->sta, sta) != 0) continue;
        if (net && net[0] && strcmp(s->net, net) != 0) continue;
        return i;
    }
    return fallback;
}

void mag_sta_apply_corr(MagStationList *list, const char *sta_corr_file)
{
    FILE *f;
    char  line[256];
    if (!list || !sta_corr_file || !sta_corr_file[0]) return;

    f = fopen(sta_corr_file, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char sta[32];
        double cml, cmb, cms;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%31s %lf %lf %lf", sta, &cml, &cmb, &cms) < 4) continue;
        for (int i = 0; i < list->n; i++) {
            if (strcmp(list->st[i].sta, sta) == 0) {
                list->st[i].corr_ml = cml;
                list->st[i].corr_mb = cmb;
                list->st[i].corr_ms = cms;
            }
        }
    }
    fclose(f);
}
