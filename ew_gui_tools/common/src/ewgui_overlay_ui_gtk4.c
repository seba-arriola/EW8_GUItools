/*
 * UI GTK4 compartida para editar la capa de líneas.
 * Ventana modal con una fila por capa: nombre, visibilidad y color.
 * Solo se compila en el build GTK4 (ver common/Makefile).
 */
#include "ewgui/overlay_ui.h"

#include <gtk/gtk.h>

typedef struct {
    EwOverlay          *ov;
    int                 index;
    GtkWidget          *button;      /* muestra el hex actual */
    EwOverlayChangedFn  on_changed;
    void               *ud;
} OvRow;

static void rgb_to_hex(const double rgb[3], char out[8])
{
    int r = (int)(rgb[0] * 255.0 + 0.5);
    int g = (int)(rgb[1] * 255.0 + 0.5);
    int b = (int)(rgb[2] * 255.0 + 0.5);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    snprintf(out, 8, "%02X%02X%02X", r, g, b);
}

static void row_sync_button(OvRow *row)
{
    char hex[8];
    rgb_to_hex(row->ov->layers[row->index].color, hex);
    gtk_button_set_label(GTK_BUTTON(row->button), hex);
}

static void on_color_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    OvRow *row = data;
    GdkRGBA *c = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src), res, NULL);
    if (c) {
        double *col = row->ov->layers[row->index].color;
        col[0] = c->red;
        col[1] = c->green;
        col[2] = c->blue;
        gdk_rgba_free(c);
        row_sync_button(row);
        if (row->on_changed)
            row->on_changed(row->ud);
    }
    g_object_unref(src);
}

static void on_color_clicked(GtkButton *btn, gpointer data)
{
    OvRow *row = data;
    const double *col = row->ov->layers[row->index].color;
    GdkRGBA cur = {0};
    GtkColorDialog *cd;
    (void)btn;
    cur.red = col[0];
    cur.green = col[1];
    cur.blue = col[2];
    cur.alpha = 1.0;
    cd = gtk_color_dialog_new();
    gtk_color_dialog_set_title(cd, row->ov->layers[row->index].name);
    gtk_color_dialog_choose_rgba(cd, NULL, &cur, NULL, on_color_chosen, row);
}

static void on_visible_toggled(GtkCheckButton *btn, gpointer data)
{
    OvRow *row = data;
    row->ov->layers[row->index].visible =
        gtk_check_button_get_active(btn) ? 1 : 0;
    if (row->on_changed)
        row->on_changed(row->ud);
}

void ewgui_overlay_dialog_run(EwOverlay *ov, GtkWindow *parent,
                              EwOverlayChangedFn on_changed, void *ud)
{
    GtkWidget *win, *vbox, *scrolled, *list, *hint, *close;
    size_t i;

    if (!ov)
        return;

    win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Overlay layers");
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 380, 320);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list);
    gtk_box_append(GTK_BOX(vbox), scrolled);

    for (i = 0; i < ov->nlayers; i++) {
        EwOverlayLayer *L = &ov->layers[i];
        GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *lbl = gtk_label_new(L->name);
        GtkWidget *chk = gtk_check_button_new();
        GtkWidget *btn = gtk_button_new();
        OvRow *row = g_new0(OvRow, 1);

        gtk_widget_set_margin_top(rowbox, 4);
        gtk_widget_set_margin_bottom(rowbox, 4);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(chk), L->visible);

        row->ov = ov;
        row->index = (int)i;
        row->button = btn;
        row->on_changed = on_changed;
        row->ud = ud;
        row_sync_button(row);

        /* el row vive mientras viva su fila */
        g_object_set_data_full(G_OBJECT(rowbox), "ov-row", row, g_free);
        g_signal_connect(chk, "toggled", G_CALLBACK(on_visible_toggled), row);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_color_clicked), row);

        gtk_box_append(GTK_BOX(rowbox), lbl);
        gtk_box_append(GTK_BOX(rowbox), chk);
        gtk_box_append(GTK_BOX(rowbox), btn);
        gtk_list_box_append(GTK_LIST_BOX(list), rowbox);
    }

    hint = gtk_label_new("Cambios en memoria: copia las lineas #layer al "
                         "archivo de datos para conservarlos.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(vbox), hint);

    close = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(vbox), close);

    gtk_window_present(GTK_WINDOW(win));
}
