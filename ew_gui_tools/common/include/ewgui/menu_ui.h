#ifndef EWGUI_MENU_UI_H
#define EWGUI_MENU_UI_H

#include <stddef.h>

#include "ewgui/actions.h"   /* EwMenuGroup, EwMenuItem */

/*
 * Menú propio de los módulos GUI (GTK4). Sustituye al `GtkMenuButton` con
 * `GMenuModel`, cuyo `GtkPopoverMenu` es una superficie popup que bajo
 * WSLg/Wayland queda detrás de la ventana y se comporta de forma anómala.
 *
 * En su lugar, el botón ☰ abre una VENTANA MODAL propia (mismo patrón que
 * `ewgui_overlay_dialog_run`). Solo build GTK4 + libadwaita.
 */

/* Forward-decls: este header NO incluye <gtk/gtk.h> (tests/check_no_gtk.sh). */
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;

/* Botón (icono "open-menu-symbolic", clase "flat") que al pulsarse abre
 * `ewgui_menu_dialog_run()`. `groups` debe tener vida estática: los módulos
 * los declaran `static const` a nivel de fichero. Si n==0, botón insensible. */
GtkWidget *ewgui_menu_button_new(const EwMenuGroup *groups, size_t n);

/* Actualiza los grupos de un botón ya creado (menú dependiente de la vista,
 * p. ej. csnhypodbp: Eventos vs Ondas). `groups` debe tener vida estática.
 * Con n==0 el botón queda insensible. */
void ewgui_menu_button_set_groups(GtkWidget *btn, const EwMenuGroup *groups, size_t n);

/* Presenta `groups` como ventana modal propia (GtkWindow transient_for parent,
 * modal, no bloqueante). Semántica de EwMenuGroup:
 *   - group->label           -> encabezado de sección (si NULL, sin encabezado)
 *   - item.label == NULL     -> separador (igual que ewgui_menu_build)
 *   - item.label + item.action -> botón que activa `action` en `parent`;
 *     si item.target_fmt != NULL se pasa item.target_int (formato "i").
 * Se cierra al pulsar un ítem o Close. */
void ewgui_menu_dialog_run(GtkWindow *parent, const EwMenuGroup *groups, size_t n);

#endif /* EWGUI_MENU_UI_H */
