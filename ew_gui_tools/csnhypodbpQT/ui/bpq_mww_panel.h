/*
 * bpq_mww_panel.h - Panel `Mww` (fase 6.3): inversion de fase W on-demand.
 *
 * Layout: a la IZQUIERDA, la tabla de parametros (parametro | valor, editable
 * en los campos con rango) y al pie `Calculate`/`Cancel`; a la derecha, la
 * playera (mecanismo focal), el resumen de la inversion y la salida completa
 * del motor (con scroll). La barra inferior de magnitudes y la tabla de
 * estaciones no viven aqui: son de la pagina `traza`.
 *
 * Todo el estado y los textos vienen de la fachada C (`BpqMww`): el panel no
 * conoce csnwphase, solo `BpqMwwParamRow` y cadenas ya formateadas.
 */
#ifndef BPQ_MWW_PANEL_H
#define BPQ_MWW_PANEL_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWidget>

#include "bpq_capi.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTimer;
class BpqCanvasWidget;

/* Alto comun de los cuadros de resultado (el de la GTK, +50 %). */
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

    /* Resumen para `--dump` (tabla de 2 columnas, filas y editables). */
    QString dumpState() const;

signals:
    void calculateRequested();
    void cancelRequested();

private:
    void rebuildTable();
    void onCellEdited(int row, int col);
    void tick();                       /* "corriendo csnwphase… N s" */

    BpqMww         *m_mww = nullptr;
    bool            m_busy = false;
    QVector<BpqMwwParamRow> m_rows;

    QLabel          *m_status = nullptr;
    QLabel          *m_summary = nullptr;
    QLabel          *m_output = nullptr;
    QScrollArea     *m_outputScroll = nullptr;
    BpqCanvasWidget *m_ball = nullptr;
    QTableWidget    *m_table = nullptr;
    QPushButton     *m_calc = nullptr;
    QPushButton     *m_cancel = nullptr;
    QTimer          *m_tick = nullptr;
    QElapsedTimer    m_elapsed;
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
