#ifndef EWGUI_EXPORT_H
#define EWGUI_EXPORT_H

#include <cairo.h>

#include "ewgui/view.h"

/*
 * Exportación de un canvas a SVG/PDF/PNG/PS reutilizando EXACTAMENTE el mismo
 * callback de dibujo que pinta en pantalla (EwDrawFn). El resultado es fiel a
 * la vista, en tamaño lógico configurable.
 *
 * El render es puro cairo (no usa GTK), así que es testeable headless con un
 * callback propio y canvas == NULL (ver tests/test_export.c).
 */

/* Formatos soportados. UNKNOWN se usa como valor de error. */
typedef enum {
    EW_EXPORT_SVG = 0,
    EW_EXPORT_PDF,
    EW_EXPORT_PNG,
    EW_EXPORT_PS,
    EW_EXPORT_UNKNOWN = -1
} EwExportFormat;

/* Infiere el formato por la extensión del path (.svg/.pdf/.png/.ps, sin
 * distinguir mayúsculas). Devuelve EW_EXPORT_UNKNOWN si no lo reconoce. */
EwExportFormat ewgui_export_format_from_path(const char *path);

/*
 * Vuelca la escena `fn` a un archivo.
 *   fn/user_data  callback de dibujo y su contexto (el mismo del canvas)
 *   path          archivo destino (sobrescribe)
 *   fmt           formato de salida
 *   width/height  tamaño lógico del dibujo; debe ser > 0
 *   scale         factor del ráster PNG (> 0); los vectoriales lo ignoran
 * Devuelve 0 si OK, -1 si los argumentos son inválidos o cairo falla al
 * crear/escribir la superficie (no deja el proceso en estado inconsistente).
 */
int ewgui_export_render(EwDrawFn fn, void *user_data, const char *path,
                        EwExportFormat fmt, int width, int height, double scale);

/* Conveniencia para la UI: exporta el canvas con su draw callback y, si
 * width/height son <= 0, usa el tamaño actual del canvas. */
int ewgui_canvas_export(EwGuiCanvas *canvas, const char *path,
                        EwExportFormat fmt, int width, int height, double scale);

/* UI (solo build GTK4): abre un diálogo "Guardar" y exporta `canvas` al
 * archivo elegido; el formato se infiere por la extensión (default SVG).
 * La definición vive en ewgui_export_ui_gtk4.c (no se compila en GTK3). */
void ewgui_export_dialog_run(EwGuiCanvas *canvas, GtkWindow *parent);

#endif /* EWGUI_EXPORT_H */
