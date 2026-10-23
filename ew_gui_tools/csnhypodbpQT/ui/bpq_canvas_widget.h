/*
 * bpq_canvas_widget.h - Widget que pinta un lienzo del prototipo.
 *
 * Unico punto de contacto entre Qt y cairo: pide a la capa C que pinte la
 * escena en el buffer de un QImage (Format_ARGB32_Premultiplied, que coincide
 * byte a byte con CAIRO_FORMAT_ARGB32 en little-endian) y lo blitea.
 * Nada de este widget conoce cairo.
 */
#ifndef BPQ_CANVAS_WIDGET_H
#define BPQ_CANVAS_WIDGET_H

#include <QPoint>
#include <QSize>
#include <QWidget>

#include "bpq_capi.h"

class QImage;

class BpqCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BpqCanvasWidget(BpqScene scene, QWidget *parent = nullptr);
    ~BpqCanvasWidget() override;

    /* Contexto de dibujo (struct plano de la capa C). Debe vivir >= que el
     * widget; NULL es valido (escena sin datos). */
    void     setContext(const void *ctx);
    /* Contexto vigente: lo necesita el export (sin el, la escena sale vacia). */
    const void *context() const { return m_ctx; }
    BpqScene scene() const { return m_scene; }

    void   requestRepaint();
    QImage grabImage() const;     /* ultimo render (para tests/export) */

    QSize  sizeHint() const override;
    QSize  minimumSizeHint() const override;

signals:
    void clicked(double x, double y);        /* clic sin arrastre (coords del widget) */
    void rightClicked(double x, double y);   /* clic derecho: quita la picada manual */
    void dragged(double dx, double dy);      /* paneo */
    void zoomed(double factor, double atX, double atY);
    /* Pulsacion/liberacion del boton izquierdo: las necesita el arrastre de la
     * franja de magnitudes, que debe decidir el agarre ANTES de moverse (el
     * `dragged` solo trae deltas). */
    void pressed(double x, double y);
    void released();

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;

private:
    BpqScene    m_scene;
    const void *m_ctx = nullptr;
    QImage     *m_img = nullptr;     /* se recrea SOLO en resize (no realloc en paint) */
    int         m_minW = 160, m_minH = 100;
    bool        m_pressed = false;
    bool        m_moved = false;
    bool        m_rpressed = false;
    QPoint      m_last;
};

#endif /* BPQ_CANVAS_WIDGET_H */
