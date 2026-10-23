/*
 * bpq_mag_model.h - Tabla de la sub-pestana `magnitudes` (fase 6.1).
 *
 * Una fila por estacion. Las celdas llegan YA formateadas por el nucleo puro
 * `csnhypodbp_magrow_*` (la misma fuente que la GUI GTK), asi que este modelo no
 * recalcula nada: solo mapea fila/columna a texto y alinea a la derecha lo que
 * es numerico.
 */
#ifndef BPQ_MAG_MODEL_H
#define BPQ_MAG_MODEL_H

#include <QAbstractTableModel>
#include <QVector>

#include "bpq_capi.h"

class BpqMagModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum {
        ColSta = 0, ColDist, ColAnchor, ColT0, ColT1, ColMag, ColRes, ColStat,
        ColCount
    };

    explicit BpqMagModel(QObject *parent = nullptr);

    void setRows(const QVector<CsnhypodbpMagRow> &rows);
    const QVector<CsnhypodbpMagRow> &rows() const { return m_rows; }
    const CsnhypodbpMagRow *row(int r) const;

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int col, Qt::Orientation o, int role) const override;

private:
    QVector<CsnhypodbpMagRow> m_rows;
};

#endif /* BPQ_MAG_MODEL_H */
