#ifndef EWGUI_ACTIONS_H
#define EWGUI_ACTIONS_H

#include <stddef.h>

#include <gio/gio.h>

/*
 * Acciones y menús declarativos con GIO (GSimpleAction + GMenu), sin GTK.
 * Portables a GTK4: en GTK3 el GMenuModel se monta con
 * gtk_menu_bar_new_from_model(); en GTK4 con GtkPopoverMenuBar (mismo modelo).
 */

typedef void (*EwActionCb)(GSimpleAction *action, GVariant *param, gpointer user_data);

/* Añade una acción a `map` (p. ej. el GtkWindow, que es GActionMap).
 * `param_type` es el formato GVariant del parámetro ("i", "s", ...) o NULL. */
void ewgui_action_add(GActionMap *map, const char *name, const char *param_type,
                      EwActionCb cb, gpointer user_data);

/* Ítem de menú. label==NULL => separador de sección.
 * `checkable`/`checked` permiten una casilla con estado: el diálogo la dibuja
 * marcada según `checked` y, al pulsarla, activa la misma acción. El estado lo
 * mantiene el módulo (el menú no tiene acceso público al GSimpleAction); el
 * `.d` no se reescribe: `checked` es estado de sesión. */
typedef struct {
    const char *label;
    const char *action;      /* p. ej. "win.colour" */
    const char *target_fmt;  /* "i" o NULL */
    int         target_int;
    int         checkable;   /* 0 = comando (default histórico); 1 = casilla */
    int         checked;     /* estado que dibuja la casilla al abrir el menú */
} EwMenuItem;

/* Submenú de primer nivel. */
typedef struct {
    const char       *label;
    const EwMenuItem *items;
    size_t            n_items;
} EwMenuGroup;

/* Construye un GMenuModel con un submenú por grupo. Devuelve una referencia
 * nueva (liberar con g_object_unref). */
GMenuModel *ewgui_menu_build(const EwMenuGroup *groups, size_t n);

#endif /* EWGUI_ACTIONS_H */
