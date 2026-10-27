/*
 * bpq_stations_model.cxx - Ver bpq_stations_model.h.
 */
#include "bpq_stations_model.h"

#include <QColor>
#include <QString>

#include <cmath>

static QString origin_label(char origin)
{
    switch (origin) {
    case 'M': return QStringLiteral("M");     /* picada manual */
    case 'A': return QStringLiteral("A");     /* automatica   */
    default:  return QStringLiteral("Teo");   /* teorica      */
    }
}

BpqStationsModel::BpqStationsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int BpqStationsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size() * 2;      /* P y S por estacion */
}

int BpqStationsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColCount;
}

const CsnhypodbpStaPick *BpqStationsModel::pickAt(int row) const
{
    const int i = row / 2;
    const int phase = row % 2;
    if (i < 0 || i >= m_rows.size()) return nullptr;
    return phase == 0 ? &m_rows[i].p : &m_rows[i].s;
}

int BpqStationsModel::phaseAt(int row) const
{
    if (row < 0 || row >= m_rows.size() * 2) return -1;
    return row % 2;
}

int BpqStationsModel::staIndexAt(int row) const
{
    const int i = row / 2;
    return (i >= 0 && i < m_rows.size()) ? i : -1;
}

double BpqStationsModel::sortValue(int row, int col) const
{
    const int i = row / 2;
    const int phase = row % 2;
    const QString txt = data(index(row, col), Qt::DisplayRole).toString();

    if (i < 0 || i >= m_rows.size()) return std::nan("");
    if (txt.isEmpty() || txt == QStringLiteral("-")) return std::nan("");
    if (col == ColDist) return m_rows.at(i).dist_km;
    if (col == ColRes) {
        const CsnhypodbpStaPick &pk = (phase == 0) ? m_rows.at(i).p
                                                   : m_rows.at(i).s;
        return (pk.present && std::isfinite(pk.resid)) ? pk.resid : std::nan("");
    }
    return std::nan("");
}

int BpqStationsModel::pickedCount() const
{
    int n = 0;
    for (const CsnhypodbpStaRow &r : m_rows) {
        if (r.p.present) n++;
        if (r.s.present) n++;
    }
    return n;
}

/* Filas de la vista cuyo residual viene del localizador (no NAN): es la prueba
 * de que el JSONL de csnloc llego y se proyecto a la tabla. */
int BpqStationsModel::residCount() const
{
    int n = 0;

    for (const CsnhypodbpStaRow &r : m_rows) {
        if (r.p.present && std::isfinite(r.p.resid)) n++;
        if (r.s.present && std::isfinite(r.s.resid)) n++;
    }
    return n;
}

QVariant BpqStationsModel::data(const QModelIndex &ix, int role) const
{
    if (!ix.isValid() || ix.row() >= m_rows.size() * 2) return QVariant();

    const int i = ix.row() / 2;
    const int phase = ix.row() % 2;
    const CsnhypodbpStaRow &r = m_rows.at(i);
    const CsnhypodbpStaPick &pk = (phase == 0) ? r.p : r.s;

    if (role == Qt::CheckStateRole && ix.column() == ColStation)
        return int(isChecked(ix.row()) ? Qt::Checked : Qt::Unchecked);

    if (role == Qt::DisplayRole) {
        switch (ix.column()) {
        case ColStation: return QString::fromUtf8(r.sta);
        case ColDist:    return QString::number(r.dist_km, 'f', 1);
        case ColPick:    return phase == 0 ? QStringLiteral("P") : QStringLiteral("S");
        case ColStat:    return pk.present ? origin_label(pk.origin)
                                           : QStringLiteral("-");
        case ColRes:
            if (!pk.present || !std::isfinite(pk.resid)) return QString();
            return QString::asprintf("%+.2f", pk.resid);
        default: break;
        }
        return QVariant();
    }

    if (role == Qt::TextAlignmentRole) {
        if (ix.column() == ColDist || ix.column() == ColRes)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ForegroundRole && ix.column() == ColStat && pk.present) {
        /* Manual en azul, automatica en gris: se distinguen de un vistazo. */
        if (pk.origin == 'M') return QColor(0x14, 0x54, 0xa8);
        if (pk.origin == 'A') return QColor(0x60, 0x60, 0x60);
    }
    if (role == Qt::ForegroundRole && !pk.present)
        return QColor(0xa0, 0xa0, 0xa0);

    return QVariant();
}

QVariant BpqStationsModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case ColStation: return QStringLiteral("station");
    case ColDist:    return QStringLiteral("dist[km]");
    case ColPick:    return QStringLiteral("pick");
    case ColStat:    return QStringLiteral("stat");
    case ColRes:     return QStringLiteral("res");
    default:         return QVariant();
    }
}

Qt::ItemFlags BpqStationsModel::flags(const QModelIndex &ix) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(ix);

    if (ix.isValid() && ix.column() == ColStation)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

bool BpqStationsModel::setData(const QModelIndex &ix, const QVariant &value,
                               int role)
{
    if (role != Qt::CheckStateRole || !ix.isValid() ||
        ix.column() != ColStation || ix.row() < 0 ||
        ix.row() >= m_on.size())
        return false;
    {
        const unsigned char on = (value.toInt() == Qt::Checked) ? 1 : 0;

        if (m_on.at(ix.row()) == on) return true;
        m_on[ix.row()] = on;
    }
    emit dataChanged(ix, ix, {Qt::CheckStateRole});
    return true;
}

bool BpqStationsModel::isChecked(int row) const
{
    return row >= 0 && row < m_on.size() && m_on.at(row) != 0;
}

void BpqStationsModel::setChecked(int row, bool on)
{
    if (row < 0 || row >= m_on.size()) return;
    {
        const unsigned char v = on ? 1 : 0;

        if (m_on.at(row) == v) return;
        m_on[row] = v;
    }
    {
        const QModelIndex ix = index(row, ColStation);

        emit dataChanged(ix, ix, {Qt::CheckStateRole});
    }
}

void BpqStationsModel::clearChecks()
{
    int i;

    if (m_on.isEmpty()) return;
    for (i = 0; i < m_on.size(); i++) m_on[i] = 1;
    emit dataChanged(index(0, ColStation), index(m_on.size() - 1, ColStation),
                     {Qt::CheckStateRole});
}

int BpqStationsModel::checkedCount() const
{
    int i, n = 0;

    for (i = 0; i < m_on.size(); i++)
        if (m_on.at(i)) n++;
    return n;
}

void BpqStationsModel::setRows(const CsnhypodbpStaRow *rows, int n)
{
    const QVector<CsnhypodbpStaRow> oldRows = m_rows;
    const QVector<unsigned char> oldOn = m_on;

    beginResetModel();
    m_rows.clear();
    m_on.clear();
    if (rows && n > 0) {
        m_rows.reserve(n);
        m_on.reserve(n * 2);
        for (int i = 0; i < n; i++) {
            m_rows.append(rows[i]);
            for (int ph = 0; ph < 2; ph++) {
                unsigned char on = 1;

                /* El check sobrevive al rebuild de la tabla: se busca la misma
                 * entrada del catalogo (`idx`, unico) y fase en las filas
                 * anteriores. */
                for (int j = 0; j < oldRows.size(); j++) {
                    if (oldRows.at(j).idx != rows[i].idx)
                        continue;
                    if (j * 2 + ph < oldOn.size())
                        on = oldOn.at(j * 2 + ph);
                    break;
                }
                m_on.append(on);
            }
        }
    }
    endResetModel();
}
