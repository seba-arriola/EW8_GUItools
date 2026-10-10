#ifndef EWGUI_OVERLAY_UI_H
#define EWGUI_OVERLAY_UI_H

#include "ewgui/overlay.h"

/*
 * UI GTK4 compartida para editar la capa de líneas (ew_controller/csnrv/
 * csnstaevdisp). Los cambios se aplican EN MEMORIA sobre `ov`; el módulo
 * decide si los loguea/persiste. Solo build GTK4 + libadwaita.
 */

typedef struct _GtkWindow GtkWindow;

/* Se invoca tras cada cambio (color o visibilidad) para que el módulo repinte. */
typedef void (*EwOverlayChangedFn)(void *ud);

void ewgui_overlay_dialog_run(EwOverlay *ov, GtkWindow *parent,
                              EwOverlayChangedFn on_changed, void *ud);

#endif /* EWGUI_OVERLAY_UI_H */
