/*
 * UI de export para GTK4: diálogo "Guardar" + volcado del canvas.
 * Vive en un archivo *gtk* (capa de vista) y SOLO se compila en el build GTK4;
 * en el build GTK3 no se incluye (ver common/Makefile). Ver ewgui/export.h.
 */
#include <gtk/gtk.h>

#include "ewgui/export.h"

static void on_export_saved(GObject *src, GAsyncResult *res, gpointer data)
{
    EwGuiCanvas *canvas = data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);

    if (!file) {
        if (err) {
            fprintf(stderr, "ewgui_export: cancelado o error: %s\n", err->message);
            g_error_free(err);
        }
        g_object_unref(src);
        return;
    }

    char *path = g_file_get_path(file);
    if (path) {
        EwExportFormat fmt = ewgui_export_format_from_path(path);
        if (fmt == EW_EXPORT_UNKNOWN)
            fmt = EW_EXPORT_SVG;
        int w = 0, h = 0;
        ewgui_canvas_get_size(canvas, &w, &h);
        if (w <= 0) w = 1024;
        if (h <= 0) h = 768;
        double scale = (fmt == EW_EXPORT_PNG) ? 2.0 : 1.0;
        int rc = ewgui_canvas_export(canvas, path, fmt, w, h, scale);
        if (rc == 0) fprintf(stderr, "ewgui_export: guardado %s\n", path);
        else         fprintf(stderr, "ewgui_export: ERROR al guardar %s\n", path);
        g_free(path);
    }
    g_object_unref(file);
    g_object_unref(src);
}

void ewgui_export_dialog_run(EwGuiCanvas *canvas, GtkWindow *parent)
{
    if (!canvas)
        return;

    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Export view");
    gtk_file_dialog_set_initial_name(dlg, "csn_view.svg");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "Imagenes (SVG, PDF, PNG)");
    gtk_file_filter_add_suffix(f, "svg");
    gtk_file_filter_add_suffix(f, "pdf");
    gtk_file_filter_add_suffix(f, "png");
    g_list_store_append(filters, f);
    g_object_unref(f);
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
    g_object_unref(filters);

    /* Mantenemos nuestra referencia hasta el callback (on_export_saved la suelta). */
    gtk_file_dialog_save(dlg, parent, NULL, on_export_saved, canvas);
}
