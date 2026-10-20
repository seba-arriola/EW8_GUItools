/******************************************************************************
 * csnhypodbp_trace.c                                                          *
 *                                                                             *
 * Implementacion pura (sin GTK) del parseo de TYPE_PICK_SCNL, la captura por  *
 * estacion y la seleccion de la referencia P. Ver csnhypodbp_trace.h.         *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "csnhypodbp_trace.h"

int csnhypodbp_pick_parse(const char *msg, CsnhypodbpTracePick *out)
{
    if (!msg || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    out->phase  = 'P';
    out->origin = 'A';

    int type, mod, inst, seq;
    char scnl[64], fmwt[16], ts[32];
    if (sscanf(msg, "%d %d %d %d %63s %15s %31s",
               &type, &mod, &inst, &seq, scnl, fmwt, ts) < 7)
        return 0;
    (void)type; (void)mod; (void)inst; (void)seq; (void)fmwt;

    char net[16], loc[16];
    if (sscanf(scnl, "%15[^.].%15[^.].%15[^.].%15s",
               out->sta, out->chan, net, loc) < 3)
        return 0;

    for (int k = 0; ts[k]; k++)
        if (ts[k] == ',')
            ts[k] = '.';

    int py, pmo, pd, phh, pmn;
    double psec;
    if (sscanf(ts, "%4d%2d%2d%2d%2d%lf", &py, &pmo, &pd, &phh, &pmn, &psec) != 6)
        return 0;

    struct tm pt;
    memset(&pt, 0, sizeof(pt));
    pt.tm_year = py - 1900;
    pt.tm_mon  = pmo - 1;
    pt.tm_mday = pd;
    pt.tm_hour = phh;
    pt.tm_min  = pmn;
    pt.tm_sec  = (int)psec;

    /* El mensaje lleva la hora en UTC; mktime la resuelve con TZ=GMT. */
    char *otz = getenv("TZ");
    setenv("TZ", "GMT", 1);
    tzset();
    out->t = (double)mktime(&pt) + (psec - (int)psec);
    if (otz) setenv("TZ", otz, 1);
    else     unsetenv("TZ");
    tzset();

    /* Tokens 11 (fase) y 12 (origen), opcionales. */
    char phase_tok[8] = "", origin_tok[8] = "";
    sscanf(msg, "%*d %*d %*d %*d %*63s %*15s %*31s %*d %*d %*d %7s %7s",
           phase_tok, origin_tok);
    out->phase  = (phase_tok[0] == 'S' || phase_tok[0] == 's') ? 'S' : 'P';
    out->origin = (origin_tok[0] == 'M' || origin_tok[0] == 'm') ? 'M' : 'A';
    return 1;
}

void csnhypodbp_cap_init(CsnhypodbpPickCap *cap)
{
    if (!cap)
        return;
    memset(cap, 0, sizeof(*cap));
}

void csnhypodbp_cap_add(CsnhypodbpPickCap *cap, const CsnhypodbpTracePick *p)
{
    if (!cap || !p)
        return;
    cap->picks[cap->next] = *p;
    cap->next = (cap->next + 1) % CSNHYPODBP_CAP_MAX;
    if (cap->n < CSNHYPODBP_CAP_MAX)
        cap->n++;
}

static char pick_phase(const CsnhypodbpTracePick *p)
{
    return (p->phase == 'S' || p->phase == 's') ? 'S' : 'P';
}

int csnhypodbp_cap_find(const CsnhypodbpPickCap *cap, const char *sta,
                        char phase, double t0, double window_sec,
                        CsnhypodbpTracePick *out)
{
    if (!cap || !sta || !out || cap->n <= 0)
        return 0;
    int    best = -1;
    double best_dt = 1e30;
    for (int k = 0; k < cap->n; k++) {
        const CsnhypodbpTracePick *p = &cap->picks[k];
        if (!p->sta[0] || strcmp(p->sta, sta))
            continue;
        if (pick_phase(p) != phase)
            continue;
        double dt = fabs(p->t - t0);
        if (dt > window_sec)
            continue;
        if (dt < best_dt) { best_dt = dt; best = k; }
    }
    if (best < 0)
        return 0;
    *out = cap->picks[best];
    return 1;
}

int csnhypodbp_cap_find_all(const CsnhypodbpPickCap *cap, const char *sta,
                            char phase, double t0, double window_sec,
                            CsnhypodbpTracePick *out, int max_out)
{
    if (!cap || !sta || !out || max_out <= 0 || cap->n <= 0)
        return 0;
    int count = 0;
    for (int k = 0; k < cap->n && count < max_out; k++) {
        const CsnhypodbpTracePick *p = &cap->picks[k];
        if (!p->sta[0] || strcmp(p->sta, sta))
            continue;
        if (pick_phase(p) != phase)
            continue;
        if (fabs(p->t - t0) > window_sec)
            continue;
        out[count++] = *p;
    }
    return count;
}

int csnhypodbp_cap_window(const CsnhypodbpPickCap *cap, const char *sta,
                          double t0, double window_sec,
                          CsnhypodbpTracePick *out, int max_out)
{
    if (!cap || !sta || !out || max_out <= 0 || cap->n <= 0)
        return 0;
    int count = 0;
    for (int k = 0; k < cap->n && count < max_out; k++) {
        const CsnhypodbpTracePick *p = &cap->picks[k];
        if (!p->sta[0] || strcmp(p->sta, sta))
            continue;
        if (fabs(p->t - t0) > window_sec)
            continue;
        out[count++] = *p;
    }
    return count;
}

int csnhypodbp_ref_pick(const CsnhypodbpTracePick *p, int n,
                        double theo_p, double *ref_t){
    for (int k = 0; k < n; k++) {
        if (pick_phase(&p[k]) == 'P') {
            if (ref_t) *ref_t = p[k].t;
            return 1;
        }
    }
    if (ref_t) *ref_t = theo_p;
    return 0;
}

CsnhypodbpPickOrigin csnhypodbp_pick_origin(char origin)
{
    if (origin == 'M')
        return CSNHYPODBP_PICK_MANUAL;
    if (origin == 'T')
        return CSNHYPODBP_PICK_THEO;
    return CSNHYPODBP_PICK_AUTO;
}

double csnhypodbp_color_tint_channel(double c, double frac)
{
    if (c < 0.0) c = 0.0;
    if (c > 1.0) c = 1.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return c + (1.0 - c) * frac;
}

void csnhypodbp_color_tint(const double base[3], double frac, double out[3])
{
    int k;
    if (!base || !out)
        return;
    for (k = 0; k < 3; k++)
        out[k] = csnhypodbp_color_tint_channel(base[k], frac);
}

void csnhypodbp_color_set_init(CsnhypodbpColorSet *set, const double base[3])
{
    if (!set)
        return;
    memset(set, 0, sizeof(*set));
    if (base)
        memcpy(set->manual, base, sizeof(set->manual));
}

void csnhypodbp_color_set_override(CsnhypodbpColorSet *set,
                                   CsnhypodbpPickOrigin o, const double rgb[3])
{
    if (!set || !rgb)
        return;
    if (o == CSNHYPODBP_PICK_MANUAL) {
        memcpy(set->manual, rgb, sizeof(set->manual));
    } else if (o == CSNHYPODBP_PICK_AUTO) {
        memcpy(set->auto_rgb, rgb, sizeof(set->auto_rgb));
        set->has_auto = 1;
    } else {
        memcpy(set->theo_rgb, rgb, sizeof(set->theo_rgb));
        set->has_theo = 1;
    }
}

void csnhypodbp_color_resolve(const CsnhypodbpColorSet *set,
                              CsnhypodbpPickOrigin o, double rgb[3])
{
    if (!rgb)
        return;
    if (!set) {
        rgb[0] = rgb[1] = rgb[2] = 0.0;
        return;
    }
    if (o == CSNHYPODBP_PICK_MANUAL) {
        memcpy(rgb, set->manual, sizeof(set->manual));
        return;
    }
    if (o == CSNHYPODBP_PICK_AUTO && set->has_auto) {
        memcpy(rgb, set->auto_rgb, sizeof(set->auto_rgb));
        return;
    }
    if (o == CSNHYPODBP_PICK_THEO && set->has_theo) {
        memcpy(rgb, set->theo_rgb, sizeof(set->theo_rgb));
        return;
    }
    csnhypodbp_color_tint(set->manual,
                          (o == CSNHYPODBP_PICK_AUTO) ? CSNHYPODBP_LIGHTEN_AUTO
                                                      : CSNHYPODBP_LIGHTEN_THEO,
                          rgb);
}

void csnhypodbp_color_to_hex(const double rgb[3], char out[7])
{
    static const char hex[] = "0123456789ABCDEF";
    int k;
    if (!out)
        return;
    if (!rgb) {
        memcpy(out, "000000", 7);
        return;
    }
    for (k = 0; k < 3; k++) {
        double c = rgb[k];
        int v;
        if (c < 0.0) c = 0.0;
        if (c > 1.0) c = 1.0;
        /* El epsilon evita que 0.7*255 (0.69999999999999996) trunque a 178:
           el redondeo debe ser half-up sobre el valor decimal esperado. */
        v = (int)(c * 255.0 + 0.5 + 1e-9);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out[2 * k]     = hex[v >> 4];
        out[2 * k + 1] = hex[v & 0x0F];
    }
    out[6] = '\0';
}

void csnhypodbp_trace_rgb(double rgb[3])
{
    if (!rgb)
        return;
    /* Politica: la traza del picking SIEMPRE es negra; el origen de la picada
       se distingue por el color de su marca (csnhypodbp_color_resolve). */
    rgb[0] = 0.0; rgb[1] = 0.0; rgb[2] = 0.0;
}

char csnhypodbp_phase_from_char(char c)
{
    return (c == 'S' || c == 's') ? 'S' : 'P';
}
