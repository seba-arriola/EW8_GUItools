/* Ver include/ewguiqt/log_view.h. */
#include "ewguiqt/log_view.h"

#include <QFile>
#include <QFontDatabase>
#include <QTextCursor>

namespace ewguiqt {

LogView::LogView(QWidget *parent) : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setUndoRedoEnabled(false);
}

bool LogView::loadPath(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        clear();
        return false;
    }

    const qint64 size = f.size();
    const qint64 start = size > m_tail ? size - m_tail : 0;
    if (!f.seek(start)) {
        clear();
        return false;
    }
    QByteArray buf = f.readAll();

    /* Empezar en borde de linea: si se recorto el principio, la primera linea
     * esta partida y se descarta entera (igual que tail_file() del GTK). */
    if (start > 0) {
        const int nl = buf.indexOf('\n');
        buf = nl < 0 ? QByteArray() : buf.mid(nl + 1);
    }

    setPlainText(QString::fromUtf8(buf));
    moveCursor(QTextCursor::End);
    return true;
}

void LogView::setStatus(const QString &message)
{
    setPlainText(message);
}

} /* namespace ewguiqt */
