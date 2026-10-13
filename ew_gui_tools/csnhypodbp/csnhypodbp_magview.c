/*
 * csnhypodbp_magview.c - Traza de vista por magnitud (ver csnhypodbp_magview.h).
 * Sin GTK ni EarthWorm: se testea headless.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_magview.h"

void csnhypodbp_magview_init(CsnhypodbpMagView *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->kind = -1;
}

void csnhypodbp_magview_free(CsnhypodbpMagView *v)
{
    if (!v) return;
    free(v->v);
    csnhypodbp_magview_init(v);
}

double csnhypodbp_magview_at(const CsnhypodbpMagView *v, long i)
{
    if (!v || !v->v || i < 0 || i >= v->n) return NAN;
    return v->v[i];
}

double csnhypodbp_magview_margin(double t0, double t1)
{
    double w = (isfinite(t0) && isfinite(t1)) ? fabs(t1 - t0) : 0.0;

    return 20.0 + 0.25 * w;
}

int csnhypodbp_magview_frame(const CsnhypodbpView *view, double anchor,
                             double t0, double t1, double *win0, double *span)
{
    double w0, sp;

    if (!isfinite(anchor) || anchor <= 0.0) return 0;
    if (!isfinite(t0) || !isfinite(t1)) return 0;

    if (view && view->active && view->t1 > view->t0) {
        w0 = view->t0;
        sp = view->t1 - view->t0;
    } else {
        double m = csnhypodbp_magview_margin(t0, t1);

        w0 = anchor + t0 - m;
        sp = (anchor + t1 + m) - w0;
    }
    if (!isfinite(w0) || !isfinite(sp) || !(sp > 0.0)) return 0;
    if (win0) *win0 = w0;
    if (span) *span = sp;
    return 1;
}

const char *csnhypodbp_magview_cursor(int on_handle, int on_edge)
{
    if (on_handle) return "move";
    if (on_edge) return "ew-resize";
    return "crosshair";
}

static int view_alloc(CsnhypodbpMagView *v, long n)
{
    if (n <= 0 || n > CSNHYPODBP_MAGVIEW_MAX) return -1;
    v->v = malloc((size_t)n * sizeof(*v->v));
    if (!v->v) return -1;
    v->cap = n;
    v->n = n;
    return 0;
}

/* int32 (gaps INT32_MAX) -> double (gaps NaN). */
static void view_from_int32(CsnhypodbpMagView *v, const int32_t *w, long n)
{
    long i;

    for (i = 0; i < n; i++)
        v->v[i] = (w[i] == INT32_MAX) ? NAN : (double)w[i];
}

int csnhypodbp_magview_build(CsnhypodbpMagView *v, EwGuiTrace *tr, int kind,
                             const CsnhypodbpMagCfg *c, double t_start, double t_end)
{
    double rate, oldest;
    long len, i0, i1, n, i;
    int32_t *raw, *w = NULL, *dm = NULL, *dec = NULL;
    int factor = 1;

    if (!v || !tr || !c || kind < 0 || kind >= CSNHYPODBP_MAG_N) return -1;
    csnhypodbp_magview_free(v);
    if (!(t_end > t_start)) return -1;

    rate = ewgui_trace_rate(tr);
    oldest = ewgui_trace_oldest(tr);
    len = ewgui_trace_length(tr);
    raw = ewgui_trace_raw(tr);
    if (!raw || len <= 0 || !(rate > 0.0) || !isfinite(oldest)) return -1;

    i0 = (long)((t_start - oldest) * rate);
    i1 = (long)((t_end - oldest) * rate) + 1;
    if (i0 < 0) i0 = 0;
    if (i1 > len) i1 = len;
    if (i1 <= i0) return -1;
    n = i1 - i0;
    if (n > CSNHYPODBP_MAGVIEW_MAX) return -1;

    w = malloc((size_t)n * sizeof(*w));
    dm = malloc((size_t)n * sizeof(*dm));
    if (!w || !dm) { free(w); free(dm); return -1; }
    for (i = 0; i < n; i++) w[i] = raw[i0 + i];
    ewgui_demean(w, (size_t)n, dm);   /* dm = w - media (los gaps se conservan) */

    v->kind = kind;
    v->oldest = oldest + (double)i0 / rate;
    v->decim = 1;

    switch (kind) {
        case CSNHYPODBP_MAG_ML:
            ewgui_filter_wood_anderson(dm, (size_t)n, rate, c->wa_period, c->wa_damp,
                                       c->wa_gain);
            v->rate = rate;
            if (view_alloc(v, n) != 0) goto fail;
            view_from_int32(v, dm, n);
            break;

        case CSNHYPODBP_MAG_MB:
            ewgui_filter_bandpass(dm, (size_t)n, rate, c->mb_f1, c->mb_f2, 2);
            v->rate = rate;
            if (view_alloc(v, n) != 0) goto fail;
            view_from_int32(v, dm, n);
            break;

        case CSNHYPODBP_MAG_MWP: {
            double *ig;
            long k, m;

            factor = (int)(rate / CSNHYPODBP_MAGVIEW_MWP_SPS + 0.5);
            if (factor < 1) factor = 1;
            ig = malloc((size_t)n * sizeof(*ig));
            if (!ig) goto fail;
            ewgui_integrate(dm, (size_t)n, 1.0 / rate, ig);
            m = (n + factor - 1) / factor;
            if (view_alloc(v, m) != 0) { free(ig); goto fail; }
            for (k = 0; k < m; k++) v->v[k] = ig[k * factor];
            free(ig);
            v->rate = rate / factor;
            v->decim = factor;
            break;
        }

        case CSNHYPODBP_MAG_MS: {
            long m;

            factor = (int)(rate / CSNHYPODBP_MAGVIEW_MS_SPS + 0.5);
            if (factor < 1) factor = 1;
            dec = malloc((size_t)n * sizeof(*dec));
            if (!dec) goto fail;
            m = (long)ewgui_decimate(dm, (size_t)n, factor, dec);
            if (m <= 0) goto fail;
            ewgui_filter_bandpass(dec, (size_t)m, rate / factor, c->ms_f1, c->ms_f2, 2);
            if (view_alloc(v, m) != 0) goto fail;
            view_from_int32(v, dec, m);
            v->rate = rate / factor;
            v->decim = factor;
            break;
        }

        default:
            goto fail;
    }

    free(w);
    free(dm);
    free(dec);
    return 0;

fail:
    free(w);
    free(dm);
    free(dec);
    csnhypodbp_magview_free(v);
    return -1;
}
