#ifndef EWGUI_VIEW_H
#define EWGUI_VIEW_H

#include <cairo.h>
#include <gtk/gtk.h>

/*
 * Abstracción de canvas de dibujo. Es el ÚNICO punto que cambia al migrar a
 * GTK4: la implementación (ewgui_view_gtk3.c) usa GtkDrawingArea + signal
 * "draw"; la de GTK4 usará set_draw_func/snapshot o GtkGLArea, con la misma
 * API. Los módulos NO deben usar GtkDrawingArea directamente.
 *
 * Nota: este header SÍ incluye GTK (es la capa de vista); está exento de la
 * regla "core/dsp/wave/geo sin GTK" (ver tests/check_no_gtk.sh).
 */

typedef struct EwGuiCanvas EwGuiCanvas;

/* Callback de dibujo. `width`/`height` son el tamaño asignado del canvas. */
typedef void (*EwDrawFn)(EwGuiCanvas *canvas, cairo_t *cr,
                         int width, int height, void *user_data);

EwGuiCanvas *ewgui_canvas_new(void);
void         ewgui_canvas_free(EwGuiCanvas *canvas);

void         ewgui_canvas_set_draw(EwGuiCanvas *canvas, EwDrawFn fn, void *user_data);

/* Reejecuta el callback de dibujo sobre un cairo_t cualquiera (p.ej. export a
 * SVG/PDF/PNG). No toca el widget ni el estado de la ventana. */
void         ewgui_canvas_render(EwGuiCanvas *canvas, cairo_t *cr, int width, int height);

/* Devuelve el callback y su user_data; permite exportar sin acoplarse a GTK. */
void         ewgui_canvas_get_draw(EwGuiCanvas *canvas, EwDrawFn *fn, void **user_data);

void         ewgui_canvas_queue_draw(EwGuiCanvas *canvas);
void         ewgui_canvas_queue_draw_rect(EwGuiCanvas *canvas, int x, int y, int w, int h);
void         ewgui_canvas_get_size(EwGuiCanvas *canvas, int *width, int *height);

/* Widget para empaquetar en el layout (y para añadir eventos de ratón). */
GtkWidget   *ewgui_canvas_widget(EwGuiCanvas *canvas);

#endif /* EWGUI_VIEW_H */
