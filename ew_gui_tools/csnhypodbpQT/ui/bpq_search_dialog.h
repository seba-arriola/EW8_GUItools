/*
 * bpq_search_dialog.h - Ventana modal de busqueda hipocentral.
 *
 * Pide el rango de tiempo de origen (obligatorio, el resto de filtros llega
 * despues) y el tope de eventos. Devuelve texto tal cual: el parseo y la
 * validacion son del nucleo C (bpq_db_search), no del dialogo.
 *
 * No se usan popups de Qt (GtkDropDown/QComboBox sufren el bug de WSLg #1299):
 * es una ventana modal propia con campos de texto, como el resto del prototipo.
 */
#ifndef BPQ_SEARCH_DIALOG_H
#define BPQ_SEARCH_DIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QSpinBox;

class BpqSearchDialog : public QDialog
{
    Q_OBJECT
public:
    BpqSearchDialog(const QString &from, const QString &to, int maxEvents,
                    QWidget *parent = nullptr);

    QString fromText() const;
    QString toText() const;
    int     maxEvents() const;

private:
    QLineEdit *m_from = nullptr;
    QLineEdit *m_to = nullptr;
    QSpinBox  *m_max = nullptr;
};

#endif /* BPQ_SEARCH_DIALOG_H */
