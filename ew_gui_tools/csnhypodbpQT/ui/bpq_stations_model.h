/*
 * bpq_stations_model.h - Tabla de estaciones del panel de picking.
 *
 * Una fila por (estacion, fase), igual que la GUI GTK: las dos filas son
 * autonomas (estacion, distancia, fase, origen y residual propios). El modelo
 * solo PINTA: quien decide que picada gana (M > A > Teo) es
 * csnhypodbp_statable_build, que ya deja en `p`/`s` la elegida y su origen.
 */
#ifndef BPQ_STATIONS_MODEL_H
#define BPQ_STATIONS_MODEL_H

#include <QAbstractTableModel>
#include <QVector>

#include "bpq_capi.h"

class BpqStationsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColStation = 0, ColDist, ColPick, ColStat, ColRes, ColCount };

    explicit BpqStationsModel(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &ix, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    /* Copia las filas (2 por estacion en la vista). */
    void setRows(const CsnhypodbpStaRow *rows, int n);
    int  staCount() const { return m_rows.size(); }
    int  pickedCount() const;          /* filas con fase presente */
    int  residCount() const;           /* filas con residual del localizador */
    /* Indice de la fase de una fila de la vista: 0 = P, 1 = S, -1 invalido. */
    int  phaseAt(int row) const;
    int  staIndexAt(int row) const;    /* indice en m_rows */
    /* Valor numerico para el proxy de ordenacion (NaN = columna de texto). */
    double sortValue(int row, int col) const;

private:
    const CsnhypodbpStaPick *pickAt(int row) const;

    QVector<CsnhypodbpStaRow> m_rows;
};

#endif /* BPQ_STATIONS_MODEL_H */
