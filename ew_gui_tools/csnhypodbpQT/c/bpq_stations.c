/*
 * bpq_stations.c - Catalogo de estaciones y tabla de picadas del picking.
 *
 * Dos piezas:
 *
 *   1. El catalogo. LoadStationsFromFile() vive en csnhypodbp_ws.c, que no es
 *      puro (incluye csnhypodbp.h con GTK) y llena globales (StaArray /
 *      NumEstaciones). Aqui se relee StaFile en un catalogo propio, con el mismo
 *      formato del repo:  <sta> <net> <chan> <loc> <lat> <lon> [elev] [gain]
 *
 *   2. La tabla. La regla de precedencia M > A > Teo y el emparejamiento de las
 *      picadas con cada estacion los hace csnhypodbp_statable_build (puro): esta
 *      capa solo arma los CsnhypodbpStaInput (distancia haversine en grados, como
 *      csnhypodbp_ws.c:1321-1326) y ordena las filas por distancia.
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_statable.h"
#include "csnhypodbp_station.h"
#include "csnhypodbp_tt.h"

typedef struct {
    char   sta[16];
    char   net[8];
    char   chan[16];
    char   loc[8];
    double lat, lon;
} BpqSta;

struct BpqStations {
    BpqSta *v;
    int     n;
};

/* Distancia epicentral en grados (haversine). 0 si falta alguna coordenada. */
static double dist_deg(double lat1, double lon1, double lat2, double lon2)
{
    const double d2r = M_PI / 180.0;
    double p1, p2, dp, dl, a;

    if (!isfinite(lat1) || !isfinite(lon1) || !isfinite(lat2) || !isfinite(lon2))
        return 0.0;

    p1 = lat1 * d2r;
    p2 = lat2 * d2r;
    dp = (lat2 - lat1) * d2r;
    dl = (lon2 - lon1) * d2r;
    a = sin(dp / 2.0) * sin(dp / 2.0) +
        cos(p1) * cos(p2) * sin(dl / 2.0) * sin(dl / 2.0);
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return 2.0 * atan2(sqrt(a), sqrt(1.0 - a)) / d2r;
}

/* --- catalogo ------------------------------------------------------------- */

BpqStations *bpq_stations_open(const BpqConfig *cfg, char *err, size_t errn)
{
    BpqStations *c;
    FILE *f;
    char line[512];
    int cap = 0;

    if (err && errn) err[0] = '\0';
    c = (BpqStations *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (!cfg || !cfg->sta_file[0]) {
        if (err) snprintf(err, errn, "sin StaFile en el .d");
        return c;
    }

    f = fopen(cfg->sta_file, "r");
    if (!f) {
        if (err) snprintf(err, errn, "no se pudo abrir StaFile <%s>", cfg->sta_file);
        return c;
    }

    while (fgets(line, sizeof(line), f)) {
        char sta[32] = "", net[16] = "", chan[16] = "", loc[16] = "";
        double lat = 0.0, lon = 0.0, elev = 0.0, gain = 0.0;
        const int nf = sscanf(line, "%31s %15s %15s %15s %lf %lf %lf %lf",
                              sta, net, chan, loc, &lat, &lon, &elev, &gain);

        if (nf < 4 || sta[0] == '#' || sta[0] == '\0') continue;
        if (nf < 6) { lat = 0.0; lon = 0.0; }   /* formato legacy sin lat/lon */

        if (c->n == cap) {
            const int nc = cap ? cap * 2 : 128;
            BpqSta *t = (BpqSta *)realloc(c->v, (size_t)nc * sizeof(*t));
            if (!t) break;
            c->v = t;
            cap = nc;
        }
        memset(&c->v[c->n], 0, sizeof(c->v[c->n]));
        snprintf(c->v[c->n].sta, sizeof(c->v[c->n].sta), "%s", sta);
        snprintf(c->v[c->n].net, sizeof(c->v[c->n].net), "%s", net);
        snprintf(c->v[c->n].chan, sizeof(c->v[c->n].chan), "%s", chan);
        snprintf(c->v[c->n].loc, sizeof(c->v[c->n].loc), "%s", loc);
        c->v[c->n].lat = lat;
        c->v[c->n].lon = lon;
        c->n++;
    }
    fclose(f);

    if (err && c->n == 0)
        snprintf(err, errn, "StaFile <%s> sin estaciones validas", cfg->sta_file);
    return c;
}

void bpq_stations_close(BpqStations *c)
{
    if (!c) return;
    free(c->v);
    free(c);
}

int bpq_stations_count(const BpqStations *c)
{
    return c ? c->n : 0;
}

/* --- tabla ---------------------------------------------------------------- */

int bpq_stations_scnl(const BpqStations *c, int idx, const char **sta,
                      const char **net, const char **chan, const char **loc)
{
    if (!c || !c->v || idx < 0 || idx >= c->n) return 0;
    if (sta) *sta = c->v[idx].sta;
    if (net) *net = c->v[idx].net;
    if (chan) *chan = c->v[idx].chan;
    if (loc) *loc = c->v[idx].loc;
    return 1;
}

/* `sol` aporta las picadas (tiempos y origen); `resid` la fuente de residuales.
 * Normalmente son la misma; tras un `locate` la elegida manda las picadas y la
 * candidata los residuales (csnhypodbp_resid_source del nucleo). */
int bpq_stations_build_resid(const BpqStations *c, const CsnhypodbpSolution *sol,
                             const CsnhypodbpSolution *resid,
                             const CsnhypodbpManualPick *man, int nman,
                             CsnhypodbpStaRow *out, int max)
{
    int i, n = 0;
    BpqTtTheo theo;

    if (!c || !c->v || !out || max <= 0) return 0;

    /* La banda Potin del evento se elige UNA vez, fuera del bucle. */
    memset(&theo, 0, sizeof(theo));
    if (sol) bpq_tt_theo_prepare(&theo, sol->lat, sol->lon, sol->depth);

    for (i = 0; i < c->n && n < max; i++) {
        CsnhypodbpStaInput si;
        CsnhypodbpStaRow row;

        memset(&si, 0, sizeof(si));
        memset(&row, 0, sizeof(row));

        snprintf(si.sta, sizeof(si.sta), "%s", c->v[i].sta);
        snprintf(si.chan, sizeof(si.chan), "%s", c->v[i].chan);
        snprintf(si.net, sizeof(si.net), "%s", c->v[i].net);
        snprintf(si.loc, sizeof(si.loc), "%s", c->v[i].loc);
        si.idx = i;
        si.has_data = 0;                    /* lo fija el fetch de ondas (fase 4) */
        si.dist_deg = sol ? dist_deg(sol->lat, sol->lon, c->v[i].lat, c->v[i].lon)
                          : 0.0;
        si.caps = NULL;                     /* sin anillo: manda la solucion */
        si.ncap = 0;
        si.sol = sol ? sol->picks : NULL;
        si.nsol = sol ? sol->npicks : 0;
        si.theo_p = 0.0;
        if (sol) {
            /* Modelo de capas de hyp2000 dentro del dominio regional; fuera de
             * el (o si no cargo) sigue mandando IASP91. */
            double tp = bpq_tt_theo_p(&theo, si.dist_deg, sol->depth);

            if (!(tp > 0.0) && csnhypodbp_tt_ready())
                tp = csnhypodbp_tt_p(si.dist_deg, sol->depth);
            if (tp > 0.0) si.theo_p = sol->otime + tp;
        }

        if (csnhypodbp_statable_build(&row, 1, &si, 1, man, nman, resid) <= 0)
            continue;
        out[n++] = row;
    }

    /* Orden por distancia (insercion: el catalogo es de ~100 estaciones). */
    for (i = 1; i < n; i++) {
        const CsnhypodbpStaRow tmp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].dist_km > tmp.dist_km) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = tmp;
    }
    return n;
}

int bpq_stations_build(const BpqStations *c, const CsnhypodbpSolution *sol,
                       CsnhypodbpStaRow *out, int max)
{
    return bpq_stations_build_resid(c, sol, sol, NULL, 0, out, max);
}

/* Una fila por CANAL de `sta`, en el orden del .d. La disponibilidad es por
 * banda (`HH`): el catalogo trae la vertical y las horizontales se derivan del
 * prefijo, igual que csnhypodbp_station_has_band en la GTK. */
int bpq_stations_chan_rows(const BpqStations *c, const char *sta,
                           const char *channels_order,
                           const char *components_order,
                           const CsnhypodbpStaRow *base,
                           CsnhypodbpStaRow *out, char chans[][8], int max)
{
    char cand[64][8];
    int nc, k, n = 0, base_idx = -1;

    if (!c || !c->v || !sta || !base || !out || !chans || max <= 0) return 0;

    /* Indice del catalogo para net/loc (el primero de esa estacion). */
    for (int i = 0; i < c->n; i++) {
        if (strcmp(c->v[i].sta, sta) == 0) {
            base_idx = i;
            break;
        }
    }
    if (base_idx < 0) {          /* estacion fuera del catalogo: la fila tal cual */
        out[0] = *base;
        snprintf(chans[0], 8, "%s", base->p.present ? base->p.chan : "");
        return 1;
    }

    nc = csnhypodbp_channel_candidates(channels_order, components_order, cand, 64);
    for (k = 0; k < nc && n < max; k++) {
        int band_ok = 0;
        int i;

        /* Banda = los dos primeros caracteres del candidato ("HHZ" -> "HH"). */
        for (i = 0; i < c->n; i++) {
            if (strcmp(c->v[i].sta, sta) != 0) continue;
            if (strncmp(c->v[i].chan, cand[k], 2) == 0) {
                band_ok = 1;
                break;
            }
        }
        if (!band_ok) continue;
        out[n] = *base;
        out[n].idx = base_idx;
        snprintf(chans[n], 8, "%s", cand[k]);
        n++;
    }
    if (n == 0) {               /* el .d no casa con la banda: al menos la base */
        out[0] = *base;
        snprintf(chans[0], 8, "%s", base->p.present ? base->p.chan : "");
        return 1;
    }
    return n;
}
