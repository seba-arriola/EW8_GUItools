/*
 * Menú propio de los módulos GUI como ventana modal (GTK4).
 * Evita `GtkMenuButton`/`GtkPopoverMenu` (superficie popup que bajo
 * WSLg/Wayland queda detrás de la ventana).
 * Solo se compila con GUI_TOOLKIT=gtk4 (ver common/Makefile).
 */
#include "ewgui/menu_ui.h"

#include <gtk/gtk.h>

typedef struct {
    const EwMenuGroup *groups;
    size_t             n;
} MenuCtx;

typedef struct {
    GtkWindow        *parent;    /* ventana con el GSimpleActionGroup "win" */
    GtkWindow        *menu_win;  /* ventana modal del menú */
    const EwMenuItem *it;
} ItemCtx;

static void on_item_clicked(GtkButton *btn, gpointer data)
{
    ItemCtx *ctx = data;
    GtkWindow *parent = ctx->parent;
    const EwMenuItem *it = ctx->it;
    (void)btn;

    /* Cierra primero: los diálogos que abra la acción (Export/Overlay/Events)
     * deben colgarse de la ventana principal, que sigue viva. */
    gtk_window_destroy(ctx->menu_win);

    if (!parent || !it->action)
        return;
    if (it->target_fmt)
        gtk_widget_activate_action(GTK_WIDGET(parent), it->action,
                                   it->target_fmt, it->target_int);
    else
        gtk_widget_activate_action(GTK_WIDGET(parent), it->action, NULL);
}

static void on_menu_btn_clicked(GtkButton *btn, gpointer data)
{
    /* Los grupos se leen del object-data (no del data de la señal) para que
       ewgui_menu_button_set_groups() pueda cambiarlos en caliente. */
    MenuCtx *ctx = g_object_get_data(G_OBJECT(btn), "ewgui-menu-ctx");
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
    GtkWindow *parent = root ? GTK_WINDOW(root) : NULL;
    (void)data;

    if (ctx)
        ewgui_menu_dialog_run(parent, ctx->groups, ctx->n);
}

GtkWidget *ewgui_menu_button_new(const EwMenuGroup *groups, size_t n)
{
    GtkWidget *btn = gtk_button_new_from_icon_name("open-menu-symbolic");
    MenuCtx *ctx = g_new0(MenuCtx, 1);

    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_tooltip_text(btn, "Menu");

    ctx->groups = groups;
    ctx->n = n;
    g_object_set_data_full(G_OBJECT(btn), "ewgui-menu-ctx", ctx, g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_btn_clicked), NULL);

    if (!groups || n == 0)
        gtk_widget_set_sensitive(btn, FALSE);

    return btn;
}

void ewgui_menu_button_set_groups(GtkWidget *btn, const EwMenuGroup *groups, size_t n)
{
    MenuCtx *ctx;
    if (!btn)
        return;
    ctx = g_new0(MenuCtx, 1);
    ctx->groups = groups;
    ctx->n = n;
    /* g_object_set_data_full libera el ctx anterior (mismo key). */
    g_object_set_data_full(G_OBJECT(btn), "ewgui-menu-ctx", ctx, g_free);
    gtk_widget_set_sensitive(btn, (groups && n > 0) ? TRUE : FALSE);
}

void ewgui_menu_dialog_run(GtkWindow *parent, const EwMenuGroup *groups, size_t n)
{
    GtkWidget *win, *vbox, *scrolled, *box, *close;
    size_t g, i;

    win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Menu");
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 360, 420);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    gtk_box_append(GTK_BOX(vbox), scrolled);

    for (g = 0; g < n; g++) {
        const EwMenuGroup *grp = &groups[g];

        if (g > 0) {
            GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_widget_set_margin_top(sep, 6);
            gtk_widget_set_margin_bottom(sep, 6);
            gtk_box_append(GTK_BOX(box), sep);
        }

        if (grp->label) {
            GtkWidget *head = gtk_label_new(grp->label);
            gtk_label_set_xalign(GTK_LABEL(head), 0.0);
            gtk_widget_add_css_class(head, "heading");
            gtk_widget_set_margin_top(head, 4);
            gtk_box_append(GTK_BOX(box), head);
        }

        for (i = 0; i < grp->n_items; i++) {
            const EwMenuItem *it = &grp->items[i];
            GtkWidget *btn;
            ItemCtx *ctx;

            if (!it->label) {
                GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                gtk_box_append(GTK_BOX(box), sep);
                continue;
            }
            if (!it->action)
                continue;   /* defensivo: ítem no interactivo */

            btn = gtk_button_new_with_label(it->label);
            gtk_widget_set_halign(btn, GTK_ALIGN_FILL);
            gtk_widget_add_css_class(btn, "flat");

            ctx = g_new0(ItemCtx, 1);
            ctx->parent = parent;
            ctx->menu_win = GTK_WINDOW(win);
            ctx->it = it;
            g_object_set_data_full(G_OBJECT(btn), "ewgui-menu-item", ctx, g_free);
            g_signal_connect(btn, "clicked", G_CALLBACK(on_item_clicked), ctx);

            gtk_box_append(GTK_BOX(box), btn);
        }
    }

    close = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close, GTK_ALIGN_END);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(vbox), close);

    gtk_window_present(GTK_WINDOW(win));
}
