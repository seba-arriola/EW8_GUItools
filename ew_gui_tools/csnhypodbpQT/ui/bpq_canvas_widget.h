/*
 * bpq_canvas_widget.h - Lienzo de las escenas de csnhypodbpQT.
 *
 * Es un adaptador fino sobre ewguiqt::CanvasWidget (commonqt/): el widget
 * generico entrega el buffer BGRA y aqui se le dice QUE escena y QUE contexto
 * pintar con la capa C. Las senales (clicked/rightClicked/dragged/zoomed/
 * pressed/released) y `grabImage()` son las del widget compartido, asi que
 * bpq_mainwindow.cxx y bpq_mww_panel.cxx no cambian.
 *
 * La doble memoria y la regla "el QImage solo se recrea al redimensionar"
 * viven en commonqt (tests/test_canvas.cxx).
 */
#ifndef BPQ_CANVAS_WIDGET_H
#define BPQ_CANVAS_WIDGET_H

#include <QWidget>

#include "bpq_capi.h"
#include "ewguiqt/canvas.h"

class BpqCanvasWidget : public ewguiqt::CanvasWidget
{
    Q_OBJECT
public:
    explicit BpqCanvasWidget(BpqScene scene, QWidget *parent = nullptr)
        : ewguiqt::CanvasWidget(parent), m_scene(scene)
    {
        int w = 160, h = 100;
        if (bpq_scene_min_size(scene, &w, &h) == 0)
            setMinSizeHint(w, h);
        setPaintFn([this](unsigned char *bgra, int w, int h, int stride) {
            bpq_scene_paint(m_scene, m_ctx, bgra, w, h, stride);
        });
    }

    /* Contexto de dibujo (struct plano de la capa C). Debe vivir >= que el
     * widget; NULL es valido (escena sin datos). */
    void setContext(const void *ctx)
    {
        m_ctx = ctx;
        requestRepaint();
    }
    /* Contexto vigente: lo necesita el export (sin el, la escena sale vacia). */
    const void *context() const { return m_ctx; }
    BpqScene scene() const { return m_scene; }

private:
    BpqScene    m_scene;
    const void *m_ctx = nullptr;
};

#endif /* BPQ_CANVAS_WIDGET_H */
