/*
 * bpq_events_model.cxx - Ver bpq_events_model.h.
 *
 * Todo el formateo de celdas vive en la capa C (bpq_event_cell/bpq_origin_cell),
 * que es donde estan los formatos de la version GTK y sus tests. Aqui solo se
 * adapta a QVariant.
 */
#include "bpq_events_model.h"

#include <QLocale>

#include <algorithm>
#include <cmath>

BpqEventsModel::BpqEventsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void BpqEventsModel::setStore(BpqEvents *store)
{
    beginResetModel();
    m_store = store;
    endResetModel();
}

void BpqEventsModel::refresh()
{
    beginResetModel();
    endResetModel();
}

int BpqEventsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_store ? bpq_events_count(m_store) : 0;
}

int BpqEventsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return bpq_event_ncells();
}

const BpqEvent *BpqEventsModel::eventAt(int row) const
{
    if (!m_store || row < 0 || row >= bpq_events_count(m_store)) return nullptr;
    return bpq_events_at(m_store, row);
}

QString BpqEventsModel::keyAt(int row) const
{
    const BpqEvent *ev = eventAt(row);
    const CsnhypodbpSolution *s;
    int p;
    if (!ev) return QString();
    p = bpq_event_preferred(ev);
    s = bpq_event_sol(ev, p);
    if (!s) return QString();
    return QStringLiteral("%1:%2:%3").arg(s->qid).arg(s->mod).arg(s->version);
}

int BpqEventsModel::rowForKey(const QString &key) const
{
    for (int i = 0; i < rowCount(); i++)
        if (keyAt(i) == key) return i;
    return -1;
}

QVariant BpqEventsModel::data(const QModelIndex &idx, int role) const
{
    const BpqEvent *ev = eventAt(idx.row());
    char buf[64];

    if (!ev || !idx.isValid()) return QVariant();
    switch (role) {
    case Qt::DisplayRole:
        bpq_event_cell(ev, idx.column(), buf, sizeof(buf));
        return QString::fromUtf8(buf);
    case Qt::TextAlignmentRole:
        /* Numericas a la derecha (paridad con la tabla GTK). */
        if (bpq_event_sort_kind(idx.column()) != 0)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ToolTipRole: {
        bpq_event_cell(ev, idx.column(), buf, sizeof(buf));
        return QString::fromUtf8(buf);
    }
    default:
        return QVariant();
    }
}

QVariant BpqEventsModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    return QString::fromUtf8(bpq_event_header(section));
}

/* ------------------------------------------------------------------------- */

BpqSortProxy::BpqSortProxy(QObject *parent)
    : ewguiqt::SortProxy(parent)
{
    /* Mismo reparto texto/numero que bpq_event_sort_kind(). 32 columnas cubren
     * de sobra las 15 de la tabla; las sobrantes quedan como Text. */
    QVector<ewguiqt::SortKind> kinds;

    kinds.reserve(32);
    for (int c = 0; c < 32; ++c)
        kinds.push_back(bpq_event_sort_kind(c) == 0 ? ewguiqt::SortKind::Text
                                                   : ewguiqt::SortKind::Number);
    setColumnKinds(kinds);

    /* La celda visible no siempre es el valor ordenable: "ML-4" ordena por -4. */
    setCellTextFn([](const QAbstractItemModel &model, int row, int column) {
        const auto *m = qobject_cast<const BpqEventsModel *>(&model);
        const BpqEvent *ev = m ? m->eventAt(row) : nullptr;
        char buf[64] = {0};
        if (ev)
            bpq_event_cell(ev, column, buf, sizeof(buf));
        return QString::fromUtf8(buf);
    });
    setCellNumberFn([](const QAbstractItemModel &model, int row, int column) {
        const auto *m = qobject_cast<const BpqEventsModel *>(&model);
        const BpqEvent *ev = m ? m->eventAt(row) : nullptr;
        return ev ? bpq_event_sort_num(ev, column) : std::nan("");
    });

    /* El modulo refresca con reset() del modelo; no hace falta re-filtrar. */
    setDynamicSortFilter(false);
}

/* ------------------------------------------------------------------------- */

BpqOriginsModel::BpqOriginsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void BpqOriginsModel::setEvent(const BpqEvent *ev)
{
    beginResetModel();
    m_ev = ev;
    m_order.clear();
    if (ev) {
        const int n = bpq_event_nsols(ev);
        QVector<QPair<double, int>> tmp;
        tmp.reserve(n);
        for (int j = 0; j < n; j++)
            tmp.append(qMakePair(bpq_origin_sort_num(ev, j, 3), j));   /* Created */
        /* Descendente; empate por indice (estable). */
        std::sort(tmp.begin(), tmp.end(), [](const QPair<double, int> &a,
                                             const QPair<double, int> &b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        m_order.reserve(n);
        for (const auto &p : tmp) m_order.append(p.second);
    }
    endResetModel();
}

int BpqOriginsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_order.size();
}

int BpqOriginsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return bpq_origin_ncells();
}

QString BpqOriginsModel::keyAt(int row) const
{
    const CsnhypodbpSolution *s;
    if (row < 0 || row >= m_order.size()) return QString();
    s = bpq_event_sol(m_ev, m_order[row]);
    if (!s) return QString();
    return QStringLiteral("%1:%2:%3").arg(s->qid).arg(s->mod).arg(s->version);
}

int BpqOriginsModel::rowForKey(const QString &key) const
{
    for (int i = 0; i < rowCount(); i++)
        if (keyAt(i) == key) return i;
    return -1;
}

int BpqOriginsModel::rowForPreferred() const
{
    const int pref = m_ev ? bpq_event_preferred(m_ev) : -1;
    if (pref < 0) return -1;
    for (int i = 0; i < m_order.size(); i++)
        if (m_order[i] == pref) return i;
    return -1;
}

int BpqOriginsModel::solIndexAt(int row) const
{
    if (row < 0 || row >= m_order.size()) return -1;
    return m_order[row];
}

int BpqOriginsModel::rowForSolution(int sol) const
{
    for (int i = 0; i < m_order.size(); i++)
        if (m_order[i] == sol) return i;
    return -1;
}

QVariant BpqOriginsModel::data(const QModelIndex &idx, int role) const
{
    char buf[64];
    int j;

    if (!m_ev || !idx.isValid()) return QVariant();
    if (idx.row() < 0 || idx.row() >= m_order.size()) return QVariant();
    j = m_order[idx.row()];
    switch (role) {
    case Qt::DisplayRole:
        bpq_origin_cell(m_ev, j, idx.column(), buf, sizeof(buf));
        return QString::fromUtf8(buf);
    case Qt::TextAlignmentRole:
        /* Mod/Ver/Est. son texto; el resto numerico. */
        if (idx.column() == 0 || idx.column() == 2) return int(Qt::AlignLeft | Qt::AlignVCenter);
        return int(Qt::AlignRight | Qt::AlignVCenter);
    default:
        return QVariant();
    }
}

QVariant BpqOriginsModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    return QString::fromUtf8(bpq_origin_header(section));
}
