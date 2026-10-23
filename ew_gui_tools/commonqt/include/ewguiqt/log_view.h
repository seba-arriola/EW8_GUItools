#ifndef EWGUIQT_LOG_VIEW_H
#define EWGUIQT_LOG_VIEW_H

/* Visor de log de solo lectura, monoespaciado y sin ajuste de linea.
 *
 * Reproduce el `GtkTextView` + `tail_file()` del modulo GTK
 * (`ew_controller_gui.c:736-754`): lee los ultimos N bytes y empieza en un
 * borde de linea, para no ensenar media linea cortada.
 */

#include <QPlainTextEdit>

namespace ewguiqt {

class LogView : public QPlainTextEdit {
public:
    explicit LogView(QWidget *parent = nullptr);

    void setTailBytes(qint64 bytes) { m_tail = bytes; }
    qint64 tailBytes() const { return m_tail; }

    /* false si el fichero no se pudo abrir (el texto queda vacio). */
    bool loadPath(const QString &path);

    /* Mensaje plano en lugar del fichero: "(no log file)" y compania. */
    void setStatus(const QString &message);

private:
    qint64 m_tail = 32768;   /* LOG_TAIL del modulo GTK */
};

} /* namespace ewguiqt */

#endif /* EWGUIQT_LOG_VIEW_H */
