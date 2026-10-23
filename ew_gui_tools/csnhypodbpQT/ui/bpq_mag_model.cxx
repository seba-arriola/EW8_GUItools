/*
 * bpq_mag_model.cxx - Modelo de la tabla de magnitudes (fase 6.1).
 */
#include "bpq_mag_model.h"

#include <QString>
#include <cstdio>

BpqMagModel::BpqMagModel(QObject *parent) : QAbstractTableModel(parent) {}

void BpqMagModel::setRows(const QVector<CsnhypodbpMagRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

const CsnhypodbpMagRow *BpqMagModel::row(int r) const
{
    if (r < 0 || r >= m_rows.size()) return nullptr;
    return &m_rows.at(r);
}

int BpqMagModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int BpqMagModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant BpqMagModel::data(const QModelIndex &idx, int role) const
{
    const CsnhypodbpMagRow *r = row(idx.row());

    if (!r) return QVariant();
    if (role == Qt::TextAlignmentRole) {
        switch (idx.column()) {
        case ColSta:
        case ColAnchor:
        case ColStat:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        default:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    if (role != Qt::DisplayRole) return QVariant();

    char txt[64] = "";

    switch (idx.column()) {
    case ColSta:
        return QString::fromUtf8(r->sta);
    case ColDist:
        snprintf(txt, sizeof(txt), "%.0f", r->dist_km);
        return QString::fromUtf8(txt);
    case ColAnchor:
        return QString::fromUtf8(csnhypodbp_magrow_anchor_text(r));
    case ColT0:
        csnhypodbp_magrow_t0_text(r, txt, sizeof(txt));
        return QString::fromUtf8(txt);
    case ColT1:
        csnhypodbp_magrow_t1_text(r, txt, sizeof(txt));
        return QString::fromUtf8(txt);
    case ColMag:
        csnhypodbp_magrow_mag_text(r, txt, sizeof(txt));
        return QString::fromUtf8(txt);
    case ColRes:
        csnhypodbp_magrow_res_text(r, txt, sizeof(txt));
        return QString::fromUtf8(txt);
    case ColStat:
        return QString::fromUtf8(csnhypodbp_magrow_stat_text(r));
    default:
        return QVariant();
    }
}

QVariant BpqMagModel::headerData(int col, Qt::Orientation o, int role) const
{
    static const char *const kHeaders[ColCount] = {
        "station", "dist[km]", "anchor", "t0[s]", "t1[s]", "mag", "res", "stat"
    };

    if (role != Qt::DisplayRole || o != Qt::Horizontal) return QVariant();
    if (col < 0 || col >= ColCount) return QVariant();
    return QString::fromUtf8(kHeaders[col]);
}
