#ifndef EWGUIQT_THEME_H
#define EWGUIQT_THEME_H

/* Modo oscuro del modulo, sin depender del tema del escritorio.
 *
 * Se aplica sobre una QPalette completa (estilo Fusion) porque QSS por widget
 * deja zonas sin cubrir y el tema del sistema bajo WSLg cambia a media sesion.
 * No se persiste nada: igual que el `AdwStyleManager` del modulo GTK, el modo
 * vuelve al del sistema al arrancar.
 */

class QApplication;

namespace ewguiqt {

bool dark_mode(const QApplication &app);
void set_dark_mode(QApplication &app, bool on);

} /* namespace ewguiqt */

#endif /* EWGUIQT_THEME_H */
