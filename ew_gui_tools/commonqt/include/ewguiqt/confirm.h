#ifndef EWGUIQT_CONFIRM_H
#define EWGUIQT_CONFIRM_H

/* Confirmacion modal Si/No con el boton por defecto en NO.
 *
 * Es el equivalente de `GtkAlertDialog` con `No/Yes` de los modulos GTK
 * (`ew_controller_gui.c:259`): cerrar la ventana o pulsar Esc devuelve false,
 * igual que "No". Nunca se usa un QMessageBox con el default en Yes para una
 * accion destructiva.
 */

#include <QString>

class QWidget;

namespace ewguiqt {

bool confirm(QWidget *parent, const QString &text,
             const QString &acceptLabel = QStringLiteral("Yes"),
             const QString &rejectLabel = QStringLiteral("No"));

} /* namespace ewguiqt */

#endif /* EWGUIQT_CONFIRM_H */
