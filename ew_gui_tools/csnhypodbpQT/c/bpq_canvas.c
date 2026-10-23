/*
 * bpq_canvas.c - Lienzos del prototipo: cairo dibuja sobre un buffer de pixeles
 * que la UI Qt envuelve en un QImage (CAIRO_FORMAT_ARGB32 ==
 * QImage::Format_ARGB32_Premultiplied en little-endian).
 *
 * Aqui vive TODO el cairo del proyecto (regla R5 del plan): ningun translation
 * unit de ui/ incluye <cairo.h>. La UI solo pide "pinta esta escena en este
 * buffer" o "exporta esta escena a este archivo".
 *
 * Fase 0: el contenido de cada escena es todavia un marcador de posicion
 * (marco + grilla + cruz central) para poder verificar el camino
 * cairo -> buffer -> QImage sin depender de datos reales. Las fases 2/4/6
 * sustituyen cada escena por su dibujo real reutilizando los nucleos puros
 * (csnhypodbp_mapview_*, _profile_*, _view_*, ewgui_overlay_draw).
 */
#include "bpq_capi.h"

#include <math.h>
#include <string.h>

#include <cairo.h>
#include <cairo-pdf.h>
#include <cairo-svg.h>

#include "bpq_internal.h"

/* Color del marcador por escena: se usa en el test de pixeles (AC13). */
static void scene_bg(BpqScene s, double *r, double *g, double *b)
{
    switch (s) {
    case BPQ_SCENE_MAP_PLAN:   *r = 0.93; *g = 0.95; *b = 0.98; break; /* planta: azul muy claro */
    case BPQ_SCENE_MAP_PROF:   *r = 0.97; *g = 0.94; *b = 0.88; break; /* perfil: crema */
    case BPQ_SCENE_TRACES:     *r = 1.00; *g = 1.00; *b = 1.00; break; /* trazas: blanco */
    case BPQ_SCENE_TRACE_AXIS: *r = 0.90; *g = 0.90; *b = 0.90; break;
    case BPQ_SCENE_MAG:        *r = 1.00; *g = 1.00; *b = 1.00; break;
    case BPQ_SCENE_MAG_AXIS:   *r = 0.90; *g = 0.90; *b = 0.90; break;
    case BPQ_SCENE_MWW_BALL:   *r = 0.15; *g = 0.15; *b = 0.15; break;
    default:                   *r = 1.00; *g = 1.00; *b = 1.00; break;
    }
}

/* Dibujo de la escena. `w`/`h` en pixeles. Debe ser determinista: el test
 * compara pixeles concretos. Las escenas con dibujo real delegan en
 * bpq_map.c / bpq_profile.c; el resto usa el marcador de posicion. */
static void scene_draw(BpqScene s, cairo_t *cr, int w, int h, const void *ctx)
{
    if (s == BPQ_SCENE_MAP_PLAN && ctx) {
        const BpqMapCtx *c = (const BpqMapCtx *)ctx;
        bpq_map_draw(cr, w, h, c, c->map);
        return;
    }
    if (s == BPQ_SCENE_MAP_PROF && ctx) {
        bpq_profile_draw(cr, w, h, (const BpqProfileCtx *)ctx);
        return;
    }
    if (s == BPQ_SCENE_TRACES && ctx) {
        bpq_traces_draw(cr, w, h, (const BpqTracesCtx *)ctx);
        return;
    }
    if (s == BPQ_SCENE_TRACE_AXIS && ctx) {
        bpq_traces_draw_axis(cr, w, h, (const BpqTracesCtx *)ctx);
        return;
    }
    if (s == BPQ_SCENE_MAG && ctx) {
        bpq_mag_draw(cr, w, h, (const BpqMagCtx *)ctx);
        return;
    }
    if (s == BPQ_SCENE_MAG_AXIS && ctx) {
        bpq_mag_draw_axis(cr, w, h, (const BpqMagCtx *)ctx);
        return;
    }
    if (s == BPQ_SCENE_MWW_BALL && ctx) {
        bpq_mww_ball_draw(cr, w, h, (const BpqMww *)ctx);
        return;
    }
    bpq_scene_draw_placeholder(cr, w, h, s);
}

void bpq_scene_draw_placeholder(cairo_t *cr, int w, int h, BpqScene s)
{
    double r, g, b;
    int i;

    if (w <= 0 || h <= 0) return;

    scene_bg(s, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);

    /* grilla cada 50 px + marco: da estructura y hace verificable el render */
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.12);
    for (i = 50; i < w; i += 50) {
        cairo_move_to(cr, i + 0.5, 0.0);
        cairo_line_to(cr, i + 0.5, (double)h);
        cairo_stroke(cr);
    }
    for (i = 50; i < h; i += 50) {
        cairo_move_to(cr, 0.0, i + 0.5);
        cairo_line_to(cr, (double)w, i + 0.5);
        cairo_stroke(cr);
    }

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, 1.0, 1.0, (double)w - 2.0, (double)h - 2.0);
    cairo_stroke(cr);

    /* Cruz central: el test comprueba que el pixel del centro NO es el fondo. */
    cairo_set_source_rgb(cr, 0.80, 0.10, 0.10);
    cairo_set_line_width(cr, 3.0);
    cairo_move_to(cr, w * 0.5 - 20.0, h * 0.5);
    cairo_line_to(cr, w * 0.5 + 20.0, h * 0.5);
    cairo_move_to(cr, w * 0.5, h * 0.5 - 20.0);
    cairo_line_to(cr, w * 0.5, h * 0.5 + 20.0);
    cairo_stroke(cr);
}

const char *bpq_scene_name(BpqScene s)
{
    switch (s) {
    case BPQ_SCENE_MAP_PLAN:   return "MAP_PLAN";
    case BPQ_SCENE_MAP_PROF:   return "MAP_PROF";
    case BPQ_SCENE_TRACES:     return "TRACES";
    case BPQ_SCENE_TRACE_AXIS: return "TRACE_AXIS";
    case BPQ_SCENE_MAG:        return "MAG";
    case BPQ_SCENE_MAG_AXIS:   return "MAG_AXIS";
    case BPQ_SCENE_MWW_BALL:   return "MWW_BALL";
    default:                   return "?";
    }
}

int bpq_scene_min_size(BpqScene s, int *w, int *h)
{
    if (!w || !h) return -1;
    switch (s) {
    case BPQ_SCENE_MAP_PLAN:   *w = 160; *h = 140; return 0;  /* paridad csnhypodbp */
    case BPQ_SCENE_MAP_PROF:   *w = 160; *h = 100; return 0;
    case BPQ_SCENE_TRACE_AXIS: *w = 120; *h = 18;  return 0;
    case BPQ_SCENE_MAG_AXIS:   *w = 120; *h = 18;  return 0;
    case BPQ_SCENE_TRACES:     *w = 200; *h = 120; return 0;
    case BPQ_SCENE_MAG:        *w = 200; *h = 90;  return 0;
    case BPQ_SCENE_MWW_BALL:   *w = 120; *h = 120; return 0;
    default: return -1;
    }
}

/*
 * Pinta la escena en un buffer BGRA premultiplicado de `h` filas de `stride`
 * bytes. Devuelve 0 o -1. `stride` <= 0 usa 4*w.
 */
int bpq_scene_paint(BpqScene s, const void *ctx, unsigned char *bgra,
                    int w, int h, int stride)
{
    cairo_surface_t *surf;
    cairo_t *cr;
    cairo_status_t st;

    if (!bgra || w <= 0 || h <= 0) return -1;
    if (stride <= 0) stride = 4 * w;
    if (stride < 4 * w) return -1;

    surf = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32,
                                               w, h, stride);
    if (!surf) return -1;
    st = cairo_surface_status(surf);
    if (st != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return -1;
    }
    cr = cairo_create(surf);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        return -1;
    }

    scene_draw(s, cr, w, h, ctx);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}

/*
 * Exporta la escena a PNG/SVG/PDF. Reemplaza a ewgui_export_render, que no se
 * puede compilar sin GTK (ewgui/export.h -> view.h -> <gtk/gtk.h>).
 */
int bpq_scene_export(BpqScene s, const void *ctx, const char *path, BpqFmt fmt,
                     int w, int h, double scale)
{
    cairo_surface_t *surf = NULL;
    cairo_t *cr;
    cairo_status_t st;

    if (!path || !path[0] || w <= 0 || h <= 0) return -1;
    if (scale <= 0.0) scale = 1.0;

    switch (fmt) {
    case BPQ_FMT_SVG:
        surf = cairo_svg_surface_create(path, w * scale, h * scale);
        break;
    case BPQ_FMT_PDF:
        surf = cairo_pdf_surface_create(path, w * scale, h * scale);
        break;
    case BPQ_FMT_PNG:
        surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        break;
    default:
        return -1;
    }
    if (!surf) return -1;
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
    cairo_scale(cr, scale, scale);
    scene_draw(s, cr, w, h, ctx);
    cairo_show_page(cr);
    cairo_destroy(cr);

    if (fmt == BPQ_FMT_PNG) {
        st = cairo_surface_write_to_png(surf, path);
        cairo_surface_destroy(surf);
        return (st == CAIRO_STATUS_SUCCESS) ? 0 : -1;
    }
    cairo_surface_finish(surf);
    st = cairo_surface_status(surf);
    cairo_surface_destroy(surf);
    return (st == CAIRO_STATUS_SUCCESS) ? 0 : -1;
}
