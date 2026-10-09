/*
 * Exportación de un canvas a SVG/PDF/PNG/PS reutilizando su callback cairo.
 * Puro cairo (no GTK): solo necesita el EwDrawFn y un tamaño lógico.
 * Ver ewgui/export.h.
 */
#include "ewgui/export.h"

#include <cairo-svg.h>
#include <cairo-pdf.h>
#include <cairo-ps.h>
#include <string.h>

EwExportFormat ewgui_export_format_from_path(const char *path)
{
    const char *dot;

    if (!path)
        return EW_EXPORT_UNKNOWN;
    dot = strrchr(path, '.');
    if (!dot)
        return EW_EXPORT_UNKNOWN;
    if (!strcasecmp(dot, ".svg")) return EW_EXPORT_SVG;
    if (!strcasecmp(dot, ".pdf")) return EW_EXPORT_PDF;
    if (!strcasecmp(dot, ".png")) return EW_EXPORT_PNG;
    if (!strcasecmp(dot, ".ps"))  return EW_EXPORT_PS;
    return EW_EXPORT_UNKNOWN;
}

int ewgui_export_render(EwDrawFn fn, void *user_data, const char *path,
                        EwExportFormat fmt, int width, int height, double scale)
{
    cairo_surface_t *surface = NULL;
    cairo_t *cr;
    int ok = 0;

    if (!fn || !path || path[0] == '\0' || width <= 0 || height <= 0)
        return -1;
    if (!(scale > 0.0))
        scale = 1.0;

    switch (fmt) {
    case EW_EXPORT_SVG:
        surface = cairo_svg_surface_create(path, width, height);
        break;
    case EW_EXPORT_PDF:
        surface = cairo_pdf_surface_create(path, width, height);
        break;
    case EW_EXPORT_PS:
        surface = cairo_ps_surface_create(path, width, height);
        break;
    case EW_EXPORT_PNG:
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                             (int)(width  * scale + 0.5),
                                             (int)(height * scale + 0.5));
        break;
    default:
        return -1;
    }
    if (!surface)
        return -1;
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return -1;
    }

    cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return -1;
    }

    /* El canvas dibuja en unidades lógicas; para el ráster escalamos el
     * contexto para obtener más píxeles con la misma escena. */
    if (fmt == EW_EXPORT_PNG)
        cairo_scale(cr, scale, scale);

    fn(NULL, cr, width, height, user_data);

    if (fmt != EW_EXPORT_PNG)
        cairo_show_page(cr);   /* cierra la página del formato vectorial */
    cairo_destroy(cr);

    if (fmt == EW_EXPORT_PNG) {
        ok = (cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS);
    } else {
        /* Los vectoriales escriben en finish(); ahí se materializa un path
         * no escribible. */
        cairo_surface_finish(surface);
        ok = (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
    }
    cairo_surface_destroy(surface);

    return ok ? 0 : -1;
}

int ewgui_canvas_export(EwGuiCanvas *canvas, const char *path,
                        EwExportFormat fmt, int width, int height, double scale)
{
    EwDrawFn fn = NULL;
    void *ud = NULL;
    int w = width;
    int h = height;

    if (!canvas)
        return -1;
    ewgui_canvas_get_draw(canvas, &fn, &ud);
    if (!fn)
        return -1;
    if (w <= 0 || h <= 0)
        ewgui_canvas_get_size(canvas, &w, &h);
    return ewgui_export_render(fn, ud, path, fmt, w, h, scale);
}
