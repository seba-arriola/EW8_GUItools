#ifndef EWGUIQT_LIST_SELECT_H
#define EWGUIQT_LIST_SELECT_H

/* Selector modal de una opcion de una lista.
 *
 * Sustituye a los desplegables: bajo WSLg/Wayland los popups quedan DETRAS de
 * la ventana y no cierran al elegir (bug #1299, AGENTS.md §7). Por eso ningun
 * modulo usa QComboBox para elegir modulo/log/config: abre esta ventana modal.
 *
 * Contrato:
 *   - `pick()` devuelve el indice elegido o -1 si se cancelo (Esc/Close);
 *   - doble clic o Enter aceptan;
 *   - `current` < 0 o fuera de rango => sin preseleccion.
 */

#include <QDialog>
#include <QStringList>

class QListWidget;

namespace ewguiqt {

class ListSelectDialog : public QDialog {
    Q_OBJECT

public:
    ListSelectDialog(const QString &title, const QStringList &labels,
                     int current, QWidget *parent = nullptr);

    int chosenIndex() const { return m_chosen; }

    static int pick(QWidget *parent, const QString &title,
                    const QStringList &labels, int current);

private:
    void acceptCurrent();

    QListWidget *m_list = nullptr;
    int m_chosen = -1;
};

} /* namespace ewguiqt */

#endif /* EWGUIQT_LIST_SELECT_H */
