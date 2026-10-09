/*
 * Implementación GTK3 de EwGuiCanvas (GtkDrawingArea + signal "draw").
 * Ver ewgui/view.h. Al migrar a GTK4 se sustituye por view_gtk4.c sin tocar
 * a los módulos.
 */
#include "ewgui/view.h"

struct EwGuiCanvas {
    GtkWidget *widget;
    EwDrawFn   draw;
    void      *ud;
};

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    EwGuiCanvas *c = data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    if (c->draw)
        c->draw(c, cr, w, h, c->ud);
    return TRUE;
}

EwGuiCanvas *ewgui_canvas_new(void)
{
    EwGuiCanvas *c = g_malloc0(sizeof(*c));
    c->widget = gtk_drawing_area_new();
    g_signal_connect(c->widget, "draw", G_CALLBACK(on_draw), c);
    return c;
}

void ewgui_canvas_free(EwGuiCanvas *c)
{
    if (!c)
        return;
    if (c->widget)
        gtk_widget_destroy(c->widget);
    g_free(c);
}

void ewgui_canvas_set_draw(EwGuiCanvas *c, EwDrawFn fn, void *user_data)
{
    if (!c)
        return;
    c->draw = fn;
    c->ud = user_data;
}

void ewgui_canvas_render(EwGuiCanvas *c, cairo_t *cr, int width, int height)
{
    if (c && c->draw)
        c->draw(c, cr, width, height, c->ud);
}

void ewgui_canvas_get_draw(EwGuiCanvas *c, EwDrawFn *fn, void **user_data)
{
    if (fn) *fn = c ? c->draw : NULL;
    if (user_data) *user_data = c ? c->ud : NULL;
}

void ewgui_canvas_queue_draw(EwGuiCanvas *c)
{
    if (c && c->widget)
        gtk_widget_queue_draw(c->widget);
}

void ewgui_canvas_queue_draw_rect(EwGuiCanvas *c, int x, int y, int w, int h)
{
    if (c && c->widget)
        gtk_widget_queue_draw_area(c->widget, x, y, w, h);
}

void ewgui_canvas_get_size(EwGuiCanvas *c, int *width, int *height)
{
    if (!c || !c->widget) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    if (width) *width = gtk_widget_get_allocated_width(c->widget);
    if (height) *height = gtk_widget_get_allocated_height(c->widget);
}

GtkWidget *ewgui_canvas_widget(EwGuiCanvas *c)
{
    return c ? c->widget : NULL;
}
