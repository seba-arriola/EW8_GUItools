/*
 * bpq_canvas_widget.cxx - Ver bpq_canvas_widget.h.
 *
 * Doble buffering: el QImage se reserva por tamano de dispositivo y se reutiliza
 * entre paints; solo resizeEvent lo recrea (regla E20 del plan: no reasignar
 * memoria en medio de un paint).
 */
#include "bpq_canvas_widget.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>

BpqCanvasWidget::BpqCanvasWidget(BpqScene scene, QWidget *parent)
    : QWidget(parent), m_scene(scene)
{
    int w = 160, h = 100;
    if (bpq_scene_min_size(scene, &w, &h) == 0) { m_minW = w; m_minH = h; }
    setMinimumSize(m_minW, m_minH);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
}

BpqCanvasWidget::~BpqCanvasWidget()
{
    delete m_img;
}

void BpqCanvasWidget::setContext(const void *ctx)
{
    m_ctx = ctx;
    requestRepaint();
}

void BpqCanvasWidget::requestRepaint()
{
    update();
}

QImage BpqCanvasWidget::grabImage() const
{
    if (!m_img) {
        BpqCanvasWidget *self = const_cast<BpqCanvasWidget *>(this);
        const qreal dpr = self->devicePixelRatioF();
        const int w = std::max(1, int(self->width() * dpr));
        const int h = std::max(1, int(self->height() * dpr));
        if (!self->m_img) self->m_img = new QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        self->m_img->fill(Qt::transparent);
        bpq_scene_paint(m_scene, m_ctx, self->m_img->bits(), w, h,
                        self->m_img->bytesPerLine());
    }
    return m_img->copy();
}

QSize BpqCanvasWidget::sizeHint() const
{
    return QSize(480, 320);
}

QSize BpqCanvasWidget::minimumSizeHint() const
{
    return QSize(m_minW, m_minH);
}

void BpqCanvasWidget::resizeEvent(QResizeEvent *ev)
{
    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, int(ev->size().width() * dpr));
    const int h = std::max(1, int(ev->size().height() * dpr));
    if (!m_img || m_img->width() != w || m_img->height() != h) {
        delete m_img;
        m_img = new QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    }
    QWidget::resizeEvent(ev);
}

void BpqCanvasWidget::paintEvent(QPaintEvent *)
{
    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, int(width() * dpr));
    const int h = std::max(1, int(height() * dpr));
    if (!m_img || m_img->width() != w || m_img->height() != h) {
        delete m_img;
        m_img = new QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    }
    if (!m_img) return;

    m_img->fill(Qt::transparent);
    bpq_scene_paint(m_scene, m_ctx, m_img->bits(), w, h, m_img->bytesPerLine());

    QPainter p(this);
    p.drawImage(QRect(0, 0, width(), height()), *m_img);
}

void BpqCanvasWidget::mousePressEvent(QMouseEvent *ev)
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

void BpqCanvasWidget::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_pressed) {
        const QPoint p = ev->position().toPoint();
        const QPoint d = p - m_last;
        if (d.manhattanLength() > 2) m_moved = true;
        if (m_moved) {
            emit dragged(double(d.x()), double(d.y()));
            m_last = p;
            ev->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(ev);
}

void BpqCanvasWidget::mouseReleaseEvent(QMouseEvent *ev)
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

void BpqCanvasWidget::wheelEvent(QWheelEvent *ev)
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
