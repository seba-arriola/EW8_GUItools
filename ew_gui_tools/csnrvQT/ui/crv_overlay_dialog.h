#ifndef CRV_OVERLAY_DIALOG_H
#define CRV_OVERLAY_DIALOG_H

/* Dialogo de capas .lines (equivalente a `ewgui_overlay_dialog_run` del GTK).
 *
 * Sin Q_OBJECT a proposito: no emite senales, aplica visibilidad al mapa
 * directamente y la ventana repinta al cerrar. Se usan QCheckBox (WSLg bug
 * #1299: nada de desplegables).
 */

#include <functional>

#include <QDialog>

#include "crv_capi.h"

class CrvOverlayDialog : public QDialog
{
public:
    /* `onChanged` se llama en cada toggle para repintar el canvas al instante
     * (equivalente al callback de `ewgui_overlay_dialog_run`). */
    explicit CrvOverlayDialog(CrvMap *map, QWidget *parent = nullptr,
                              std::function<void()> onChanged = {});

private:
    CrvMap *m_map;
    std::function<void()> m_changed;
};

#endif /* CRV_OVERLAY_DIALOG_H */
