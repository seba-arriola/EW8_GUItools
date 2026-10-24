/*
 * bpq_mag_model.cxx - Modelo de la tabla de magnitudes (fase 6.1).
 */
#include "bpq_mag_model.h"

#include <QString>
#include <cmath>
#include <cstdio>
#include <cstring>

/* NaN == NaN para decidir si un refresco cambia algo (los campos sin dato son
 * NaN y `==` los declararia distintos en cada render). */
static bool same_double(double a, double b)
{
    return a == b || (std::isnan(a) && std::isnan(b));
}

static bool same_mag_row(const CsnhypodbpMagRow &a, const CsnhypodbpMagRow &b)
{
    return std::strncmp(a.sta, b.sta, sizeof(a.sta)) == 0 &&
           std::strncmp(a.chan, b.chan, sizeof(a.chan)) == 0 &&
           same_double(a.dist_km, b.dist_km) &&
           same_double(a.mag, b.mag) &&
           same_double(a.res, b.res) &&
           same_double(a.amp, b.amp) &&
           same_double(a.period, b.period) &&
           a.manual == b.manual && a.used == b.used &&
           a.has_data == b.has_data && a.in_range == b.in_range &&
           a.eligible == b.eligible && a.win_src == b.win_src &&
           a.anchor == b.anchor && same_double(a.anchor_t, b.anchor_t) &&
           same_double(a.dist_deg, b.dist_deg) &&
           same_double(a.t0, b.t0) && same_double(a.t1, b.t1);
}

BpqMagModel::BpqMagModel(QObject *parent) : QAbstractTableModel(parent) {}

bool BpqMagModel::setRows(const QVector<CsnhypodbpMagRow> &rows)
{
    if (m_rows.size() == rows.size()) {
        bool same = true;

        for (int i = 0; i < rows.size(); i++) {
            if (!same_mag_row(m_rows.at(i), rows.at(i))) {
                same = false;
                break;
            }
        }
        if (same) return false;
    }
    beginResetModel();
    m_rows = rows;
    endResetModel();
    return true;
}

const CsnhypodbpMagRow *BpqMagModel::row(int r) const
{
    if (r < 0 || r >= m_rows.size()) return nullptr;
    return &m_rows.at(r);
}

double BpqMagModel::sortValue(int row, int col) const
{
    const CsnhypodbpMagRow *r = this->row(row);
    const QString txt = data(index(row, col), Qt::DisplayRole).toString();

    if (!r || txt.isEmpty() || txt == QStringLiteral("-")) return std::nan("");
    switch (col) {
    case ColDist: return r->dist_km;
    case ColT0:   return r->t0;
    case ColT1:   return r->t1;
    case ColMag:  return r->mag;
    case ColRes:  return r->res;
    default:      return std::nan("");
    }
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
