/*
 * Implementación GTK4 de EwGuiCanvas (GtkDrawingArea + set_draw_func).
 * Ver ewgui/view.h. Mantiene la misma API que la de GTK3, así que los módulos
 * no cambian. El cairo_t que recibe el draw-func se integra en el snapshot de
 * GTK4; la optimización con GskPath/GLArea queda para más adelante (ver
 * docs/adr-0001-render-backend.md).
 */
#include "ewgui/view.h"

struct EwGuiCanvas {
    GtkWidget *widget;
    EwDrawFn   draw;
    void      *ud;
};

static void draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data)
{
    EwGuiCanvas *c = data;
    (void)area;
    if (c->draw)
        c->draw(c, cr, width, height, c->ud);
}

EwGuiCanvas *ewgui_canvas_new(void)
{
    EwGuiCanvas *c = g_malloc0(sizeof(*c));
    c->widget = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->widget), draw_func, c, NULL);
    return c;
}

void ewgui_canvas_free(EwGuiCanvas *c)
{
    if (!c)
        return;
    if (c->widget)
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->widget), NULL, NULL, NULL);
    g_free(c);
}

void ewgui_canvas_set_draw(EwGuiCanvas *c, EwDrawFn fn, void *user_data)
{
    if (!c)
        return;
    c->draw = fn;
    c->ud = user_data;
}

void ewgui_canvas_queue_draw(EwGuiCanvas *c)
{
    if (c && c->widget)
        gtk_widget_queue_draw(c->widget);
}

void ewgui_canvas_queue_draw_rect(EwGuiCanvas *c, int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    /* GTK4 no tiene queue_draw_area; se repinta el widget completo. */
    if (c && c->widget)
        gtk_widget_queue_draw(c->widget);
}

void ewgui_canvas_get_size(EwGuiCanvas *c, int *width, int *height)
{
    if (!c || !c->widget) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    if (width) *width = gtk_widget_get_width(c->widget);
    if (height) *height = gtk_widget_get_height(c->widget);
}

GtkWidget *ewgui_canvas_widget(EwGuiCanvas *c)
{
    return c ? c->widget : NULL;
}
