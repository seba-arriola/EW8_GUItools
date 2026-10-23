/*
 * bpq_events_model.h - Modelos de las dos tablas de la pestaña "Eventos".
 *
 * BpqEventsModel  -> tabla maestra: 1 fila por EVENTO (solucion preferida).
 * BpqOriginsModel -> tabla de detalle: 1 fila por SOLUCION del evento elegido.
 *
 * Sustituyen al trio GListStore + GtkSortListModel + GtkSingleSelection del
 * modulo GTK. La diferencia importante (docs/testLibQT.md §5, T8): en Qt los
 * indices se invalidan con beginResetModel, asi que la seleccion se conserva
 * por `keyAt()` (qid:mod:version), nunca por numero de fila.
 */
#ifndef BPQ_EVENTS_MODEL_H
#define BPQ_EVENTS_MODEL_H

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

#include "ewguiqt/table.h"
#include <QString>
#include <QVector>

#include "bpq_capi.h"

class BpqEventsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit BpqEventsModel(QObject *parent = nullptr);

    void setStore(BpqEvents *store);          /* no toma propiedad */
    BpqEvents *store() const { return m_store; }

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;

    const BpqEvent *eventAt(int row) const;
    /* Identidad estable de la fila: <qid>:<mod>:<version> de la preferida. */
    QString keyAt(int row) const;
    int     rowForKey(const QString &key) const;

public slots:
    void refresh();                           /* reset del modelo (los datos ya cambiaron) */

private:
    BpqEvents *m_store = nullptr;
};

/* Orden por cabecera: reusa el comparador compartido (commonqt). Las columnas
 * numericas/fecha se declaran con bpq_event_sort_kind(); el valor ordenable es
 * bpq_event_sort_num() porque la celda visible no siempre es el numero (p.ej.
 * "ML-4"). El primer clic lo maneja QTableView::setSortingEnabled(). */
class BpqSortProxy : public ewguiqt::SortProxy
{
    Q_OBJECT
public:
    explicit BpqSortProxy(QObject *parent = nullptr);
};

class BpqOriginsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit BpqOriginsModel(QObject *parent = nullptr);

    void setEvent(const BpqEvent *ev);         /* NULL -> tabla vacia */
    /* Evento cuyas soluciones muestra el modelo (para refrescar vistas derivadas).
     * Ojo: no se llama `event()` porque taparia QObject::event(QEvent*). */
    const BpqEvent *currentEvent() const { return m_ev; }

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;

    QString keyAt(int row) const;             /* <qid>:<mod>:<version> de esa solucion */
    int     rowForKey(const QString &key) const;
    int     rowForPreferred() const;
    /* Fila de la vista para un indice de solucion (-1 si no esta). */
    int     rowForSolution(int sol) const;
    /* Indice de la solucion en el store para una fila de la vista (el modelo
     * ordena por Created DESC, asi que fila != indice). -1 si no vale. */
    int     solIndexAt(int row) const;

private:
    const BpqEvent *m_ev = nullptr;
    /* Orden por defecto: Created DESC (paridad con csnhypodbp:1786-1790).
     * `m_order[row]` es el indice de la solucion en el store. */
    QVector<int> m_order;
};

#endif /* BPQ_EVENTS_MODEL_H */
