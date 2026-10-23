/*
 * bpq_mww_panel.h - Panel `Mww` (fase 6.3): inversion de fase W on-demand.
 *
 * Layout (paridad con la GTK): arriba, playera + resumen a la izquierda y la
 * salida completa del motor a la derecha, los dos cuadros con la MISMA altura
 * (MWW_ROW_H); abajo, la tabla de parametros EDITABLE (solo las celdas con
 * campo propio) y `Calculate`/`Cancel`.
 *
 * Todo el estado y los textos vienen de la fachada C (`BpqMww`): el panel no
 * conoce csnwphase, solo `BpqMwwParamRow` y cadenas ya formateadas.
 */
#ifndef BPQ_MWW_PANEL_H
#define BPQ_MWW_PANEL_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QWidget>

#include "bpq_capi.h"

class QLabel;
class QPushButton;
class QTableWidget;
class BpqCanvasWidget;

/* Alto comun de los dos cuadros de resultado (el de la GTK, +50 %). */
#define BPQ_MWW_ROW_H 248

class BpqMwwPanel : public QWidget
{
    Q_OBJECT
public:
    explicit BpqMwwPanel(QWidget *parent = nullptr);

    /* Estado del panel (lo posee la ventana: vive lo que la ventana). */
    void setMww(BpqMww *m);
    /* Rearma tabla + resumen + playera + linea de estado desde la fachada. */
    void refresh();

    void setBusy(bool on);
    bool busy() const { return m_busy; }
    void setStatus(const QString &txt, bool error);

signals:
    void calculateRequested();
    void cancelRequested();

private:
    void rebuildTable();
    void onCellEdited(int row, int col);

    BpqMww         *m_mww = nullptr;
    bool            m_busy = false;
    QVector<BpqMwwParamRow> m_rows;

    QLabel          *m_status = nullptr;
    QLabel          *m_summary = nullptr;
    QLabel          *m_output = nullptr;
    BpqCanvasWidget *m_ball = nullptr;
    QTableWidget    *m_table = nullptr;
    QPushButton     *m_calc = nullptr;
    QPushButton     *m_cancel = nullptr;
};

/* Corre `bpq_mww_run` FUERA del hilo de la GUI (el motor tarda minutos). */
class BpqMwwWorker : public QObject
{
    Q_OBJECT
public:
    explicit BpqMwwWorker(QObject *parent = nullptr);

public slots:
    void runJob(quint64 id, BpqMww *m, int dryRun);

signals:
    void finished(quint64 id, bool ok, const QString &err, double secs);
};

#endif /* BPQ_MWW_PANEL_H */
