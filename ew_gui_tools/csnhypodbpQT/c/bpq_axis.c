/*
 * bpq_axis.c - Pintor comun de los ejes de tiempo (picking y magnitudes).
 *
 * El eje de picking y el de la sub-pestana magnitudes dibujaban distinto (rejilla
 * de altura completa + letra 8.5 gris vs filete superior + ticks cortos + letra
 * 9 negra). Este .c es la UNICA fuente del estilo: fondo neutro, filete superior,
 * ticks cortos, etiquetas HH:MM:SS y paso 1-2-5 en multiplos redondos.
 *
 * Tambien pinta la banda de arrastre (rubber) que marca el intervalo que se va a
 * ampliar: mismo gris translucido que usaba la GUI GTK.
 */
#include "bpq_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Paso "bonito" 1-2-5x10^n para que las marcas caigan cada ~80 px. */
double bpq_axis_step(double span, double plot_px, double dpr)
{
    static const double nice[3] = { 1.0, 2.0, 5.0 };
    double want, base;
    int i;

    if (!(span > 0.0) || !(plot_px > 1.0)) return 1.0;
    if (!(dpr > 0.0)) dpr = 1.0;
    want = span * 80.0 * dpr / plot_px;
    if (!(want > 0.0)) return 1.0;

    base = 1.0;
    while (base <= want) base *= 10.0;
    base /= 10.0;
    for (i = 0; i < 3; i++)
        if (base * nice[i] >= want) return base * nice[i];
    return base * 10.0;
}

static int axis_hhmmss(double epoch, char *out, size_t n)
{
    time_t t;
    struct tm g;

    if (!out || n < 9 || !isfinite(epoch) || epoch <= 0.0) return 0;
    t = (time_t)floor(epoch);
    memset(&g, 0, sizeof(g));
    if (!gmtime_r(&t, &g)) return 0;
    snprintf(out, n, "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);
    return 1;
}

void bpq_axis_paint(cairo_t *cr, int w, int h, const BpqAxisPaint *p)
{
    double dpr, left, right, plot_w, span, t0, step, t;
    long long k0, k1, k;

    if (!cr || w <= 0 || h <= 0) return;
    dpr = (p && p->dpr > 0.0) ? p->dpr : 1.0;

    cairo_set_source_rgb(cr, 0.90, 0.90, 0.90);
    cairo_paint(cr);
    if (!p) return;

    left = p->left;
    right = p->right;
    if (left < 0.0) left = 0.0;
    if (right > (double)w) right = (double)w;
    plot_w = right - left;
    span = p->span;
    t0 = p->t0;

    /* Banda de arrastre DEBAJO de ticks y etiquetas. */
    if (p->band && span > 0.0 && plot_w > 0.0) {
        double x0 = p->band_x0, x1 = p->band_x1;

        if (x0 > x1) {
            const double tmp = x0;

            x0 = x1;
            x1 = tmp;
        }
        if (x0 < left) x0 = left;
        if (x1 > right) x1 = right;
        if (x1 > x0) {
            cairo_set_source_rgba(cr, 0.50, 0.50, 0.50, 0.35);
            cairo_rectangle(cr, x0, 1.0, x1 - x0, (double)h - 2.0);
            cairo_fill(cr);
        }
    }

    /* Filete superior: identifica el eje aunque no haya ventana. */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_move_to(cr, 0.0, 0.5 * dpr);
    cairo_line_to(cr, (double)w, 0.5 * dpr);
    cairo_stroke(cr);

    if (!(span > 0.0) || plot_w < 8.0) return;

    step = bpq_axis_step(span, plot_w, dpr);
    if (!(step > 0.0)) return;

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0 * dpr);

    /* Multiplos REDONDOS del paso, no t0 + k*step (asi las horas son legibles). */
    k0 = (long long)ceil(t0 / step);
    k1 = (long long)floor((t0 + span) / step);
    if (k1 < k0 || k1 - k0 > 4096) return;

    for (k = k0; k <= k1; k++) {
        char lbl[16];
        cairo_text_extents_t ext;
        double x;

        t = (double)k * step;
        x = left + ((t - t0) / span) * plot_w;
        if (x < left - 0.5 || x > right + 0.5) continue;

        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
        cairo_set_line_width(cr, 1.0 * dpr);
        /* +0.5 px: el trazo de 1 px queda centrado en el pixel y no gris a
         * medias (el muestreo de los tests lo detecta). */
        cairo_move_to(cr, x + 0.5 * dpr, 0.5 * dpr);
        cairo_line_to(cr, x + 0.5 * dpr, 4.5 * dpr);
        cairo_stroke(cr);

        if (!axis_hhmmss(t, lbl, sizeof(lbl))) continue;
        cairo_text_extents(cr, lbl, &ext);
        /* No invadir el borde derecho: la ultima etiqueta se omite. */
        if (x + 2.0 * dpr + ext.width > (double)w - 1.0) continue;
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_move_to(cr, x + 2.0 * dpr, (double)h - 4.0 * dpr);
        cairo_show_text(cr, lbl);
    }
}
