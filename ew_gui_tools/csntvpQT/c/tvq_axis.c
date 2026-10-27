/* Eje de tiempo UTC (paridad con csntvp_gui.c:430-447). */
#include <stdio.h>
#include <time.h>

#include <cairo.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

int tvq_paint_axis(TvqCore *c, unsigned char *bgra, int pw, int ph, int stride,
                   double scale)
{
    cairo_surface_t *surf;
    cairo_t *cr;
    int w, h;
    double t_left = 0.0, t_right = 0.0;
    time_t tr, tl, tc;
    char sl[32], sc[32], sr[32];
    struct tm tmv;

    if (!c || !bgra || pw <= 0 || ph <= 0)
        return 0;
    if (scale <= 0.0)
        scale = 1.0;

    w = (int)((double)pw / scale + 0.5);
    h = (int)((double)ph / scale + 0.5);
    if (w <= 0 || h <= 0)
        return 0;

    surf = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32, pw, ph,
                                               stride);
    cr = cairo_create(surf);
    cairo_scale(cr, scale, scale);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, w, 0);
    cairo_stroke(cr);

    tvq_core_view_range(c, &t_left, &t_right);
    if (t_right > 0.0) {
        tr = (time_t)t_right;
        tl = (time_t)t_left;
        tc = (time_t)(t_right - ((t_right - t_left) / 2.0));

        gmtime_r(&tl, &tmv);
        strftime(sl, sizeof sl, "%H:%M:%S", &tmv);
        gmtime_r(&tc, &tmv);
        strftime(sc, sizeof sc, "%H:%M:%S", &tmv);
        gmtime_r(&tr, &tmv);
        strftime(sr, sizeof sr, "%H:%M:%S UTC", &tmv);

        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_font_size(cr, 13);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_move_to(cr, 10, h - 10);
        cairo_show_text(cr, "UTC TIME");
        cairo_move_to(cr, TVQ_PANEL_WIDTH + 10, h - 10);
        cairo_show_text(cr, sl);
        cairo_move_to(cr, TVQ_PANEL_WIDTH + (w - TVQ_PANEL_WIDTH) / 2 - 30,
                      h - 10);
        cairo_show_text(cr, sc);
        cairo_move_to(cr, w - 110, h - 10);
        cairo_show_text(cr, sr);
    }

    if (c->stale) {
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_move_to(cr, 10, 14);
        cairo_show_text(cr, "STALE DATA");
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 1;
}
