/* Implementacion GTK4 del helper de tablas ordenables (ewgui/table_ui.h).
 *
 * Piezas:
 *   - un GtkCustomSorter por columna que lee la propiedad `sort_prop` del item
 *     (string/double/int) y la compara con ewgui_table_cmp_values();
 *   - el sorter de la vista (gtk_column_view_get_sorter(), un
 *     GtkColumnViewSorter) se pone en el GtkSortListModel, que es lo que hace
 *     que las cabeceras ordenen de verdad;
 *   - la direccion del PRIMER clic se declara por columna: GTK arranca en
 *     ascendente, asi que si la columna pide descendente se re-aplica en el
 *     primer ::changed (el segundo clic ya invierte solo).
 *
 * Sin popups a proposito: los popups de GTK quedan detras de la ventana bajo
 * WSLg/Wayland (bug #1299). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "ewgui/table_ui.h"

#define FIRST_DIR_KEY "ewgui-first-dir"
#define LAST_COL_KEY  "ewgui-last-col"

/* Texto comparable de una propiedad del item, sea string, double o int. */
static void prop_to_text(GObject *obj, const char *prop, char *buf, size_t n)
{
    GParamSpec *ps;
    GValue v = G_VALUE_INIT;

    buf[0] = '\0';
    if (!obj || !prop || !prop[0])
        return;
    ps = g_object_class_find_property(G_OBJECT_GET_CLASS(obj), prop);
    if (!ps)
        return;
    g_value_init(&v, ps->value_type);
    g_object_get_property(obj, prop, &v);
    if (G_VALUE_HOLDS_STRING(&v)) {
        const char *s = g_value_get_string(&v);
        snprintf(buf, n, "%s", s ? s : "");
    } else if (G_VALUE_HOLDS_DOUBLE(&v)) {
        double d = g_value_get_double(&v);
        if (!isnan(d))                    /* NaN = sin dato -> "" (= 0) */
            snprintf(buf, n, "%.6f", d);
    } else if (G_VALUE_HOLDS_FLOAT(&v)) {
        float f = g_value_get_float(&v);
        if (!isnan(f))
            snprintf(buf, n, "%.6f", (double)f);
    } else if (G_VALUE_HOLDS_INT(&v)) {
        snprintf(buf, n, "%d", g_value_get_int(&v));
    } else if (G_VALUE_HOLDS_UINT(&v)) {
        snprintf(buf, n, "%u", g_value_get_uint(&v));
    } else if (G_VALUE_HOLDS_INT64(&v)) {
        snprintf(buf, n, "%lld", (long long)g_value_get_int64(&v));
    } else if (G_VALUE_HOLDS_BOOLEAN(&v)) {
        snprintf(buf, n, "%d", g_value_get_boolean(&v) ? 1 : 0);
    }
    g_value_unset(&v);
}

/* user_data = "sort_prop\0tiebreak_prop" (el desempate puede ir vacio). */
static int cmp_items(gconstpointer a, gconstpointer b, gpointer user_data)
{
    const char *props = user_data;
    const char *tie = props + strlen(props) + 1;
    char va[64], vb[64], ta[64], tb[64];
    int r;

    prop_to_text(G_OBJECT(a), props, va, sizeof va);
    prop_to_text(G_OBJECT(b), props, vb, sizeof vb);
    r = ewgui_table_cmp_values(va, vb);
    if (r == 0 && tie[0]) {
        prop_to_text(G_OBJECT(a), tie, ta, sizeof ta);
        prop_to_text(G_OBJECT(b), tie, tb, sizeof tb);
        r = ewgui_table_cmp_values(ta, tb);
    }
    return r;
}

static void on_sorter_changed(GtkSorter *sorter, GtkSorterChange change,
                              gpointer user_data)
{
    GtkColumnView *view = user_data;
    GtkColumnViewColumn *col, *last;
    GtkSortType order;
    int want;

    (void)change;
    if (!GTK_IS_COLUMN_VIEW_SORTER(sorter))
        return;
    col = gtk_column_view_sorter_get_primary_sort_column(GTK_COLUMN_VIEW_SORTER(sorter));
    order = gtk_column_view_sorter_get_primary_sort_order(GTK_COLUMN_VIEW_SORTER(sorter));
    last = g_object_get_data(G_OBJECT(view), LAST_COL_KEY);
    if (!col || col == last) {
        g_object_set_data(G_OBJECT(view), LAST_COL_KEY, col);
        return;
    }
    g_object_set_data(G_OBJECT(view), LAST_COL_KEY, col);
    want = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), FIRST_DIR_KEY));
    /* Primer clic en esta columna: GTK arranca ascendente; si la columna pide
     * descendente se re-aplica (el segundo clic ya invierte por su cuenta). */
    if (order == GTK_SORT_ASCENDING && want == EWGUI_TABLE_DESC)
        gtk_column_view_sort_by_column(view, col, GTK_SORT_DESCENDING);
}

void ewgui_table_column_sortable(GtkColumnViewColumn *col, const char *sort_prop,
                                 const char *tiebreak_prop, int first_dir)
{
    char *props;
    size_t ls, lt;
    GtkCustomSorter *sorter;

    if (!col || !sort_prop || !sort_prop[0])
        return;
    ls = strlen(sort_prop);
    lt = tiebreak_prop ? strlen(tiebreak_prop) : 0;
    props = g_malloc(ls + 1 + lt + 1);
    memcpy(props, sort_prop, ls + 1);
    memcpy(props + ls + 1, tiebreak_prop ? tiebreak_prop : "", lt + 1);

    sorter = gtk_custom_sorter_new(cmp_items, props, g_free);
    gtk_column_view_column_set_sorter(col, GTK_SORTER(sorter));
    g_object_unref(sorter);
    g_object_set_data(G_OBJECT(col), FIRST_DIR_KEY, GINT_TO_POINTER(first_dir));
}

void ewgui_table_enable_sorting(GtkColumnView *view, GtkSortListModel *model,
                                GtkColumnViewColumn *initial, int initial_dir)
{
    GtkSorter *sorter;

    if (!view || !model)
        return;
    sorter = gtk_column_view_get_sorter(view);
    if (sorter) {
        gtk_sort_list_model_set_sorter(model, sorter);
        g_signal_connect(sorter, "changed", G_CALLBACK(on_sorter_changed), view);
    }
    if (initial)
        gtk_column_view_sort_by_column(view, initial, (GtkSortType)initial_dir);
}
