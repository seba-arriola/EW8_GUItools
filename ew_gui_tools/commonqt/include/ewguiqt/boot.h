#ifndef EWGUIQT_BOOT_H
#define EWGUIQT_BOOT_H

/* Arranque comun de un modulo GUI Qt6.
 *
 * Hace las tres cosas que todos los `main.cxx` repiten y que es facil hacer
 * mal (ver tests):
 *   1) fija el locale numerico a `C` ANTES de crear QApplication y otra vez
 *      DESPUES (Qt re-aplica el locale del sistema al construir la app). Sin
 *      esto `strtod` lee "3,38" y los `.d`/anillos se parsean mal
 *      (AGENTS.md §7: awk/mawk y LC_NUMERIC);
 *   2) resuelve los flags "headless" (--print-config, --check-*) ANTES de crear
 *      QApplication, asi funcionan sin DISPLAY;
 *   3) delega el resto en el callback `gui`.
 *
 * Header sin Qt widgets: se puede incluir en un `main.cxx` que solo use QtCore.
 */

#include <functional>

#include <QString>
#include <QStringList>
#include <QVector>

class QApplication;

namespace ewguiqt {

struct HeadlessCommand {
    QString flag;                                        /* p.ej. "--print-config" */
    std::function<int(const QStringList &args)> run;     /* args = los que siguen */
};

struct BootOptions {
    QString app_name;
    QVector<HeadlessCommand> headless;
};

/* Devuelve el exit code del proceso. `gui` recibe la app ya creada y el argv
 * completo (argv[0] incluido) para que el modulo parsee sus propios flags. */
int boot(int argc, char **argv, const BootOptions &opts,
         const std::function<int(QApplication &, const QStringList &)> &gui);

} /* namespace ewguiqt */

#endif /* EWGUIQT_BOOT_H */
