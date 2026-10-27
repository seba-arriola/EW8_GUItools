/* Lienzo de trazas: cairo -> BGRA (sin GTK ni Qt).
 *
 * Desvio CONSCIENTE del GTK (csntvp_gui.c:333-428): el lienzo es del tamano del
 * viewport y se pinta desplazado `scroll_y`, en vez de crear un canvas de
 * n_estaciones * alto_pista (141 pistas x 66 px = 9300 px de alto, 44 MB de
 * BGRA re-pintados a ~2 Hz). El resultado visual es el mismo y el coste es el
 * del viewport.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

int tvq_core_track_height(const TvqCore *c, int viewport_h)
{
    int sps;
    int th;

    if (!c)
        return 2;

    sps = c->cfg->stations_per_screen;
    if (sps < 1)
        sps = 1;
    if (viewport_h <= 0)
        viewport_h = 600;

    th = viewport_h / sps;
    if (th < 2)
        th = 2;
    return th;
}

int tvq_core_canvas_height(const TvqCore *c, int viewport_h)
{
    if (!c)
        return 0;
    return c->n_sta * tvq_core_track_height(c, viewport_h);
}

int tvq_colour_for_chan(const TvqCore *c, const char *chan, const double **rgb)
{
    char k = 'Z';
    int role = TVQ_COLOUR_WAVE;
    size_t len;

    if (!c || !rgb)
        return TVQ_COLOUR_WAVE;

    len = chan ? strlen(chan) : 0;
    if (len > 0)
        k = chan[len - 1];

    if (k == 'Z' || k == 'z')
        role = TVQ_COLOUR_Z;
    else if (k == 'N' || k == 'n')
        role = TVQ_COLOUR_N;
    else if (k == 'E' || k == 'e')
        role = TVQ_COLOUR_E;

    *rgb = c->colour[role];
    return role;
}

int tvq_core_hit(TvqCore *c, int x, int y, int w, int h, int scroll_y,
                 int *row, double *t)
{
    int th, area_w;
    int y_canvas, r;
    double t_left = 0.0, t_right = 0.0;
    double window_secs;

    if (!c)
        return 0;
    if (x <= TVQ_PANEL_WIDTH || x >= w)
        return 0;
    if (y < 0 || y >= h)
        return 0;

    area_w = w - TVQ_PANEL_WIDTH;
    if (area_w <= 0)
        return 0;

    th = tvq_core_track_height(c, h);
    y_canvas = y + scroll_y;
    r = y_canvas / th;
    if (r < 0 || r >= c->n_sta)
        return 0;

    tvq_core_view_range(c, &t_left, &t_right);
    window_secs = (double)c->cfg->time_window_min * 60.0;

    if (row)
        *row = r;
    if (t)
        *t = t_left + ((double)(x - TVQ_PANEL_WIDTH) / (double)area_w) * window_secs;
    return 1;
}

int tvq_paint_traces(TvqCore *c, unsigned char *bgra, int pw, int ph, int stride,
                     double scale, int scroll_y)
{
    cairo_surface_t *surf;
    cairo_t *cr;
    const double *bg;
    const double *fg;
    const double *sep;
    int w, h, area_w, th;
    double t_left = 0.0, t_right = 0.0, window_secs, font;

    if (!c || !bgra || pw <= 0 || ph <= 0)
        return 0;
    if (scale <= 0.0)
        scale = 1.0;

    w = (int)((double)pw / scale + 0.5);
    h = (int)((double)ph / scale + 0.5);
    if (w <= 0 || h <= 0)
        return 0;

    c->view_w = w;
    c->view_h = h;

    surf = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32, pw, ph,
                                               stride);
    cr = cairo_create(surf);
    cairo_scale(cr, scale, scale);

    bg = c->colour[TVQ_COLOUR_BG];
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, bg[0], bg[1], bg[2]);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Panel izquierdo (STA CHAN NET) como en el GTK. */
    cairo_set_source_rgb(cr, 0.96, 0.96, 0.96);
    cairo_rectangle(cr, 0, 0, TVQ_PANEL_WIDTH, h);
    cairo_fill(cr);

    area_w = w - TVQ_PANEL_WIDTH;
    if (area_w <= 0) {
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        return 1;
    }

    /* La envolvente depende del ancho en pixeles: se recalcula aqui para que
     * el primer dibujo (y cada resize) sea correcto. */
    tvq_env_update(c, area_w, 0);

    th = tvq_core_track_height(c, h);
    tvq_core_view_range(c, &t_left, &t_right);
    window_secs = (double)c->cfg->time_window_min * 60.0;

    fg = c->colour[TVQ_COLOUR_FONT];
    sep = c->colour[TVQ_COLOUR_SEP];

    font = (double)th * 0.7;
    if (font > 12.0)
        font = 12.0;
    if (font < 4.0)
        font = 4.0;

    for (int i = 0; i < c->n_sta; i++) {
        TvqStation *s = &c->sta[i];
        double y_top = (double)i * (double)th - (double)scroll_y;
        double y_center = y_top + (double)th / 2.0;
        const double *wave;
        char comp_net[48];

        if (y_top + (double)th <= 0.0)
            continue;
        if (y_top >= (double)h)
            break;

        /* etiqueta: STA (negrita) + CHAN NET */
        cairo_set_source_rgb(cr, fg[0], fg[1], fg[2]);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font);
        cairo_move_to(cr, 6, y_center + font * 0.35);
        cairo_show_text(cr, s->sta);

        {
            cairo_text_extents_t ext;

            cairo_text_extents(cr, s->sta, &ext);
            cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            snprintf(comp_net, sizeof comp_net, "%s %s", s->chan, s->net);
            cairo_move_to(cr, 6 + ext.x_advance + 6, y_center + font * 0.35);
            cairo_show_text(cr, comp_net);
        }

        /* separador */
        cairo_set_source_rgb(cr, sep[0], sep[1], sep[2]);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, TVQ_PANEL_WIDTH, y_top);
        cairo_line_to(cr, w, y_top);
        cairo_stroke(cr);

        /* envolvente cacheada (color por componente) */
        (void)tvq_colour_for_chan(c, s->chan, &wave);
        if (s->cache.env_valid && s->cache.env_width >= area_w &&
            s->cache.env_found_first) {
            double max_abs = s->cache.env_max_abs;
            double auto_scale;

            if (max_abs < 1.0)
                max_abs = 1.0;
            auto_scale = ((double)th * 0.425) / max_abs;
            auto_scale *= c->zoom;
            /* `s->scale` (columna 5 del StaFile) NO entra aqui: el GTK lo
             * guarda en dScreenScale y no lo lee nunca (csntvp_data.c:209).
             * Multiplicarlo DESPUES de normalizar encoge la traza por ese
             * factor (0.005 => 200x => linea plana). Ver tests/test_norm.c. */

            cairo_save(cr);
            cairo_rectangle(cr, TVQ_PANEL_WIDTH, y_top, area_w, th);
            cairo_clip(cr);
            cairo_set_source_rgb(cr, wave[0], wave[1], wave[2]);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

            for (int px = 0; px < area_w; px++) {
                double s_max, s_min;

                if (!s->cache.env_has[px])
                    continue;
                s_max = s->cache.env_max[px] * auto_scale;
                s_min = s->cache.env_min[px] * auto_scale;
                if (s_max - s_min < 2.0) {
                    double avg = (s_max + s_min) / 2.0;

                    s_max = avg + 1.0;
                    s_min = avg - 1.0;
                }
                cairo_rectangle(cr, TVQ_PANEL_WIDTH + px, y_center - s_max, 1.0,
                                s_max - s_min);
            }
            cairo_fill(cr);
            cairo_restore(cr);
        }

        /* picadas */
        cairo_set_line_width(cr, 2.0);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        for (int k = 0; k < TVQ_MAX_PICKS_PER_STA; k++) {
            const TvqPick *p = &s->picks[k];
            double x_pos;
            const double *pc;

            if (p->in_use <= 0 || p->t <= 0.0)
                continue;
            if (p->t < t_left || p->t > t_right + TVQ_LOOKAHEAD_SECS)
                continue;

            x_pos = TVQ_PANEL_WIDTH +
                    ((p->t - t_left) / window_secs * (double)area_w);
            if (x_pos <= (double)TVQ_PANEL_WIDTH || x_pos >= (double)w)
                continue;

            pc = (p->phase == 'S') ? c->colour[TVQ_COLOUR_S]
                                   : c->colour[TVQ_COLOUR_P];
            cairo_set_source_rgb(cr, pc[0], pc[1], pc[2]);
            cairo_move_to(cr, x_pos, y_top);
            cairo_line_to(cr, x_pos, y_top + (double)th);
            cairo_stroke(cr);
            cairo_move_to(cr, x_pos + 4, y_top + font + 2);
            cairo_show_text(cr, p->label);
        }
    }

    /* borde del panel */
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, TVQ_PANEL_WIDTH, 0);
    cairo_line_to(cr, TVQ_PANEL_WIDTH, h);
    cairo_stroke(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 1;
}
