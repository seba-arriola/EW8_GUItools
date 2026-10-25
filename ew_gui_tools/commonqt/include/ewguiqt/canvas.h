#ifndef EWGUIQT_CANVAS_H
#define EWGUIQT_CANVAS_H

/* Lienzo doble-buffer para dibujar con cairo SIN que esta lib conozca cairo.
 *
 * El widget reserva un QImage (Format_ARGB32_Premultiplied, que coincide byte a
 * byte con CAIRO_FORMAT_ARGB32 en little-endian) y llama al callback con
 * (bits, ancho, alto, stride). El callback -que vive en la capa C del modulo-
 * crea su superficie cairo sobre ese buffer y pinta. Despues el widget blitea.
 *
 * Contrato (heredado de bpq_canvas_widget.h, ya probado en WSLg):
 *   - el QImage se recrea SOLO al cambiar de tamano, nunca dentro de un paint;
 *   - `grabImage()` devuelve el ultimo render (tests y export);
 *   - arrastre > 2 px = paneo; por debajo es un clic;
 *   - rueda = zoom con factor 1.2 y la posicion del puntero.
 */

#include <functional>

#include <QImage>
#include <QPoint>
#include <QSize>
#include <QWidget>

namespace ewguiqt {

class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    using PaintFn = std::function<void(unsigned char *bgra, int width, int height, int stride)>;

    explicit CanvasWidget(QWidget *parent = nullptr);
    ~CanvasWidget() override;

    void setPaintFn(PaintFn fn);
    void setMinSizeHint(int width, int height);
    void requestRepaint();
    QImage grabImage() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /* Contadores para los tests: paints hechos y reallocaciones de buffer. */
    int paintCount() const { return m_paints; }
    int bufferAllocations() const { return m_allocs; }

signals:
    void clicked(double x, double y);
    void rightClicked(double x, double y);
    void dragged(double dx, double dy);
    void zoomed(double factor, double atX, double atY);
    void pressed(double x, double y);
    void released();
    /* Movimiento sin boton (mouse tracking) y salida del widget: los usa la
     * guia vertical del picking. Coordenadas en px LOGICOS. */
    void hovered(double x, double y);
    void left();

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;
    void leaveEvent(QEvent *ev) override;

private:
    void ensureImage(const QSize &pixels);

    PaintFn m_fn;
    QImage m_img;
    int m_minW = 160;
    int m_minH = 100;
    bool m_pressed = false;
    bool m_moved = false;
    bool m_rpressed = false;
    QPoint m_last;
    int m_paints = 0;
    int m_allocs = 0;
};

} /* namespace ewguiqt */

#endif /* EWGUIQT_CANVAS_H */
