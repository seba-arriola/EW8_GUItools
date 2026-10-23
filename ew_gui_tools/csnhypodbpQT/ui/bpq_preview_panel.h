/*
 * bpq_preview_panel.h - Cuadro de dos mitades al tope de *Procesar*.
 *
 * Paridad con csnhypodbp_preview.c (GTK): a la izquierda titulo, Origin time
 * (con el "calculando <loc>... N s" al lado mientras corre) y Lat/Lon/Depth/Nph/
 * RMS; a la derecha la magnitud ML/Mwp/Mb/Ms/Mww con sus radios compactos.
 *
 * `render()` es el UNICO camino de pintado (estado, valores, color y botones se
 * actualizan juntos): con dos renders parciales el estado y los valores se
 * desincronizan -- es la leccion que dejo el modulo GTK.
 *
 * Colores del fondo: candidata del `locate` = rojo muy leve; solucion elegida =
 * ambar suave; sin seleccion = sin fondo y todo en guiones. `Confirm`/`Discard`
 * solo son sensibles con candidata.
 */
#ifndef BPQ_PREVIEW_PANEL_H
#define BPQ_PREVIEW_PANEL_H

#include <QWidget>

#include "bpq_capi.h"

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

signals:
    void confirmRequested();
    void discardRequested();
    void prefMagChanged(int kind);

private:
    void setBg(QWidget *w, const char *hexOrNull);

    QLabel       *m_title = nullptr;
    QLabel       *m_otime = nullptr;
    QLabel       *m_busy = nullptr;
    QLabel       *m_err = nullptr;
    QLabel       *m_val[CSNHYPODBP_PREVIEW_NVALS]{};   /* 5 params + 5 mags */
    QRadioButton *m_radio[CSNHYPODBP_PREVIEW_MAGS]{};
    QPushButton  *m_confirm = nullptr;
    QPushButton  *m_discard = nullptr;
    QWidget      *m_left = nullptr;
    QWidget      *m_right = nullptr;
    bool          m_updating = false;   /* evita re-emitir al sincronizar radios */
};

#endif /* BPQ_PREVIEW_PANEL_H */
