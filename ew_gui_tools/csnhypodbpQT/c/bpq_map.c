/*
 * bpq_map.c - Panel de PLANTA del prototipo (cairo, sin GTK).
 *
 * Port de on_draw_map_plan (csnhypodbp_gui.c:3174-3270) sin globales:
 *   fondo equirectangular (o lienzo virtual) + grilla + rectas de los perfiles
 *   + capas .lines + marcadores de soluciones (preferida destacada, candidata y
 *   la elegida en Origins en verde, dibujada al final para que no la tapen).
 *
 * El mapa base NO se decodifica aqui: Qt lo convierte a ARGB32 premultiplicado y
 * lo pasa en BpqMapCtx.bg (el formato coincide con CAIRO_FORMAT_ARGB32 en
 * little-endian, igual que el buffer de bpq_scene_paint).
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>

#include "ewgui/geo.h"
#include "ewgui/overlay.h"
#include "csnhypodbp_mapview.h"

struct BpqMap {
    EwOverlay         *ov;
    CsnhypodbpProfiles prof;    /* copia: rectas de los perfiles en planta */
};

/* --- colores / radios de marcador (paridad con draw_marker de la GUI GTK) --- */
static void marker_rgb(int kind, int selected, double *r, double *g, double *b)
{
    if (selected) { *r = 0.05; *g = 0.65; *b = 0.15; return; }   /* verde: elegida */
    switch (kind) {
    case 1:  *r = 0.85; *g = 0.10; *b = 0.10; break;            /* preferida: rojo */
    case 2:  *r = 0.95; *g = 0.55; *b = 0.05; break;            /* candidata: naranja */
    default: *r = 0.20; *g = 0.20; *b = 0.20; break;            /* resto: gris oscuro */
    }
}

static double marker_radius(int kind, int selected)
{
    if (selected) return 7.0;
    return (kind == 1 || kind == 2) ? 5.5 : 3.5;
}

static void draw_marker(cairo_t *cr, double x, double y, double radius,
                        int kind, int selected)
{
    double r, g, b;
    marker_rgb(kind, selected, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
}

/* Paso "bonito" (1/2/5 x 10^k, en grados) para que la grilla no se llene ni se
 * quede vacia: apunta a ~6 divisiones sobre el tramo visible. */
static double nice_step(double span)
{
    static const double nice[] = {
        0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 45.0, 60.0, 90.0
    };
    const double want = (isfinite(span) && span > 0.0) ? span / 6.0 : 60.0;
    size_t i;

    for (i = 0; i < sizeof(nice) / sizeof(nice[0]); i++)
        if (nice[i] >= want) return nice[i];
    return 90.0;
}

/* Proyeccion para ewgui_overlay_draw: dentro de la transformacion se dibuja en
 * coordenadas de IMAGEN, asi que solo se proyecta lat/lon -> pixel de imagen. */
static void overlay_map(double lat, double lon, void *ud, double *sx, double *sy)
{
    const double *canvas = (const double *)ud;   /* [iw, ih] */
    ewgui_geo_project(lat, lon, canvas[0], canvas[1], sx, sy);
}

/* lat/lon -> pixel de PANTALLA (la misma transformacion que usa el lienzo). */
static void geo_to_screen(double lat, double lon, double iw, double ih,
                          int cw, int ch, double zoom, double cx, double cy,
                          double pan_x, double pan_y, double *sx, double *sy)
{
    double x = 0.0, y = 0.0;

    ewgui_geo_project(lat, lon, iw, ih, &x, &y);
    if (sx) *sx = (x - cx) * zoom + cw / 2.0 + pan_x;
    if (sy) *sy = (y - cy) * zoom + ch / 2.0 + pan_y;
}

/* Inversa: pixel de pantalla -> lat/lon (para saber que tramo esta a la vista). */
static void screen_to_geo(double sx, double sy, int cw, int ch, double zoom,
                          double cx, double cy, double pan_x, double pan_y,
                          double iw, double ih, double *lon, double *lat)
{
    double x, y;

    if (!isfinite(zoom) || zoom <= 0.0) zoom = 1.0;
    x = (sx - cw / 2.0 - pan_x) / zoom + cx;
    y = (sy - ch / 2.0 - pan_y) / zoom + cy;
    if (lon) *lon = x * 360.0 / iw - 180.0;
    if (lat) *lat = 90.0 - y * 180.0 / ih;
}

/* --- ciclo de vida -------------------------------------------------------- */

BpqMap *bpq_map_new(const BpqConfig *cfg)
{
    BpqMap *m;
    int i;

    if (!cfg) return NULL;
    m = (BpqMap *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->prof = cfg->profiles;   /* copia (hasta 64 perfiles) */

    if (cfg->n_overlay_files > 0) {
        m->ov = ewgui_overlay_new();
        if (m->ov) {
            for (i = 0; i < cfg->n_overlay_files; i++)
                (void)ewgui_overlay_append(m->ov, cfg->overlay_files[i]);
            for (i = 0; i < cfg->n_overlay_colors; i++) {
                double rgb[3];
                if (csnhypodbp_view_parse_color(cfg->overlay_color_hex[i], rgb))
                    (void)ewgui_overlay_set_color(m->ov, cfg->overlay_color_layer[i], rgb);
            }
            if (cfg->overlay_width > 0.0)
                ewgui_overlay_set_global_width(m->ov, cfg->overlay_width);
            for (i = 0; i < cfg->n_overlay_hides; i++)
                (void)ewgui_overlay_set_visible(m->ov, cfg->overlay_hide_layer[i], 0);
        }
    }
    return m;
}

void bpq_map_free(BpqMap *m)
{
    if (!m) return;
    if (m->ov) ewgui_overlay_free(m->ov);
    free(m);
}

int bpq_map_nlayers(const BpqMap *m)
{
    return (m && m->ov) ? (int)m->ov->nlayers : 0;
}

/* Auto-encuadre: bbox de los puntos (con suelo MapMinSpanDeg) y MapZoom del .d,
 * centrado en la preferida. Es el mismo camino que la GUI GTK
 * (csnhypodbp_mapfit_at + csnhypodbp_map_bbox). */
int bpq_map_fit(const BpqConfig *cfg, const BpqMapCtx *c, int w, int h,
                double margin, double *zoom, double *cx, double *cy)
{
    double lon0, lon1, lat0, lat1, ref_lat, ref_lon, z;
    double iw, ih;

    if (!cfg || !c || !c->pts || c->npts <= 0 || w <= 40 || h <= 40) return -1;
    if (csnhypodbp_map_bbox(c->pts, c->npts, &lon0, &lon1, &lat0, &lat1) != 0)
        return -1;

    /* Suelo de span (MapMinSpanDeg) sobre el centro del bbox. */
    {
        double sc = cfg->map_min_span_deg > 0.0 ? cfg->map_min_span_deg : 5.0;
        double clon = (lon0 + lon1) / 2.0, clat = (lat0 + lat1) / 2.0;
        if (lon1 - lon0 < sc) { lon0 = clon - sc / 2.0; lon1 = clon + sc / 2.0; }
        if (lat1 - lat0 < sc) { lat0 = clat - sc / 2.0; lat1 = clat + sc / 2.0; }
        if (lon0 < -180.0) lon0 = -180.0;
        if (lon1 > 180.0) lon1 = 180.0;
        if (lat0 < -90.0) lat0 = -90.0;
        if (lat1 > 90.0) lat1 = 90.0;
    }

    if (c->preferred >= 0 && c->preferred < c->npts) {
        ref_lat = c->pts[c->preferred].lat;
        ref_lon = c->pts[c->preferred].lon;
    } else {
        ref_lat = (lat0 + lat1) / 2.0;
        ref_lon = (lon0 + lon1) / 2.0;
    }

    iw = (c->bg && c->bg_w > 0) ? (double)c->bg_w : CSNHYPODBP_MAP_VIRTUAL_W;
    ih = (c->bg && c->bg_h > 0) ? (double)c->bg_h : CSNHYPODBP_MAP_VIRTUAL_H;

    csnhypodbp_mapfit_at(ref_lat, ref_lon, lon0, lon1, lat0, lat1,
                         iw, ih, w, h, margin,
                         cfg->map_zoom_factor, &z, cx, cy);
    if (zoom) *zoom = z;
    return 0;
}

void bpq_map_fit_world(const BpqMapCtx *c, int w, int h, double margin,
                       double *zoom, double *cx, double *cy)
{
    double iw, ih, z;

    if (!c || w <= 0 || h <= 0) return;
    iw = (c->bg && c->bg_w > 0) ? (double)c->bg_w : CSNHYPODBP_MAP_VIRTUAL_W;
    ih = (c->bg && c->bg_h > 0) ? (double)c->bg_h : CSNHYPODBP_MAP_VIRTUAL_H;
    if (!isfinite(margin) || margin < 0.0) margin = 0.0;

    z = (w - 2.0 * margin) / iw;
    if ((h - 2.0 * margin) / ih < z) z = (h - 2.0 * margin) / ih;
    if (!isfinite(z) || z <= 0.0) z = 1.0;

    if (zoom) *zoom = z;
    if (cx) *cx = iw / 2.0;
    if (cy) *cy = ih / 2.0;
}

/* --- interaccion ---------------------------------------------------------- */

void bpq_map_pan(BpqMapCtx *c, double dx, double dy)
{
    if (!c) return;
    if (!isfinite(dx)) dx = 0.0;
    if (!isfinite(dy)) dy = 0.0;
    if (!isfinite(c->pan_x)) c->pan_x = 0.0;
    if (!isfinite(c->pan_y)) c->pan_y = 0.0;
    c->pan_x += dx;
    c->pan_y += dy;
}

void bpq_map_zoom_at(BpqMapCtx *c, int w, int h, double mx, double my,
                     double factor)
{
    double z, zn, f;

    if (!c || w <= 0 || h <= 0) return;
    if (!isfinite(factor) || factor <= 0.0) return;
    z = (isfinite(c->zoom) && c->zoom > 0.0) ? c->zoom : 1.0;

    zn = z * factor;
    if (zn < 1e-4) zn = 1e-4;
    if (zn > 1e4) zn = 1e4;
    f = zn / z;
    if (!isfinite(f) || f == 1.0) return;

    if (!isfinite(mx)) mx = w / 2.0;
    if (!isfinite(my)) my = h / 2.0;
    if (!isfinite(c->pan_x)) c->pan_x = 0.0;
    if (!isfinite(c->pan_y)) c->pan_y = 0.0;

    /* Deja quieto el punto del mapa que hay bajo el cursor:
     *   pan' = m - c/2 - (m - c/2 - pan) * f   (m = cursor, c = lienzo) */
    c->pan_x = mx - w / 2.0 - (mx - w / 2.0 - c->pan_x) * f;
    c->pan_y = my - h / 2.0 - (my - h / 2.0 - c->pan_y) * f;
    c->zoom = zn;
}

int bpq_map_hit_test(const BpqMapCtx *c, int w, int h, double mx, double my,
                     double tol)
{
    double iw, ih, zoom;
    int i, best = -1;
    double bestd = 0.0;

    if (!c || !c->pts || c->npts <= 0 || w <= 0 || h <= 0) return -1;
    if (!isfinite(mx) || !isfinite(my)) return -1;
    iw = (c->bg && c->bg_w > 0) ? (double)c->bg_w : CSNHYPODBP_MAP_VIRTUAL_W;
    ih = (c->bg && c->bg_h > 0) ? (double)c->bg_h : CSNHYPODBP_MAP_VIRTUAL_H;
    zoom = (isfinite(c->zoom) && c->zoom > 0.0) ? c->zoom : 1.0;
    if (!isfinite(tol) || tol <= 0.0) tol = 8.0;

    for (i = 0; i < c->npts; i++) {
        const CsnhypodbpMapPt *p = &c->pts[i];
        double sx, sy, d;

        if (!isfinite(p->lat) || !isfinite(p->lon)) continue;
        geo_to_screen(p->lat, p->lon, iw, ih, w, h, zoom, c->cx, c->cy,
                      c->pan_x, c->pan_y, &sx, &sy);
        d = hypot(sx - mx, sy - my);
        if (d > tol) continue;
        if (best < 0 || d < bestd) { best = i; bestd = d; }
    }
    return best;
}

/* --- grilla y ejes -------------------------------------------------------- */

/* Meridianos y paralelos con sus etiquetas, y el marco del panel. Se dibuja en
 * pixeles de PANTALLA (despues de restaurar la transformacion) para que trazos
 * y textos no se escalen con el zoom: son ejes, no datos. */
static void draw_graticule(cairo_t *cr, int w, int h, const BpqMapCtx *c,
                           double iw, double ih, double zoom, double dpr)
{
    static const char *fmt_deg = "\xc2\xb0";   /* UTF-8 de "°" */
    char lbl[40];
    const char *fmt;
    const double dash[2] = { 2.0 * dpr, 4.0 * dpr };
    double lon_l, lat_t, lon_r, lat_b, step_v, step_h, v;
    long k;

    screen_to_geo(0.0, 0.0, w, h, zoom, c->cx, c->cy, c->pan_x, c->pan_y,
                  iw, ih, &lon_l, &lat_t);
    screen_to_geo((double)w, (double)h, w, h, zoom, c->cx, c->cy,
                  c->pan_x, c->pan_y, iw, ih, &lon_r, &lat_b);
    if (!isfinite(lon_l) || !isfinite(lon_r) || !isfinite(lat_t) || !isfinite(lat_b))
        return;

    step_v = nice_step(fabs(lon_r - lon_l));
    step_h = nice_step(fabs(lat_t - lat_b));

    /* Rejilla (debajo de las etiquetas). */
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.30, 0.34, 0.40, 0.30);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_set_dash(cr, dash, 2, 0.0);
    for (k = (long)ceil(lon_l / step_v); k * step_v <= lon_r; k++) {
        double sx, sy;
        v = (double)k * step_v;
        if (v < -180.0 || v > 180.0) continue;
        geo_to_screen(0.0, v, iw, ih, w, h, zoom, c->cx, c->cy,
                      c->pan_x, c->pan_y, &sx, &sy);
        cairo_move_to(cr, sx, 0.0);
        cairo_line_to(cr, sx, (double)h);
    }
    for (k = (long)ceil(lat_b / step_h); k * step_h <= lat_t; k++) {
        double sx, sy;
        v = (double)k * step_h;
        if (v < -90.0 || v > 90.0) continue;
        geo_to_screen(v, 0.0, iw, ih, w, h, zoom, c->cx, c->cy,
                      c->pan_x, c->pan_y, &sx, &sy);
        cairo_move_to(cr, 0.0, sy);
        cairo_line_to(cr, (double)w, sy);
    }
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);
    cairo_restore(cr);

    /* Etiquetas de las coordenadas: abajo los meridianos, a la izquierda los
     * paralelos (es lo que da la lectura del panel). */
    fmt = (step_v < 0.5) ? "%.2f" : (step_v < 5.0) ? "%.1f" : "%.0f";
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0 * dpr);
    cairo_set_source_rgb(cr, 0.18, 0.21, 0.26);
    for (k = (long)ceil(lon_l / step_v); k * step_v <= lon_r; k++) {
        double sx, sy;
        v = (double)k * step_v;
        if (v < -180.0 || v > 180.0) continue;
        geo_to_screen(0.0, v, iw, ih, w, h, zoom, c->cx, c->cy,
                      c->pan_x, c->pan_y, &sx, &sy);
        if (sx < 2.0 * dpr || sx > w - 26.0 * dpr) continue;
        snprintf(lbl, sizeof lbl, fmt, v);
        cairo_move_to(cr, sx + 3.0 * dpr, h - 5.0 * dpr);
        cairo_show_text(cr, lbl);
        cairo_show_text(cr, fmt_deg);
    }
    fmt = (step_h < 0.5) ? "%.2f" : (step_h < 5.0) ? "%.1f" : "%.0f";
    for (k = (long)ceil(lat_b / step_h); k * step_h <= lat_t; k++) {
        double sx, sy;
        v = (double)k * step_h;
        if (v < -90.0 || v > 90.0) continue;
        geo_to_screen(v, 0.0, iw, ih, w, h, zoom, c->cx, c->cy,
                      c->pan_x, c->pan_y, &sx, &sy);
        if (sy < 10.0 * dpr || sy > h - 14.0 * dpr) continue;
        snprintf(lbl, sizeof lbl, fmt, v);
        cairo_move_to(cr, 4.0 * dpr, sy - 3.0 * dpr);
        cairo_show_text(cr, lbl);
        cairo_show_text(cr, fmt_deg);
    }

    /* Marco del panel. */
    cairo_set_source_rgb(cr, 0.26, 0.29, 0.33);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_rectangle(cr, 0.5 * dpr, 0.5 * dpr, w - dpr, h - dpr);
    cairo_stroke(cr);
}

/* --- dibujo --------------------------------------------------------------- */

void bpq_map_draw(cairo_t *cr, int w, int h, const BpqMapCtx *c, const BpqMap *m)
{
    double iw, ih, zoom, dpr;
    int i, k;

    if (!cr || !c || w <= 0 || h <= 0) return;

    cairo_set_source_rgb(cr, 0.94, 0.95, 0.96);
    cairo_paint(cr);

    /* Sin mapa base se proyecta sobre el lienzo virtual (misma decision que la
     * version GTK: el mapa nunca se apaga). */
    if (c->bg && c->bg_w > 0 && c->bg_h > 0 && c->bg_stride >= 4 * c->bg_w) {
        iw = (double)c->bg_w;
        ih = (double)c->bg_h;
    } else {
        iw = CSNHYPODBP_MAP_VIRTUAL_W;
        ih = CSNHYPODBP_MAP_VIRTUAL_H;
    }
    zoom = (c->zoom > 0.0) ? c->zoom : 1.0;
    dpr = (c->dpr > 0.0) ? c->dpr : 1.0;

    cairo_save(cr);
    cairo_translate(cr, w / 2.0 + c->pan_x, h / 2.0 + c->pan_y);
    cairo_scale(cr, zoom, zoom);
    cairo_translate(cr, -c->cx, -c->cy);

    if (c->bg && c->bg_w > 0) {
        cairo_surface_t *s = cairo_image_surface_create_for_data(
            (unsigned char *)c->bg, CAIRO_FORMAT_ARGB32,
            c->bg_w, c->bg_h, c->bg_stride);
        if (s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
            cairo_set_source_surface(cr, s, 0.0, 0.0);
            cairo_paint(cr);
        }
        if (s) cairo_surface_destroy(s);
    }

    /* Rectas de los perfiles: la activa resaltada (paridad gui.c:3213-3235). */
    for (i = 0; i < m->prof.n; i++) {
        const CsnhypodbpProfile *pr = &m->prof.p[i];
        double lo0 = pr->lon1, lo1 = pr->lon2, x1, y1, x2, y2;
        if (pr->lon1 == pr->lon2) continue;      /* vertical: no se dibuja */
        ewgui_geo_project(pr->a * lo0 + pr->b, lo0, iw, ih, &x1, &y1);
        ewgui_geo_project(pr->a * lo1 + pr->b, lo1, iw, ih, &x2, &y2);
        if (i == c->active_profile) {
            cairo_set_source_rgb(cr, 0.08, 0.40, 0.75);
            cairo_set_line_width(cr, 2.0 * dpr / zoom);
        } else {
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, 0.65);
            cairo_set_line_width(cr, 1.0 * dpr / zoom);
        }
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
    }

    /* Capas .lines (placas, subduccion, fallas...). */
    if (m->ov && m->ov->nlayers > 0) {
        double canvas[2];
        canvas[0] = iw;
        canvas[1] = ih;
        ewgui_overlay_draw(cr, m->ov, overlay_map, canvas, dpr / zoom);
    }

    /* Marcadores: primero el resto y encima preferida y candidata (gui.c:3244-3268). */
    for (k = 0; k <= 2; k++) {
        for (i = 0; i < c->npts; i++) {
            const CsnhypodbpMapPt *p = &c->pts[i];
            double x, y;
            if (p->kind != k) continue;
            if (!isfinite(p->lat) || !isfinite(p->lon)) continue;
            ewgui_geo_project(p->lat, p->lon, iw, ih, &x, &y);
            draw_marker(cr, x, y, marker_radius(p->kind, 0) * dpr / zoom, p->kind, 0);
        }
    }
    if (c->selected >= 0 && c->selected < c->npts) {
        const CsnhypodbpMapPt *p = &c->pts[c->selected];
        double x, y;
        if (isfinite(p->lat) && isfinite(p->lon)) {
            ewgui_geo_project(p->lat, p->lon, iw, ih, &x, &y);
            draw_marker(cr, x, y, marker_radius(p->kind, 1) * dpr / zoom, p->kind, 1);
        }
    }

    cairo_restore(cr);

    /* Ejes y marco, ya en pixeles de pantalla. */
    if (c->show_grid)
        draw_graticule(cr, w, h, c, iw, ih, zoom, dpr);
    else {
        cairo_set_source_rgb(cr, 0.26, 0.29, 0.33);
        cairo_set_line_width(cr, 1.0 * dpr);
        cairo_rectangle(cr, 0.5 * dpr, 0.5 * dpr, w - dpr, h - dpr);
        cairo_stroke(cr);
    }
}
