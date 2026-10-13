/*
 * csnhypodbp_magrow.c - Modelo puro de la tabla de magnitudes (ver
 * csnhypodbp_magrow.h). Sin GTK ni EarthWorm: se testea headless.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magrow.h"

static void copy_field(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, n, "%s", src);
}

int csnhypodbp_magrow_split_nslc(const char *nslc, char *net, size_t nnet,
                                 char *sta, size_t nsta, char *loc, size_t nloc,
                                 char *chan, size_t nchan)
{
    char buf[128];
    char *f[4];
    char *p;
    int i = 0;

    if (net && nnet) net[0] = '\0';
    if (sta && nsta) sta[0] = '\0';
    if (loc && nloc) loc[0] = '\0';
    if (chan && nchan) chan[0] = '\0';
    if (!nslc || !nslc[0]) return 0;
    copy_field(buf, sizeof(buf), nslc);
    p = buf;
    while (i < 4 && p) {
        char *dot = strchr(p, '.');
        f[i++] = p;
        if (dot) { *dot = '\0'; p = dot + 1; }
        else p = NULL;
    }
    if (i != 4) return 0;
    copy_field(net, nnet, f[0]);
    copy_field(sta, nsta, f[1]);
    copy_field(loc, nloc, f[2]);
    copy_field(chan, nchan, f[3]);
    return 1;
}

/* Valor de esa estacion (por NOMBRE; gana el primero si hay repetidos). */
static const CsnhypodbpMagIn *find_val(const CsnhypodbpMagIn *vals, int nval,
                                       const char *sta)
{
    int i;

    if (!vals || !sta || !sta[0]) return NULL;
    for (i = 0; i < nval; i++)
        if (strcmp(vals[i].sta, sta) == 0) return &vals[i];
    return NULL;
}

int csnhypodbp_magrow_build(CsnhypodbpMagRow *out, int max,
                            const CsnhypodbpStaRow *stas, int nsta,
                            const CsnhypodbpMagIn *vals, int nval,
                            double red, double t0, double t1)
{
    int i, n = 0;

    if (!out || max <= 0 || !stas) return 0;
    for (i = 0; i < nsta && n < max; i++) {
        CsnhypodbpMagRow *r = &out[n];
        const CsnhypodbpMagIn *v;

        memset(r, 0, sizeof(*r));
        copy_field(r->sta, sizeof(r->sta), stas[i].sta);
        r->dist_km = stas[i].dist_km;
        r->mag = NAN; r->res = NAN; r->amp = NAN; r->period = NAN;
        r->has_data = stas[i].has_data ? 1 : 0;
        /* La puerta de distancia y el filtro A/M/Teo los aplica el llamador
           (rebuild_mag_list), que es quien conoce la magnitud activa. */
        r->in_range = 1;
        r->eligible = r->has_data;
        r->win_src = '\0';
        /* Sin traza no hay nada que medir ni que forzar. */
        r->used = r->has_data;
        r->anchor = stas[i].p.present ? stas[i].p.origin : '\0';
        r->anchor_t = (stas[i].p.present && stas[i].p.t > 0.0) ? stas[i].p.t : 0.0;
        r->dist_deg = stas[i].dist_deg;
        r->t0 = isfinite(t0) ? t0 : NAN;
        r->t1 = isfinite(t1) ? t1 : NAN;
        v = find_val(vals, nval, r->sta);
        if (v) {
            copy_field(r->chan, sizeof(r->chan), v->chan);
            r->mag = v->mag;
            r->amp = v->amp;
            r->period = v->period;
            r->manual = v->manual;
            if (isfinite(v->mag) && isfinite(red)) r->res = v->mag - red;
        }
        n++;
    }
    return n;
}

int csnhypodbp_magrow_from_db(CsnhypodbpMagIn *out, int max,
                              const CsnhypodbpMagDbRow *rows, int n)
{
    int i, k = 0;

    if (!out || max <= 0 || !rows) return 0;
    for (i = 0; i < n && k < max; i++) {
        char net[16], sta[CSNHYPODBP_MAG_STA_LEN], loc[16];
        char chan[CSNHYPODBP_MAG_CHAN_LEN];

        if (!csnhypodbp_magrow_split_nslc(rows[i].channel, net, sizeof(net), sta,
                                          sizeof(sta), loc, sizeof(loc), chan,
                                          sizeof(chan)))
            continue;
        memset(&out[k], 0, sizeof(out[k]));
        copy_field(out[k].sta, sizeof(out[k].sta), sta);
        copy_field(out[k].chan, sizeof(out[k].chan), chan);
        out[k].mag = (rows[i].calculated_magnitude != 0.0)
                         ? rows[i].calculated_magnitude : NAN;
        out[k].amp = (rows[i].read_amplitude != 0.0) ? rows[i].read_amplitude : NAN;
        out[k].period = NAN;
        k++;
    }
    return k;
}

void csnhypodbp_magrow_mag_text(const CsnhypodbpMagRow *r, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!r || !isfinite(r->mag)) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%.2f", r->mag);
}

void csnhypodbp_magrow_res_text(const CsnhypodbpMagRow *r, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!r || !isfinite(r->res)) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%+.2f", r->res);
}

const char *csnhypodbp_magrow_stat_text(const CsnhypodbpMagRow *r)
{
    if (!r || !isfinite(r->mag)) return "-";
    return r->manual ? "M" : "A";
}

const char *csnhypodbp_magrow_anchor_text(const CsnhypodbpMagRow *r)
{
    if (!r) return "-";
    switch (r->anchor) {
        case 'M': return "M";
        case 'A': return "A";
        case 'T': return "Teo";
        default:  return "-";
    }
}

/* Offset en segundos respecto del ancla, con signo ("+10", "+400"). */
static void magrow_off_text(double v, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!isfinite(v)) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%+.0f", v);
}

void csnhypodbp_magrow_t0_text(const CsnhypodbpMagRow *r, char *out, size_t n)
{
    magrow_off_text(r ? r->t0 : NAN, out, n);
}

void csnhypodbp_magrow_t1_text(const CsnhypodbpMagRow *r, char *out, size_t n)
{
    magrow_off_text(r ? r->t1 : NAN, out, n);
}

const char *csnhypodbp_magrow_vent_text(const CsnhypodbpMagRow *r)
{
    if (!r) return "-";
    switch (r->win_src) {
        case 'M': return "M";
        case 'A': return "A";
        case 'T': return "Teo";
        default:  return "-";
    }
}

void csnhypodbp_magrow_counts(const CsnhypodbpMagRow *rows, int n, int *con_dato,
                              int *fuera, int *filtradas, int *enuso)
{
    int i, cd = 0, fu = 0, fi = 0, us = 0;

    for (i = 0; i < n && rows; i++) {
        if (isfinite(rows[i].mag)) cd++;
        if (!rows[i].in_range) fu++;
        else if (rows[i].has_data && !rows[i].eligible) fi++;
        if (rows[i].used) us++;
    }
    if (con_dato) *con_dato = cd;
    if (fuera) *fuera = fu;
    if (filtradas) *filtradas = fi;
    if (enuso) *enuso = us;
}
