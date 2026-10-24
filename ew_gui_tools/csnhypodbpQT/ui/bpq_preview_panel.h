/*
 * bpq_preview_panel.h - Cuadro de dos mitades al tope de *Procesar*.
 *
 * Una sola tabla de DOS filas: encabezados (Lat/Lon/Depth/Nph/RMS + ML/Mwp/Mb/
 * Ms/Mww) y valores, con los radios de magnitud en su celda. Arriba, el titulo,
 * `Origin time` y el "calculando <loc>... N s"; abajo, el error real.
 *
 * `render()` es el UNICO camino de pintado (estado, valores, color y botones se
 * actualizan juntos): con dos renders parciales el estado y los valores se
 * desincronizan -- es la leccion que dejo el modulo GTK.
 *
 * Sin relleno de fondo: el estado se ve por el color del titulo (candidata rojo,
 * elegida ambar) y `Confirm`/`Discard` solo son sensibles con candidata.
 */
#ifndef BPQ_PREVIEW_PANEL_H
#define BPQ_PREVIEW_PANEL_H

#include <QWidget>

#include "bpq_capi.h"

class QGridLayout;
class QLabel;
class QPushButton;
class QRadioButton;

class BpqPreviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit BpqPreviewPanel(QWidget *parent = nullptr);

    /* `vals` sale del nucleo puro (csnhypodbp_preview_*). `prefMag` = radio
     * marcado (0=ML .. 4=Mww, -1 = ninguno). */
    void render(const CsnhypodbpPreviewVals &vals, int prefMag);

    int  prefMag() const;
    void setPrefMag(int kind);

    /* Estructura para los tests headless: filas/columnas, encabezados, valores
     * de la fila y si hay fondo. */
    QString dump() const;

signals:
    void confirmRequested();
    void discardRequested();
    void prefMagChanged(int kind);

private:
    QLabel       *m_title = nullptr;
    QLabel       *m_otime = nullptr;
    QLabel       *m_busy = nullptr;
    QLabel       *m_err = nullptr;
    QGridLayout  *m_grid = nullptr;                    /* 2 filas x 10 columnas */
    QLabel       *m_val[CSNHYPODBP_PREVIEW_NVALS]{};   /* 5 params; mags = radio */
    QRadioButton *m_radio[CSNHYPODBP_PREVIEW_MAGS]{};
    QPushButton  *m_confirm = nullptr;
    QPushButton  *m_discard = nullptr;
    bool          m_updating = false;   /* evita re-emitir al sincronizar radios */
};

#endif /* BPQ_PREVIEW_PANEL_H */
