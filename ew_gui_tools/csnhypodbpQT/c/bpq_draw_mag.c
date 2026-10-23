/*
 * bpq_draw_mag.c - Lienzo de la sub-pestana `magnitudes` (fase 6.1).
 *
 * Dibuja la TRAZA PROCESADA de una estacion para la magnitud activa —el trabajo
 * de procesado lo hace el nucleo `csnhypodbp_magview` (Wood-Anderson para ML,
 * integracion/diezmado para Mwp, pasa-banda para Mb/Ms)— y encima la FRANJA de
 * la ventana elegida, con su handle de arrastre arriba (misma regla que la GTK:
 * solo desde el handle se mueve o redimensiona; el resto del canvas es zoom).
 *
 * La geometria vive en `mag_geo()`, compartida por el dibujo y el hit-test, para
 * que lo que se ve y lo que se agarra sean lo mismo.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ewgui/wave.h"          /* EwGuiTrace (lo pide el nucleo magview) */
#include "csnhypodbp_view.h"
#include "csnhypodbp_magview.h"

#include "bpq_internal.h"

#define MAG_LEFT   58.0
#define MAG_RIGHT  10.0
#define MAG_TITLE  20.0
#define MAG_BOTTOM  8.0
#define MAG_HANDLE 16.0   /* = CSNHYPODBP_MAG_HANDLE_H */

struct mag_geo {
    double l, r, t, b;    /* area de dibujo (px de dispositivo) */
    double a, span;       /* ventana dibujada: epoch y segundos */
    double xa, xb;        /* franja de la ventana (px) */
    int    ok;
};

static void hhmmss(double epoch, char *out, size_t n)
{
    time_t t = (time_t)floor(epoch);
    struct tm tm;

    if (!gmtime_r(&t, &tm)) {
        snprintf(out, n, "--:--:--");
        return;
    }
    snprintf(out, n, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static double time_at(const struct mag_geo *g, double x)
{
    const double w = g->r - g->l;

    if (w <= 1.0) return g->a;
    return g->a + (x - g->l) / w * g->span;
}

static double x_at(const struct mag_geo *g, double t)
{
    const double w = g->r - g->l;

    if (g->span <= 0.0) return g->l;
    return g->l + (t - g->a) / g->span * w;
}

/* Encuadre unico: zoom si lo hay; si no [anchor+t0-m, anchor+t1+m]. */
static void mag_geo(const BpqMagCtx *c, int w, int h, struct mag_geo *g)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double m = 0.0, a0, a1;

    memset(g, 0, sizeof(*g));
    g->l = MAG_LEFT * dpr;
    g->r = (double)w - MAG_RIGHT * dpr;
    g->t = (MAG_TITLE + MAG_HANDLE) * dpr;
    g->b = (double)h - MAG_BOTTOM * dpr;
    if (!c || !c->magcfg || g->r - g->l < 8.0 || g->b - g->t < 8.0) return;

    if (c->view_span > 0.0) {
        a0 = c->view_t0;
        a1 = c->view_t0 + c->view_span;
    } else if (isfinite(c->t0) && isfinite(c->t1) && c->t1 > c->t0) {
        m = csnhypodbp_magview_margin(c->t0, c->t1);
        a0 = c->anchor + c->t0 - m;
        a1 = c->anchor + c->t1 + m;
    } else {
        return;
    }
    if (!isfinite(a0) || !isfinite(a1) || a1 <= a0) return;

    g->a = a0;
    g->span = a1 - a0;
    g->xa = x_at(g, c->anchor + c->t0);
    g->xb = x_at(g, c->anchor + c->t1);
    g->ok = 1;
}

static void label(cairo_t *cr, double x, double y, const char *txt, double size)
{
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, txt);
}

static void plot_trace(cairo_t *cr, const struct mag_geo *g,
                       const BpqMagCtx *c, const CsnhypodbpMagView *v)
{
    const double dpr = (c->dpr > 0.0) ? c->dpr : 1.0;
    const double top = g->t, bot = g->b;
    const double mid = (top + bot) * 0.5, half = (bot - top) * 0.5;
    long i, n = v->n;
    double maxv = 0.0, gain = (c->y_gain > 0.0) ? c->y_gain : 1.0;
    long first, last;
    int started = 0;

    /* Escala: maximo |muestra| de la ventana visible (los NaN son gaps). */
    first = (long)floor((g->a - v->oldest) * v->rate);
    last = (long)ceil((g->a + g->span - v->oldest) * v->rate);
    if (first < 0) first = 0;
    if (last >= n) last = n - 1;
    for (i = first; i <= last; i++) {
        const double val = csnhypodbp_magview_at(v, i);

        if (isfinite(val) && fabs(val) > maxv) maxv = fabs(val);
    }
    if (!(maxv > 0.0)) maxv = 1.0;

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.15);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_new_path(cr);
    for (i = first; i <= last; i++) {
        const double val = csnhypodbp_magview_at(v, i);
        double x, y;

        if (!isfinite(val)) {
            started = 0;
            continue;
        }
        x = x_at(g, v->oldest + (double)i / v->rate);
        y = mid - (val * gain / maxv) * half * 0.85;
        if (!started) {
            cairo_move_to(cr, x, y);
            started = 1;
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);

    /* Linea de cero. */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_move_to(cr, g->l, mid);
    cairo_line_to(cr, g->r, mid);
    cairo_stroke(cr);
}

static void draw_strip(cairo_t *cr, const struct mag_geo *g,
                       const BpqMagCtx *c)
{
    const double dpr = (c->dpr > 0.0) ? c->dpr : 1.0;
    const double hh = MAG_HANDLE * dpr;
    double x0 = g->xa, x1 = g->xb;
    char txt[64];

    if (x1 < x0) { double s = x0; x0 = x1; x1 = s; }

    /* Sombra suave de la ventana sobre la traza (da contexto sin taparla). */
    if (x1 > g->l && x0 < g->r) {
        cairo_set_source_rgba(cr, 0.95, 0.72, 0.15, 0.10);
        cairo_rectangle(cr, x0 < g->l ? g->l : x0, g->t,
                        (x1 > g->r ? g->r : x1) - (x0 < g->l ? g->l : x0),
                        g->b - g->t);
        cairo_fill(cr);
    }

    /* Handle relleno: ambar si la ventana es la del .d, naranja si es a mano. */
    cairo_set_source_rgb(cr, c->touched ? 0.93 : 0.87,
                         c->touched ? 0.62 : 0.80,
                         c->touched ? 0.20 : 0.35);
    cairo_rectangle(cr, x0, 0.0, x1 - x0 > 0.0 ? x1 - x0 : 1.0, hh);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.25, 0.18, 0.05, 0.85);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_rectangle(cr, x0 + 0.5, 0.5, (x1 - x0 > 1.0 ? x1 - x0 : 1.0) - 1.0,
                    hh - 1.0);
    cairo_stroke(cr);

    /* Bordes de la ventana: lineas verticales de arriba a abajo. */
    cairo_set_source_rgba(cr, c->touched ? 0.85 : 0.62,
                          c->touched ? 0.35 : 0.55, 0.05, 0.9);
    cairo_set_line_width(cr, 1.5 * dpr);
    cairo_move_to(cr, x0, hh);
    cairo_line_to(cr, x0, g->b);
    cairo_move_to(cr, x1, hh);
    cairo_line_to(cr, x1, g->b);
    cairo_stroke(cr);

    /* Ancla (la P): discontinua. */
    cairo_set_source_rgba(cr, 0.20, 0.20, 0.20, 0.55);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_set_dash(cr, (const double[]){ 4.0 * dpr, 3.0 * dpr }, 2, 0.0);
    cairo_move_to(cr, x_at(g, c->anchor), g->t);
    cairo_line_to(cr, x_at(g, c->anchor), g->b);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);

    /* Rotulos de los bordes sobre el handle. */
    cairo_set_source_rgb(cr, 0.15, 0.10, 0.02);
    snprintf(txt, sizeof(txt), "%+.0f", c->t0);
    label(cr, x0 + 2.0 * dpr, hh - 4.0 * dpr, txt, 9.0 * dpr);
    snprintf(txt, sizeof(txt), "%+.0f", c->t1);
    label(cr, x1 - 22.0 * dpr, hh - 4.0 * dpr, txt, 9.0 * dpr);
}

void bpq_mag_draw(cairo_t *cr, int w, int h, const BpqMagCtx *c)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    struct mag_geo g;
    CsnhypodbpMagView v;
    EwGuiTrace *tr;
    char txt[128];

    if (!cr || w <= 0 || h <= 0) return;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    mag_geo(c, w, h, &g);
    if (!g.ok) {
        /* Sin geometria (sin ventana o sin traza): fondo neutro, sin inventar
         * datos (no se llama al placeholder del canvas: este .c no lo enlaza). */
        cairo_set_source_rgb(cr, 0.96, 0.96, 0.96);
        cairo_paint(cr);
        return;
    }

    /* Marco. */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_rectangle(cr, g.l, g.t, g.r - g.l, g.b - g.t);
    cairo_stroke(cr);

    /* Titulo: estacion, magnitud y ventana. */
    {
        const char *kname = csnhypodbp_magcfg_name(c->kind);

        snprintf(txt, sizeof(txt), "%s  %s  win %+.0f..%+.0f s%s",
                 c->sta ? c->sta : "-", kname ? kname : "?", c->t0, c->t1,
                 c->touched ? " (M)" : "");
        cairo_set_source_rgb(cr, 0.10, 0.10, 0.10);
        label(cr, 4.0 * dpr, 13.0 * dpr, txt, 11.0 * dpr);
    }

    tr = (EwGuiTrace *)bpq_trace_ew(c->tr);
    csnhypodbp_magview_init(&v);
    if (!tr || csnhypodbp_magview_build(&v, tr, c->kind, c->magcfg,
                                        g.a, g.a + g.span) != 0) {
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        label(cr, g.l + 8.0 * dpr, (g.t + g.b) * 0.5, "sin traza para esta magnitud",
              11.0 * dpr);
        csnhypodbp_magview_free(&v);
        draw_strip(cr, &g, c);
        return;
    }

    plot_trace(cr, &g, c, &v);
    csnhypodbp_magview_free(&v);
    draw_strip(cr, &g, c);
}

void bpq_mag_draw_axis(cairo_t *cr, int w, int h, const BpqMagCtx *c)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    struct mag_geo g;
    double step, t;
    char txt[32];

    if (!cr || w <= 0 || h <= 0) return;

    cairo_set_source_rgb(cr, 0.90, 0.90, 0.90);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_move_to(cr, 0.0, 0.5);
    cairo_line_to(cr, (double)w, 0.5);
    cairo_stroke(cr);

    mag_geo(c, w, h * 4, &g);   /* misma escala horizontal que el canvas */
    if (!g.ok) return;

    step = g.span * 80.0 * dpr / (g.r - g.l);
    if (!(step > 0.0)) return;
    /* Paso "bonito" (1-2-5) en segundos. */
    {
        const double p = pow(10.0, floor(log10(step)));
        const double f = step / p;

        step = (f <= 1.5 ? 1.0 : (f <= 3.5 ? 2.0 : (f <= 7.5 ? 5.0 : 10.0))) * p;
    }
    cairo_set_font_size(cr, 9.0 * dpr);
    for (t = ceil(g.a / step) * step; t <= g.a + g.span; t += step) {
        const double x = x_at(&g, t);

        cairo_move_to(cr, x, 0.0);
        cairo_line_to(cr, x, 4.0 * dpr);
        cairo_stroke(cr);
        hhmmss(t, txt, sizeof(txt));
        cairo_move_to(cr, x + 2.0 * dpr, h - 4.0 * dpr);
        cairo_show_text(cr, txt);
    }
}

int bpq_mag_hit(const BpqMagCtx *c, int w, int h, double mx, double my,
                int *grab, double *t_at)
{    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    struct mag_geo g;
    CsnhypodbpMagGrab gr;

    if (grab) *grab = 0;
    if (t_at) *t_at = 0.0;
    mag_geo(c, w, h, &g);
    if (!g.ok) return 0;
    /* Solo desde el handle de arriba (igual que la GTK). */
    if (!csnhypodbp_magmarks_handle_hit(my, 0.0)) return 0;
    gr = csnhypodbp_magmarks_grab(mx, g.xa, g.xb, 5.0 * dpr);
    if (gr == CSNHYPODBP_MAGG_NONE) return 0;
    if (grab) *grab = (int)gr;
    if (t_at) *t_at = time_at(&g, mx);
    return 1;
}

double bpq_mag_time_at(const BpqMagCtx *c, int w, int h, double mx)
{
    struct mag_geo g;

    mag_geo(c, w, h, &g);
    if (!g.ok) return 0.0;
    return time_at(&g, mx);
}

double bpq_mag_margin(double t0, double t1)
{
    return csnhypodbp_magview_margin(t0, t1);
}
