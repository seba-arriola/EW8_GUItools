#include "csnhypodbp_view.h"

#include "ewgui/config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void csnhypodbp_view_set_default(CsnhypodbpView *v, double span)
{
    if (!v) return;
    v->def_span = (span > 0.0) ? span : 1.0;
    v->t0 = 0.0;
    v->t1 = v->def_span;
    v->active = 0;
    v->have_anchor = 0;
    v->anchor = 0.0;
}

CsnhypodbpAct csnhypodbp_view_click(CsnhypodbpView *v, double rel)
{
    double d;
    if (!v) return CSNHYPODBP_ACT_NONE;

    if (!v->have_anchor) {
        v->anchor = rel;
        v->have_anchor = 1;
        return CSNHYPODBP_ACT_ANCHOR;
    }

    d = rel - v->anchor;
    if (d >= CSNHYPODBP_ZOOM_MIN_SPAN) {
        v->t0 = v->anchor;
        v->t1 = rel;
        v->active = 1;
        v->have_anchor = 0;
        return CSNHYPODBP_ACT_ZOOM;
    }
    if (d <= 0.0) {
        v->t0 = 0.0;
        v->t1 = v->def_span;
        v->active = 0;
        v->have_anchor = 0;
        return CSNHYPODBP_ACT_RESET;
    }
    /* Casi encima (0 < d < MIN): ignorar y conservar el ancla. */
    return CSNHYPODBP_ACT_NONE;
}

double csnhypodbp_view_rel_to_x(const CsnhypodbpView *v, double rel,
                                int margin_left, int draw_width)
{
    double span;
    if (!v || draw_width <= 0) return margin_left;
    span = v->t1 - v->t0;
    if (span <= 0.0) return margin_left;
    return margin_left + ((rel - v->t0) / span) * draw_width;
}

double csnhypodbp_view_x_to_rel(const CsnhypodbpView *v, double x,
                                int margin_left, int draw_width)
{
    double span;
    if (!v || draw_width <= 0) return 0.0;
    span = v->t1 - v->t0;
    if (span <= 0.0) return 0.0;
    return v->t0 + ((x - margin_left) / (double)draw_width) * span;
}

const char *csnhypodbp_view_cursor(int in_plot)
{
    return in_plot ? "crosshair" : "default";
}

int csnhypodbp_view_reanchor_on(CsnhypodbpAct act)
{
    return act == CSNHYPODBP_ACT_RESET ? 1 : 0;
}

double csnhypodbp_view_ref_pick(int have_p, double p_t, double fallback)
{
    if (have_p && p_t > 1.0) return p_t;
    return fallback;
}

double csnhypodbp_view_mark_x(const CsnhypodbpView *v, double rel, int margin_left,
                              int draw_width, int *edge)
{
    double span;

    if (edge) *edge = 0;
    if (!v || draw_width <= 0) return (double)margin_left;
    span = v->t1 - v->t0;
    if (span <= 0.0) return (double)margin_left;
    if (rel < v->t0) { if (edge) *edge = -1; return (double)margin_left + 2.0; }
    if (rel > v->t1) { if (edge) *edge = 1;  return (double)(margin_left + draw_width - 2); }
    return margin_left + ((rel - v->t0) / span) * draw_width;
}

double csnhypodbp_view_time_zoom(CsnhypodbpView *v, int dir)
{
    double span, center, ns, maxs;
    if (!v) return 0.0;
    if (dir == 0) {
        csnhypodbp_view_set_default(v, v->def_span);
        return v->def_span;
    }
    span = v->t1 - v->t0;
    if (span <= 0.0) span = v->def_span;
    center = v->t0 + span / 2.0;             /* ancla: centro de la ventana */
    ns = (dir > 0) ? span * CSNHYPODBP_ZOOM_STEP : span / CSNHYPODBP_ZOOM_STEP;
    if (ns < CSNHYPODBP_ZOOM_MIN_SPAN) ns = CSNHYPODBP_ZOOM_MIN_SPAN;
    maxs = CSNHYPODBP_TIME_SPAN_MAX;
    if (maxs < v->def_span) maxs = v->def_span;   /* nunca menos que el default */
    if (ns > maxs) ns = maxs;
    v->t0 = center - ns / 2.0;
    v->t1 = center + ns / 2.0;
    v->active = (v->t0 != 0.0 || v->t1 != v->def_span) ? 1 : 0;
    v->have_anchor = 0;
    return ns;
}

void csnhypodbp_view_drag_begin(CsnhypodbpAxisDrag *d, double rel)
{
    if (!d) return;
    d->active = 1;
    d->anchor_rel = rel;
    d->cur_rel = rel;
}

void csnhypodbp_view_drag_update(CsnhypodbpAxisDrag *d, double rel)
{
    if (!d || !d->active) return;
    d->cur_rel = rel;
}

CsnhypodbpAct csnhypodbp_view_drag_end(CsnhypodbpAxisDrag *d, CsnhypodbpView *v)
{
    double dd;
    if (!d || !v || !d->active) return CSNHYPODBP_ACT_NONE;
    d->active = 0;
    dd = d->cur_rel - d->anchor_rel;
    if (dd >= CSNHYPODBP_ZOOM_MIN_SPAN) {          /* izq->der: zoom al rango */
        v->t0 = d->anchor_rel;
        v->t1 = d->cur_rel;
        v->active = 1;
        v->have_anchor = 0;
        return CSNHYPODBP_ACT_ZOOM;
    }
    if (dd <= -CSNHYPODBP_ZOOM_MIN_SPAN) {         /* der->izq: reset */
        v->t0 = 0.0;
        v->t1 = v->def_span;
        v->active = 0;
        v->have_anchor = 0;
        return CSNHYPODBP_ACT_RESET;
    }
    return CSNHYPODBP_ACT_NONE;                    /* arrastre demasiado corto */
}

int csnhypodbp_view_axis_band_x(const CsnhypodbpView *v, const CsnhypodbpAxisDrag *d,
                                int margin_left, int draw_width,
                                double *xa, double *xb)
{
    double a, b, lo, hi, t;
    if (!v || !d || !d->active || draw_width <= 0) return 0;
    a = csnhypodbp_view_rel_to_x(v, d->anchor_rel, margin_left, draw_width);
    b = csnhypodbp_view_rel_to_x(v, d->cur_rel, margin_left, draw_width);
    lo = (double)margin_left;
    hi = (double)(margin_left + draw_width);
    if (a < lo) a = lo;
    if (a > hi) a = hi;
    if (b < lo) b = lo;
    if (b > hi) b = hi;
    if (a > b) { t = a; a = b; b = t; }
    if (xa) *xa = a;
    if (xb) *xb = b;
    return 1;
}

int csnhypodbp_view_use_polyline(double span, int ncol, double rate)
{
    double spp;
    if (span <= 0.0 || ncol <= 0 || rate <= 0.0) return 0;
    spp = (span / (double)ncol) * rate;   /* muestras por pixel */
    return (spp < CSNHYPODBP_POLYLINE_SPP) ? 1 : 0;
}

double csnhypodbp_view_min_half(double scale, double line_px)
{
    if (scale <= 0.0 || line_px <= 0.0) return 0.0;
    return 0.5 * line_px / scale;   /* media altura, en unidades de datos */
}

int csnhypodbp_view_band_runs(const int *has, int ncol, int *runs, int max_runs)
{
    int px, n = 0;
    if (!has || ncol <= 0) return 0;
    for (px = 0; px < ncol; ) {
        if (!has[px]) { px++; continue; }
        if (n >= max_runs) return -1;
        if (runs) runs[2 * n] = px;
        while (px < ncol && has[px]) px++;
        if (runs) runs[2 * n + 1] = px;
        n++;
    }
    return n;
}

double csnhypodbp_view_sample_x(double t_abs, double win0, double span, int ncol)
{
    if (span <= 0.0 || ncol <= 0) return 0.0;
    return ((t_abs - win0) / span) * (double)ncol;
}

double csnhypodbp_view_tick_step(double span, int max_ticks)
{
    double raw, mag, norm;
    if (span <= 0.0 || max_ticks <= 0) return 0.0;
    raw = span / max_ticks;
    mag = pow(10.0, floor(log10(raw)));
    norm = raw / mag;
    if (norm <= 1.0)      norm = 1.0;
    else if (norm <= 2.0) norm = 2.0;
    else if (norm <= 5.0) norm = 5.0;
    else                  norm = 10.0;
    return norm * mag;
}

int csnhypodbp_view_ticks(double t0, double t1, int max_ticks,
                          double *out, int out_max)
{
    double step, first, t;
    int n = 0;
    if (!out || out_max <= 0 || t1 <= t0) return 0;
    step = csnhypodbp_view_tick_step(t1 - t0, max_ticks);
    if (step <= 0.0) return 0;
    first = ceil(t0 / step) * step;
    for (t = first; t <= t1 + 1e-9; t += step) {
        if (n >= out_max) break;
        out[n++] = t;
    }
    return n;
}

void csnhypodbp_view_fmt_utc(double t_abs, char *buf, size_t n)
{
    time_t tt;
    struct tm tmv;
    if (!buf || n == 0) return;
    tt = (time_t)floor(t_abs);
    if (gmtime_r(&tt, &tmv))
        snprintf(buf, n, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    else
        snprintf(buf, n, "--:--:--");
}

double csnhypodbp_zoom_step(double f, int dir)
{
    if (dir > 0)      f *= CSNHYPODBP_ZOOM_STEP;
    else if (dir < 0) f /= CSNHYPODBP_ZOOM_STEP;
    else              f = 1.0;
    if (f < CSNHYPODBP_ZOOM_FACTOR_MIN) f = CSNHYPODBP_ZOOM_FACTOR_MIN;
    if (f > CSNHYPODBP_ZOOM_FACTOR_MAX) f = CSNHYPODBP_ZOOM_FACTOR_MAX;
    return f;
}

int csnhypodbp_view_parse_color(const char *s, double rgb[3])
{
    double tmp[3];
    if (!rgb) return -1;
    if (ewgui_parse_hex_color(s, tmp) != 0) return -1;   /* conserva el default */
    rgb[0] = tmp[0];
    rgb[1] = tmp[1];
    rgb[2] = tmp[2];
    return 0;
}

int csnhypodbp_view_show_pick_cue(int canvas_is_live, int click_mode_pick,
                                  int cursor_valid)
{
    return (canvas_is_live && click_mode_pick && cursor_valid) ? 1 : 0;
}

int csnhypodbp_station_changed(const char *prev, const char *next)
{
    if (!prev) prev = "";
    if (!next) next = "";
    return strcmp(prev, next) != 0;
}

int csnhypodbp_solution_changed(int oq, int om, int ov, int nq, int nm, int nv)
{
    return (oq != nq || om != nm || ov != nv);
}

int csnhypodbp_station_row_to_select(const char *sel, const char *const *names, int n)
{
    int i;
    if (!sel || !sel[0] || !names || n <= 0) return -1;
    for (i = 0; i < n; i++)
        if (names[i] && strcmp(names[i], sel) == 0) return i;
    return -1;
}

int csnhypodbp_should_autoselect(int has_selection)
{
    return has_selection ? 0 : 1;
}

int csnhypodbp_view_should_reset(int changed, int ring_sync)
{
    return (changed && !ring_sync) ? 1 : 0;
}

int csnhypodbp_view_solution_same(int aq, int am, int av, int bq, int bm, int bv)
{
    return (aq == bq && am == bm && av == bv) ? 1 : 0;
}

int csnhypodbp_fetch_clear_on(int prev_qid, int prev_mod, int prev_ver,
                              int job_qid, int job_mod, int job_ver, int has_data)
{
    if (prev_qid != job_qid || prev_mod != job_mod || prev_ver != job_ver)
        return 1;   /* otro evento: el canvas entero es de otro sismo */
    return has_data ? 1 : 0;   /* mismo evento: solo si hay datos nuevos */
}

void csnhypodbp_view_cancel_anchor(CsnhypodbpView *v)
{
    if (v) v->have_anchor = 0;
}
