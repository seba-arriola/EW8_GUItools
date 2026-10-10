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

int csnhypodbp_ref_pick(const CsnhypodbpTracePick *p, int n,
                        double theo_p, double *ref_t)
{
    for (int k = 0; k < n; k++) {
        if (pick_phase(&p[k]) == 'P') {
            if (ref_t) *ref_t = p[k].t;
            return 1;
        }
    }
    if (ref_t) *ref_t = theo_p;
    return 0;
}

void csnhypodbp_trace_rgb(int has_p_pick, double rgb[3])
{
    if (!rgb)
        return;
    if (has_p_pick) { rgb[0] = 0.0; rgb[1] = 0.0; rgb[2] = 0.0; }
    else            { rgb[0] = 0.7; rgb[1] = 0.7; rgb[2] = 0.7; }
}

char csnhypodbp_phase_from_char(char c)
{
    return (c == 'S' || c == 's') ? 'S' : 'P';
}
