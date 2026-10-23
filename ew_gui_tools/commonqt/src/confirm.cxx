/* Ver include/ewguiqt/confirm.h. */
#include "ewguiqt/confirm.h"

#include <QMessageBox>
#include <QPushButton>

namespace ewguiqt {

bool confirm(QWidget *parent, const QString &text,
             const QString &acceptLabel, const QString &rejectLabel)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setText(text);
    box.setWindowModality(Qt::WindowModal);

    QPushButton *yes = box.addButton(acceptLabel, QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(rejectLabel, QMessageBox::RejectRole);
    (void)yes;
    box.setDefaultButton(no);     /* destructivo: el default es NO */

    box.exec();
    /* Cerrar con la X o Esc deja clickedButton() en nullptr => false. */
    return box.clickedButton() == yes;
}

} /* namespace ewguiqt */
