/* Ver include/ewguiqt/canvas.h.
 *
 * Port de bpq_canvas_widget.cxx sin el `BpqScene`: aqui el dibujo es un
 * std::function que recibe el buffer. El QImage se reserva por tamano de
 * dispositivo y se reutiliza entre paints; solo cambia de tamano al
 * redimensionar (o en el primer paint), nunca en medio de un paint.
 */
#include "ewguiqt/canvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>

namespace ewguiqt {

CanvasWidget::CanvasWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(m_minW, m_minH);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    /* Sin boton tambien llegan los moves: la guia vertical del picking sigue
     * al puntero. Los modulos que no conectan `hovered` no cambian. */
    setMouseTracking(true);
}

CanvasWidget::~CanvasWidget() = default;

void CanvasWidget::setPaintFn(PaintFn fn)
{
    m_fn = std::move(fn);
    requestRepaint();
}

void CanvasWidget::setMinSizeHint(int width, int height)
{
    m_minW = std::max(1, width);
    m_minH = std::max(1, height);
    setMinimumSize(m_minW, m_minH);
}

void CanvasWidget::requestRepaint()
{
    update();
}

void CanvasWidget::ensureImage(const QSize &pixels)
{
    if (m_img.width() == pixels.width() && m_img.height() == pixels.height())
        return;
    m_img = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
    m_allocs++;
}

QImage CanvasWidget::grabImage() const
{
    CanvasWidget *self = const_cast<CanvasWidget *>(this);
    const qreal dpr = self->devicePixelRatioF();
    const QSize px(std::max(1, int(self->width() * dpr)),
                   std::max(1, int(self->height() * dpr)));
    self->ensureImage(px);
    self->m_img.fill(Qt::transparent);
    if (self->m_fn)
        self->m_fn(self->m_img.bits(), px.width(), px.height(), self->m_img.bytesPerLine());
    self->m_paints++;
    return self->m_img.copy();
}

QSize CanvasWidget::sizeHint() const
{
    return QSize(480, 320);
}

QSize CanvasWidget::minimumSizeHint() const
{
    return QSize(m_minW, m_minH);
}

void CanvasWidget::resizeEvent(QResizeEvent *ev)
{
    const qreal dpr = devicePixelRatioF();
    ensureImage(QSize(std::max(1, int(ev->size().width() * dpr)),
                      std::max(1, int(ev->size().height() * dpr))));
    QWidget::resizeEvent(ev);
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    const qreal dpr = devicePixelRatioF();
    const QSize px(std::max(1, int(width() * dpr)), std::max(1, int(height() * dpr)));
    ensureImage(px);

    m_img.fill(Qt::transparent);
    if (m_fn)
        m_fn(m_img.bits(), px.width(), px.height(), m_img.bytesPerLine());
    m_paints++;

    QPainter p(this);
    p.drawImage(QRect(0, 0, width(), height()), m_img);
}

void CanvasWidget::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        m_pressed = true;
        m_moved = false;
        m_last = ev->position().toPoint();
        emit pressed(ev->position().x(), ev->position().y());
        ev->accept();
        return;
    }
    if (ev->button() == Qt::RightButton) {
        m_rpressed = true;
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *ev)
{
    emit hovered(ev->position().x(), ev->position().y());
    if (m_pressed) {
        const QPoint p = ev->position().toPoint();
        const QPoint d = p - m_last;
        if (d.manhattanLength() > 2)
            m_moved = true;
        if (m_moved) {
            emit dragged(double(d.x()), double(d.y()));
            m_last = p;
            ev->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(ev);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        if (!m_moved) {
            const QPoint p = ev->position().toPoint();
            emit clicked(double(p.x()), double(p.y()));
        }
        emit released();
        ev->accept();
        return;
    }
    if (ev->button() == Qt::RightButton && m_rpressed) {
        const QPoint p = ev->position().toPoint();
        m_rpressed = false;
        emit rightClicked(double(p.x()), double(p.y()));
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
}

void CanvasWidget::leaveEvent(QEvent *ev)
{
    emit left();
    QWidget::leaveEvent(ev);
}

void CanvasWidget::wheelEvent(QWheelEvent *ev)
{
    const int dy = ev->angleDelta().y();
    if (dy != 0) {
        const QPointF p = ev->position();
        emit zoomed(dy > 0 ? 1.2 : 1.0 / 1.2, p.x(), p.y());
        ev->accept();
        return;
    }
    QWidget::wheelEvent(ev);
}

} /* namespace ewguiqt */
