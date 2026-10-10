#include "csnhypodbp_station.h"

#include <stdio.h>
#include <string.h>

int csnhypodbp_split_ws(const char *s, char out[][16], int max)
{
    int n = 0;
    if (!s) return 0;
    while (*s) {
        int k = 0;
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        if (!*s) break;
        if (n >= max) break;
        while (*s && *s != ' ' && *s != '\t' && *s != ',') {
            if (k < 15) out[n][k++] = *s;
            s++;
        }
        out[n][k] = '\0';
        n++;
    }
    return n;
}

void csnhypodbp_chan_code(char *out, size_t n, const char *prefix, const char *comp)
{
    if (!out || n == 0) return;
    snprintf(out, n, "%s%s", prefix ? prefix : "", comp ? comp : "");
}

int csnhypodbp_channel_candidates(const char *channels_order,
                                  const char *components_order,
                                  char out[][8], int max_out)
{
    char chans[16][16], comps[16][16];
    int nch = csnhypodbp_split_ws(channels_order, chans, 16);
    int nco = csnhypodbp_split_ws(components_order, comps, 16);
    int i, j, n = 0;

    for (i = 0; i < nch; i++) {
        for (j = 0; j < nco; j++) {
            char code[8];
            csnhypodbp_chan_code(code, sizeof(code), chans[i], comps[j]);
            if (n < max_out) {
                strncpy(out[n], code, 7);
                out[n][7] = '\0';
            }
            n++;
        }
    }
    return n;
}

int csnhypodbp_channel_order(const char *channels_order,
                             const char *components_order,
                             const char *const *avail, int navail,
                             char out[][8], int max_out)
{
    char cand[64][8];
    int nc = csnhypodbp_channel_candidates(channels_order, components_order, cand, 64);
    int i, a, n = 0;

    for (i = 0; i < nc; i++) {
        for (a = 0; a < navail; a++) {
            if (avail[a] && strcmp(avail[a], cand[i]) == 0) {
                if (n < max_out) {
                    strncpy(out[n], cand[i], 7);
                    out[n][7] = '\0';
                }
                n++;
                break;
            }
        }
    }
    return n;
}

int csnhypodbp_station_needs_fetch(const DEV_STATION *arr, int n, const char *sta)
{
    int i, found = 0;
    if (!sta || !sta[0]) return 0;
    if (!arr || n <= 0) return 1;   /* sin filas: hay que intentarlo */
    for (i = 0; i < n; i++) {
        if (strcmp(arr[i].szStation, sta) != 0) continue;
        found = 1;
        if (!arr[i].fetched) return 1;   /* alguna fila sin intentar */
    }
    return found ? 0 : 1;   /* estacion sin filas -> intentar */
}

int csnhypodbp_station_has_data(const DEV_STATION *arr, int n,
                                const int *has_data, const char *sta)
{
    int i;
    if (!arr || !has_data || !sta || !sta[0]) return 0;
    for (i = 0; i < n; i++)
        if (strcmp(arr[i].szStation, sta) == 0 && has_data[i]) return 1;
    return 0;
}

int csnhypodbp_station_has_band(const DEV_STATION *arr, int n,
                                const char *sta, const char *code)
{
    int i;
    if (!arr || !sta || !code || !code[0]) return 0;
    for (i = 0; i < n; i++) {
        if (strcmp(arr[i].szStation, sta) != 0) continue;
        if (strncmp(arr[i].szChannel, code, 2) == 0) return 1;
    }
    return 0;
}

void csnhypodbp_trace_label(char *out, size_t outsz,
                            const char *station, const char *channel, double dist_km)
{
    const char *ch;
    if (!out || outsz == 0) return;
    ch = (channel && channel[0]) ? channel : ((station && station[0]) ? station : "?");
    if (dist_km > 0.0)
        snprintf(out, outsz, "%s (%.0f km)", ch, dist_km);
    else
        snprintf(out, outsz, "%s", ch);
}
