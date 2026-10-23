/* Mapa equirectangular con cairo: port de `on_draw_map` (csnrv.c:556-649).
 *
 * El fondo lo decodifica la UI con QImage y llega como BGRA (`crv_map_set_
 * background`); asi la capa C no arrastra libjpeg y el `puente` sigue siendo
 * solo este header. El resto (grilla, overlays .lines, epicentro, borde) se
 * dibuja con cairo, igual que el GTK.
 *
 * `crv_map_paint_bgra` es lo unico que ve `ewguiqt::CanvasWidget`; el export
 * re-renderiza la MISMA escena a un surface SVG/PDF/PNG (patron
 * bpq_scene_export), no copia el buffer de pantalla.
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

#include "crv_capi.h"
#include "crv_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ZOOM_MIN     0.2
#define ZOOM_MAX     50.0
#define ZOOM_DEFAULT 8.0

struct CrvMap {
    const CrvConfig *cfg;       /* prestada */
    EwOverlay *ov;
    unsigned char *bg;          /* copia BGRA del fondo */
    int bg_w, bg_h, bg_stride;
    cairo_surface_t *bg_surface;
};

typedef struct {
    double img_w, img_h, zoom, epi_x, epi_y, cx, cy;
} CsMapCtx;

static void overlay_map(double lat, double lon, void *ud,
                        double *sx, double *sy)
{
    CsMapCtx *c = ud;
    double wx, wy;

    ewgui_geo_project(lat, lon, c->img_w, c->img_h, &wx, &wy);
    *sx = (wx - c->epi_x) * c->zoom + c->cx;
    *sy = (wy - c->epi_y) * c->zoom + c->cy;
}

CrvMap *crv_map_new(const CrvConfig *cfg)
{
    CrvMap *m;

    if (!cfg)
        return NULL;
    m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->cfg = cfg;
    m->ov = crv_overlays_load(cfg, stderr);
    return m;
}

void crv_map_free(CrvMap *m)
{
    if (!m)
        return;
    if (m->bg_surface)
        cairo_surface_destroy(m->bg_surface);
    free(m->bg);
    ewgui_overlay_free(m->ov);
    free(m);
}

void crv_map_set_background(CrvMap *m, const unsigned char *bgra,
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

int crv_map_has_background(const CrvMap *m)
{
    return (m && m->bg_surface) ? 1 : 0;
}

void crv_map_reload(CrvMap *m)
{
    if (!m)
        return;
    ewgui_overlay_free(m->ov);
    m->ov = crv_overlays_load(m->cfg, stderr);
}

int crv_map_nlayers(const CrvMap *m)
{
    return (m && m->ov) ? (int)m->ov->nlayers : 0;
}

int crv_map_layer_info(const CrvMap *m, int i, CrvLayerInfo *out)
{
    const EwOverlayLayer *L;
    size_t k, npt = 0;

    if (!m || !m->ov || !out || i < 0 || i >= (int)m->ov->nlayers)
        return -1;
    L = &m->ov->layers[i];
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", L->name);
    out->visible = L->visible;
    out->rgb = ((unsigned)(L->color[0] * 255.0 + 0.5) << 16) |
               ((unsigned)(L->color[1] * 255.0 + 0.5) << 8) |
               (unsigned)(L->color[2] * 255.0 + 0.5);
    for (k = 0; k < L->nlines; k++)
        npt += L->lines[k].npts;
    out->polylines = (int)L->nlines;
    out->vertices = (int)npt;
    return 0;
}

int crv_map_set_visible(CrvMap *m, int i, int visible)
{
    const EwOverlayLayer *L;

    if (!m || !m->ov || i < 0 || i >= (int)m->ov->nlayers)
        return -1;
    L = &m->ov->layers[i];
    return ewgui_overlay_set_visible(m->ov, L->name, visible) > 0 ? 0 : -1;
}

void crv_map_view_init(CrvMapView *v, double initial_zoom)
{
    if (!v)
        return;
    if (initial_zoom < ZOOM_MIN || initial_zoom > ZOOM_MAX)
        initial_zoom = ZOOM_DEFAULT;
    memset(v, 0, sizeof(*v));
    v->zoom = initial_zoom;
    v->initial_zoom = initial_zoom;
}

void crv_map_reset(CrvMapView *v)
{
    if (!v)
        return;
    v->zoom = v->initial_zoom;
    v->pan_x = 0.0;
    v->pan_y = 0.0;
}

void crv_map_zoom_factor(CrvMapView *v, double factor)
{
    if (!v || factor <= 0.0)
        return;
    v->zoom *= factor;
    if (v->zoom < ZOOM_MIN)
        v->zoom = ZOOM_MIN;
    if (v->zoom > ZOOM_MAX)
        v->zoom = ZOOM_MAX;
}

void crv_map_zoom_at(CrvMapView *v, int w, int h,
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
         * pan tiene que quedar intacto (con el pedido, derivaria en cada
         * rueda contra el tope). */
        applied = z1 / z0;
        v->pan_x = mx - w / 2.0 -
                   (mx - w / 2.0 - v->pan_x) * applied;
        v->pan_y = my - h / 2.0 -
                   (my - h / 2.0 - v->pan_y) * applied;
    }
    v->zoom = z1;
}

void crv_map_pan(CrvMapView *v, double dx, double dy)
{
    if (!v)
        return;
    v->pan_x += dx;
    v->pan_y += dy;
}

/* --- Proyeccion ---------------------------------------------------------- */

static double effective_zoom(const CrvMap *m, const CrvMapView *v,
                             const CrvSnapshot *s, int w, int h)
{
    if (s && s->has_data)
        return v->zoom;
    return fmin((double)w / (double)m->bg_w, (double)h / (double)m->bg_h);
}

void crv_map_project(const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s,
                     int w, int h, double lat, double lon,
                     double *x, double *y)
{
    double img_w, img_h, epi_x, epi_y, zoom, wx, wy;

    if (x) *x = 0.0;
    if (y) *y = 0.0;
    if (!m || !v || !m->bg_surface || m->bg_w <= 0 || m->bg_h <= 0 ||
        w <= 0 || h <= 0)
        return;

    img_w = (double)m->bg_w;
    img_h = (double)m->bg_h;
    epi_x = img_w / 2.0;
    epi_y = img_h / 2.0;
    if (s && s->has_data)
        ewgui_geo_project(s->lat, s->lon, img_w, img_h, &epi_x, &epi_y);

    zoom = effective_zoom(m, v, s, w, h);
    ewgui_geo_project(lat, lon, img_w, img_h, &wx, &wy);
    if (x) *x = (wx - epi_x) * zoom + (double)w / 2.0 + v->pan_x;
    if (y) *y = (wy - epi_y) * zoom + (double)h / 2.0 + v->pan_y;
}

/* --- Dibujo -------------------------------------------------------------- */

void crv_map_draw(cairo_t *cr, int width, int height,
                  const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s)
{
    if (!cr || width <= 0 || height <= 0)
        return;

    if (m && m->bg_surface) {
        double img_w = (double)m->bg_w;
        double img_h = (double)m->bg_h;
        double epi_x = img_w / 2.0;
        double epi_y = img_h / 2.0;
        double zoom;
        double step;
        char lbl[32];
        double lon, lat;

        if (s && s->has_data)
            ewgui_geo_project(s->lat, s->lon, img_w, img_h, &epi_x, &epi_y);
        zoom = effective_zoom(m, v, s, width, height);

        cairo_save(cr);
        cairo_translate(cr, width / 2.0 + v->pan_x, height / 2.0 + v->pan_y);
        cairo_scale(cr, zoom, zoom);
        cairo_translate(cr, -epi_x, -epi_y);
        cairo_set_source_surface(cr, m->bg_surface, 0, 0);
        cairo_paint(cr);

        step = ewgui_geo_grid_step(zoom);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
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

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
        cairo_set_font_size(cr, 12.0 / zoom);

        {
            double world_top_y = epi_y - (height / 2.0 + v->pan_y) / zoom;
            double world_left_x = epi_x - (width / 2.0 + v->pan_x) / zoom;
            if (world_top_y < 0)
                world_top_y = 0;
            if (world_left_x < 0)
                world_left_x = 0;

            for (lon = -180.0; lon <= 180.0; lon += step) {
                double x;
                ewgui_geo_project(0.0, lon, img_w, img_h, &x, NULL);
                cairo_move_to(cr, x + 4.0 / zoom, world_top_y + 14.0 / zoom);
                snprintf(lbl, sizeof(lbl), "%.0f\xC2\xB0", lon);
                cairo_show_text(cr, lbl);
            }
            for (lat = -90.0; lat <= 90.0; lat += step) {
                double y;
                ewgui_geo_project(lat, 0.0, img_w, img_h, NULL, &y);
                cairo_move_to(cr, world_left_x + 4.0 / zoom, y - 4.0 / zoom);
                snprintf(lbl, sizeof(lbl), "%.0f\xC2\xB0", lat);
                cairo_show_text(cr, lbl);
            }
        }
        cairo_restore(cr);

        if (m->ov) {
            CsMapCtx octx = { img_w, img_h, zoom, epi_x, epi_y,
                              width / 2.0 + v->pan_x,
                              height / 2.0 + v->pan_y };
            ewgui_overlay_draw(cr, m->ov, overlay_map, &octx, 1.0);
        }
    } else {
        cairo_set_source_rgb(cr, 0.85, 0.92, 0.98);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, width / 2.0 - 120.0, height / 2.0);
        cairo_show_text(cr, "Falta imagen de mapa");
    }

    if (s && s->has_data) {
        double ex = width / 2.0 + v->pan_x;
        double ey = height / 2.0 + v->pan_y;

        cairo_new_path(cr);
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_arc(cr, ex, ey, 6.0, 0.0, 2.0 * M_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }

    cairo_new_path(cr);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 3.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_stroke(cr);
}

void crv_map_paint_bgra(const CrvMap *m, const CrvMapView *v,
                        const CrvSnapshot *s, unsigned char *bgra,
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
        crv_map_draw(cr, w, h, m, v, s);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    cairo_surface_destroy(surf);
}

int crv_map_export(const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s,
                   const char *path, CrvFmt fmt, int w, int h)
{
    cairo_surface_t *surf = NULL;
    cairo_t *cr;
    int rc = 0;

    if (!path || *path == '\0' || w <= 0 || h <= 0)
        return -1;

    if (fmt == CRV_FMT_SVG)
        surf = cairo_svg_surface_create(path, w, h);
    else if (fmt == CRV_FMT_PDF)
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
    crv_map_draw(cr, w, h, m, v, s);
    if (fmt != CRV_FMT_PNG)
        cairo_show_page(cr);
    cairo_destroy(cr);

    if (fmt == CRV_FMT_PNG) {
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
