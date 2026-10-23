/* Mapa equirectangular con cairo: port de `on_draw_map`
 * (csnstaevdisp.c:496-737).
 *
 * El fondo lo decodifica la UI con QImage y llega como BGRA
 * (`sev_map_set_background`); asi la capa C no arrastra libjpeg. El resto
 * (rejilla, overlays .lines, triangulos de estacion coloreados por elevacion,
 * circulos de sismo escalados por magnitud, leyenda y marco) se dibuja con
 * cairo, con las MISMAS constantes que el GTK.
 *
 * `sev_map_paint_bgra` es lo unico que ve `ewguiqt::CanvasWidget`; el export
 * re-renderiza la MISMA escena a un surface SVG/PDF/PNG.
 *
 * Geometria: el centro geografico es FIJO en (-30,-70) y la vista arranca en
 * zoom 2.0 con pan 0 (no hay fit ni auto-encuadre). El zoom esta anclado al
 * cursor y NO se resetea al refrescar los datos.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <cairo-pdf.h>
#include <cairo-svg.h>

#include "ewgui/geo.h"
#include "ewgui/overlay.h"

#include "sev_capi.h"
#include "sev_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ZOOM_MIN     0.2
#define ZOOM_MAX     100.0
#define ZOOM_DEFAULT 2.0

#define EVENTS_DEFAULT 10

struct SevMap {
    const SevConfig *cfg;       /* prestada */
    EwOverlay *ov;
    unsigned char *bg;          /* copia BGRA del fondo */
    int bg_w, bg_h, bg_stride;
    cairo_surface_t *bg_surface;
    int max_events;
};

typedef struct {
    double img_w, img_h;
} SevMapCtx;

static void overlay_map(double lat, double lon, void *ud,
                        double *sx, double *sy)
{
    SevMapCtx *c = ud;
    ewgui_geo_project(lat, lon, c->img_w, c->img_h, sx, sy);
}

SevMap *sev_map_new(const SevConfig *cfg)
{
    SevMap *m;

    if (!cfg)
        return NULL;
    m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->cfg = cfg;
    m->max_events = EVENTS_DEFAULT;
    m->ov = sev_overlays_load(cfg, stderr);
    return m;
}

void sev_map_free(SevMap *m)
{
    if (!m)
        return;
    if (m->bg_surface)
        cairo_surface_destroy(m->bg_surface);
    free(m->bg);
    ewgui_overlay_free(m->ov);
    free(m);
}

void sev_map_set_background(SevMap *m, const unsigned char *bgra,
                            int w, int h, int stride)
{
    size_t bytes;

    if (!m)
        return;
    if (m->bg_surface) {
        cairo_surface_destroy(m->bg_surface);
        m->bg_surface = NULL;
    }
    free(m->bg);
    m->bg = NULL;
    m->bg_w = m->bg_h = m->bg_stride = 0;

    if (!bgra || w <= 0 || h <= 0 || stride < w * 4)
        return;

    bytes = (size_t)stride * (size_t)h;
    m->bg = malloc(bytes);
    if (!m->bg)
        return;
    memcpy(m->bg, bgra, bytes);
    m->bg_w = w;
    m->bg_h = h;
    m->bg_stride = stride;
    m->bg_surface = cairo_image_surface_create_for_data(
        m->bg, CAIRO_FORMAT_ARGB32, w, h, stride);
    if (cairo_surface_status(m->bg_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(m->bg_surface);
        m->bg_surface = NULL;
        free(m->bg);
        m->bg = NULL;
        m->bg_w = m->bg_h = m->bg_stride = 0;
    }
}

int sev_map_has_background(const SevMap *m)
{
    return (m && m->bg_surface) ? 1 : 0;
}

void sev_map_reload_overlays(SevMap *m)
{
    if (!m)
        return;
    ewgui_overlay_free(m->ov);
    m->ov = sev_overlays_load(m->cfg, stderr);
}

int sev_map_nlayers(const SevMap *m)
{
    return (m && m->ov) ? (int)m->ov->nlayers : 0;
}

int sev_map_layer_info(const SevMap *m, int i, SevLayerInfo *out)
{
    const EwOverlayLayer *L;
    size_t k, npt = 0;

    if (!m || !m->ov || !out || i < 0 || i >= (int)m->ov->nlayers)
        return -1;
    L = &m->ov->layers[i];
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", L->name);
    out->visible = L->visible;
    out->rgb[0] = L->color[0];
    out->rgb[1] = L->color[1];
    out->rgb[2] = L->color[2];
    out->width = L->width;
    for (k = 0; k < L->nlines; k++)
        npt += L->lines[k].npts;
    out->polylines = (int)L->nlines;
    out->vertices = (int)npt;
    return 0;
}

int sev_map_set_visible(SevMap *m, int i, int visible)
{
    const EwOverlayLayer *L;

    if (!m || !m->ov || i < 0 || i >= (int)m->ov->nlayers)
        return -1;
    L = &m->ov->layers[i];
    return ewgui_overlay_set_visible(m->ov, L->name, visible) > 0 ? 0 : -1;
}

void sev_map_set_max_events(SevMap *m, int n)
{
    if (!m)
        return;
    if (n < 1)
        n = 1;
    if (n > SEV_MAX_EVENTS)
        n = SEV_MAX_EVENTS;
    m->max_events = n;
}

int sev_map_max_events(const SevMap *m)
{
    return m ? m->max_events : EVENTS_DEFAULT;
}

/* --- Vista --------------------------------------------------------------- */

void sev_map_view_init(SevMapView *v)
{
    if (!v)
        return;
    memset(v, 0, sizeof(*v));
    v->zoom = ZOOM_DEFAULT;
}

void sev_map_reset(SevMapView *v)
{
    if (!v)
        return;
    v->zoom = ZOOM_DEFAULT;
    v->pan_x = 0.0;
    v->pan_y = 0.0;
}

void sev_map_zoom_at(SevMapView *v, int w, int h,
                     double mx, double my, double factor)
{
    double z0, z1, applied;

    if (!v || !isfinite(factor) || factor <= 0.0 || v->zoom <= 0.0)
        return;

    z0 = v->zoom;
    z1 = z0 * factor;
    if (z1 < ZOOM_MIN)
        z1 = ZOOM_MIN;
    if (z1 > ZOOM_MAX)
        z1 = ZOOM_MAX;

    /* Sin canvas no hay punto de anclaje: queda el zoom al centro. */
    if (w > 0 && h > 0) {
        /* Factor REALMENTE aplicado: si el clamp no dejo cambiar el zoom, el
         * pan tiene que quedar intacto (con el pedido, derivaria en cada rueda
         * contra el tope). */
        applied = z1 / z0;
        v->pan_x = mx - w / 2.0 - (mx - w / 2.0 - v->pan_x) * applied;
        v->pan_y = my - h / 2.0 - (my - h / 2.0 - v->pan_y) * applied;
    }
    v->zoom = z1;
}

void sev_map_pan(SevMapView *v, double dx, double dy)
{
    if (!v)
        return;
    v->pan_x += dx;
    v->pan_y += dy;
}

void sev_map_project(const SevMap *m, const SevMapView *v, int w, int h,
                     double lat, double lon, double *x, double *y)
{
    double img_w, img_h, cx, cy, wx, wy;

    if (x) *x = 0.0;
    if (y) *y = 0.0;
    if (!m || !v || !m->bg_surface || m->bg_w <= 0 || m->bg_h <= 0 ||
        w <= 0 || h <= 0)
        return;

    img_w = (double)m->bg_w;
    img_h = (double)m->bg_h;
    ewgui_geo_project(-30.0, -70.0, img_w, img_h, &cx, &cy);
    ewgui_geo_project(lat, lon, img_w, img_h, &wx, &wy);
    if (x)
        *x = (wx - cx) * v->zoom + (double)w / 2.0 + v->pan_x;
    if (y)
        *y = (wy - cy) * v->zoom + (double)h / 2.0 + v->pan_y;
}

/* --- Dibujo -------------------------------------------------------------- */

static const SevSnapshot g_empty;

void sev_map_draw(cairo_t *cr, int width, int height,
                  const SevMap *m, const SevMapView *v,
                  const SevSnapshot *s)
{
    double zoom = (v && v->zoom > 0.0) ? v->zoom : ZOOM_DEFAULT;
    double pan_x = v ? v->pan_x : 0.0;
    double pan_y = v ? v->pan_y : 0.0;

    if (!cr || width <= 0 || height <= 0)
        return;
    if (!s)
        s = &g_empty;

    if (m && m->bg_surface) {
        double img_w = (double)m->bg_w;
        double img_h = (double)m->bg_h;
        double center_x, center_y;

        ewgui_geo_project(-30.0, -70.0, img_w, img_h, &center_x, &center_y);

        cairo_save(cr);

        /* 1. Centro de pantalla + pan + zoom global. */
        cairo_translate(cr, width / 2.0 + pan_x, height / 2.0 + pan_y);
        cairo_scale(cr, zoom, zoom);
        cairo_translate(cr, -center_x, -center_y);

        /* 2. Mapa base. */
        cairo_set_source_surface(cr, m->bg_surface, 0, 0);
        cairo_paint(cr);

        /* 3. Rejilla lat/lon. */
        {
            double step = ewgui_geo_grid_step(zoom);
            double lon, lat;

            cairo_new_path(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
            cairo_set_line_width(cr, 1.0 / zoom);
            for (lon = -180.0; lon <= 180.0; lon += step) {
                double x;
                ewgui_geo_project(0.0, lon, img_w, img_h, &x, NULL);
                cairo_move_to(cr, x, 0);
                cairo_line_to(cr, x, img_h);
            }
            for (lat = -90.0; lat <= 90.0; lat += step) {
                double y;
                ewgui_geo_project(lat, 0.0, img_w, img_h, NULL, &y);
                cairo_move_to(cr, 0, y);
                cairo_line_to(cr, img_w, y);
            }
            cairo_stroke(cr);
        }

        /* 3b. Capas .lines (debajo de estaciones y sismos). */
        if (m->ov) {
            SevMapCtx octx = { img_w, img_h };
            ewgui_overlay_draw(cr, m->ov, overlay_map, &octx, 1.0 / zoom);
        }

        /* 4. Estaciones: triangulos coloreados por elevacion. */
        {
            double s_size = m->cfg->station_size / zoom;
            size_t i;

            cairo_set_line_width(cr, 1.0 / zoom);
            for (i = 0; i < s->nsta; i++) {
                const SevStation *st = &s->sta[i];
                double x, y, ratio, r, g, b;

                /* Paridad exacta con el GTK: (0,0) se considera "sin dato". */
                if (st->lat == 0.0 || st->lon == 0.0)
                    continue;

                ewgui_geo_project(st->lat, st->lon, img_w, img_h, &x, &y);

                ratio = (st->elev_m - s->elev_min) /
                        (s->elev_max - s->elev_min);
                if (ratio < 0.0)
                    ratio = 0.0;
                if (ratio > 1.0)
                    ratio = 1.0;

                r = 0.6 - (0.6 * ratio);
                g = 1.0 - (0.7 * ratio);
                b = 0.6 - (0.6 * ratio);

                cairo_new_path(cr);
                cairo_set_source_rgb(cr, r, g, b);
                cairo_move_to(cr, x, y - s_size);
                cairo_line_to(cr, x - s_size, y + s_size);
                cairo_line_to(cr, x + s_size, y + s_size);
                cairo_close_path(cr);
                cairo_fill_preserve(cr);

                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                cairo_stroke(cr);
            }
        }

        /* 5. Sismos: de viejo a nuevo para que el mas nuevo quede encima. */
        {
            size_t n = s->nev;
            size_t cap = (size_t)m->max_events;
            int i;

            if (n > cap)
                n = cap;
            for (i = (int)n - 1; i >= 0; i--) {
                const SevEvent *q = &s->ev[i];
                double x, y, mag, radius;

                ewgui_geo_project(q->lat, q->lon, img_w, img_h, &x, &y);
                mag = q->mag;
                if (mag < 0.0)
                    mag = 1.0;
                radius = (mag * 2.0) / zoom;

                cairo_new_path(cr);
                if (i == 0) {
                    cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
                    cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
                    cairo_set_line_width(cr, 2.0 / zoom);
                    cairo_stroke(cr);
                } else {
                    cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.50);
                    cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgba(cr, 0.4, 0.0, 0.0, 0.6);
                    cairo_set_line_width(cr, 1.0 / zoom);
                    cairo_stroke(cr);
                }
            }
        }

        cairo_restore(cr);

        /* 6. Leyenda (pixeles de pantalla, fuera del transform). */
        cairo_new_path(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
        cairo_rectangle(cr, 10, height - 370, 120, 360);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_new_path(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, 20, height - 345);
        cairo_show_text(cr, "Magnitude");

        /* Escala de magnitudes M4..M8, en vertical. */
        {
            double mag_base_x = 40;
            double mag_base_y = height - 310;
            int mag;

            for (mag = 4; mag <= 8; mag++) {
                double r = mag * 2.0;
                char lbl[8];

                cairo_new_path(cr);
                cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.6);
                cairo_arc(cr, mag_base_x, mag_base_y, r, 0, 2 * M_PI);
                cairo_fill_preserve(cr);

                cairo_set_source_rgba(cr, 0.4, 0.0, 0.0, 0.8);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);

                cairo_new_path(cr);
                snprintf(lbl, sizeof(lbl), "M%d", mag);
                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                cairo_set_font_size(cr, 12.0);
                cairo_move_to(cr, mag_base_x + 25, mag_base_y + 4);
                cairo_show_text(cr, lbl);

                mag_base_y += 35;
            }
        }

        /* Escala de elevacion: triangulo de muestra + barra. */
        {
            double t_x = 25.0;
            double t_y = height - 120.0;
            double leg_s_size = m->cfg->station_size;
            cairo_pattern_t *pat;
            int min_val, max_val;
            char min_el[32], max_el[32];

            cairo_new_path(cr);
            cairo_set_source_rgb(cr, 0.0, 0.8, 0.0);
            cairo_move_to(cr, t_x, t_y - leg_s_size);
            cairo_line_to(cr, t_x - leg_s_size, t_y + leg_s_size);
            cairo_line_to(cr, t_x + leg_s_size, t_y + leg_s_size);
            cairo_close_path(cr);
            cairo_fill_preserve(cr);

            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            cairo_new_path(cr);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_font_size(cr, 12.0);
            cairo_move_to(cr, t_x + 12.0, t_y + 4.0);
            cairo_show_text(cr, "Elevation (m)");

            pat = cairo_pattern_create_linear(0, height - 20, 0, height - 100);
            cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.6, 1.0, 0.6);
            cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.0, 0.3, 0.0);

            cairo_new_path(cr);
            cairo_rectangle(cr, 25, height - 100, 20, 80);
            cairo_set_source(cr, pat);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);

            cairo_new_path(cr);
            cairo_rectangle(cr, 25, height - 100, 20, 80);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            min_val = (int)(floor(s->elev_min / 100.0) * 100);
            if (min_val < 0 && s->elev_min >= 0.0)
                min_val = 0;
            max_val = (int)(ceil(s->elev_max / 100.0) * 100);

            snprintf(min_el, sizeof(min_el), "%d", min_val);
            snprintf(max_el, sizeof(max_el), "%d", max_val);

            cairo_new_path(cr);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_font_size(cr, 14.0);
            cairo_move_to(cr, 55, height - 20);
            cairo_show_text(cr, min_el);
            cairo_move_to(cr, 55, height - 90);
            cairo_show_text(cr, max_el);
        }
    } else {
        /* Fallback: sin mapa, fondo claro y aviso (csnstaevdisp.c:718-729). */
        cairo_set_source_rgb(cr, 0.85, 0.92, 0.98);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, width / 2 - 120, height / 2);
        cairo_show_text(cr, "Missing map file (Equirectangular)");
    }

    /* 7. Marco perimetral. */
    cairo_new_path(cr);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 3.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_stroke(cr);
}

void sev_map_paint_bgra(const SevMap *m, const SevMapView *v,
                        const SevSnapshot *s, unsigned char *bgra,
                        int w, int h, int stride)
{
    cairo_surface_t *surf;
    cairo_t *cr;

    if (!bgra || w <= 0 || h <= 0 || stride < w * 4)
        return;

    surf = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32,
                                               w, h, stride);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return;
    }
    cr = cairo_create(surf);
    if (cairo_status(cr) == CAIRO_STATUS_SUCCESS)
        sev_map_draw(cr, w, h, m, v, s);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    cairo_surface_destroy(surf);
}

int sev_map_export(const SevMap *m, const SevMapView *v, const SevSnapshot *s,
                   const char *path, SevFmt fmt, int w, int h)
{
    cairo_surface_t *surf = NULL;
    cairo_t *cr;
    int rc = 0;

    if (!path || *path == '\0' || w <= 0 || h <= 0)
        return -1;

    if (fmt == SEV_FMT_SVG)
        surf = cairo_svg_surface_create(path, w, h);
    else if (fmt == SEV_FMT_PDF)
        surf = cairo_pdf_surface_create(path, w, h);
    else
        surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);

    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return -1;
    }

    cr = cairo_create(surf);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        return -1;
    }
    sev_map_draw(cr, w, h, m, v, s);
    if (fmt != SEV_FMT_PNG)
        cairo_show_page(cr);
    cairo_destroy(cr);

    if (fmt == SEV_FMT_PNG) {
        if (cairo_surface_write_to_png(surf, path) != CAIRO_STATUS_SUCCESS)
            rc = -1;
    } else {
        cairo_surface_finish(surf);
        if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
            rc = -1;
    }
    cairo_surface_destroy(surf);
    return rc;
}
