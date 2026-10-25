/* C5: el canvas entrega el buffer al callback, `grabImage()` devuelve ese
 * contenido byte a byte y NO reasigna el QImage dentro de un paint. */
#include <cstdio>
#include <cstring>
#include <algorithm>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include "ewguiqt/canvas.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);          \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

/* BGRA de 0x10,0x20,0x30 con alpha 0xFF = ARGB32_Premultiplied 0xFF102030. */
static const unsigned char kPattern[4] = {0x30, 0x20, 0x10, 0xFF};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    ewguiqt::CanvasWidget canvas;
    /* >= el minimo (160x100): un widget nunca queda por debajo de su minimo. */
    canvas.resize(200, 100);

    int calls = 0, lastW = 0, lastH = 0, lastStride = 0;
    canvas.setPaintFn([&](unsigned char *bgra, int w, int h, int stride) {
        calls++;
        lastW = w;
        lastH = h;
        lastStride = stride;
        for (int y = 0; y < h; ++y) {
            unsigned char *row = bgra + y * stride;
            for (int x = 0; x < w; ++x)
                std::memcpy(row + x * 4, kPattern, 4);
        }
    });

    const QImage img = canvas.grabImage();
    /* El buffer es de pixeles de DISPOSITIVO (DPR incluido), no logicos. */
    const qreal dpr = canvas.devicePixelRatioF();
    const QSize wantPx(std::max(1, int(200 * dpr)), std::max(1, int(100 * dpr)));
    CHECK(calls == 1, "el callback no se llamo al pedir grabImage");
    CHECK(QSize(lastW, lastH) == wantPx, "el callback no recibio el tamano del widget");
    CHECK(lastStride == img.bytesPerLine(), "el stride no es el del QImage");
    CHECK(img.size() == wantPx, "grabImage devolvio otro tamano");
    CHECK(img.format() == QImage::Format_ARGB32_Premultiplied, "formato de QImage inesperado");
    CHECK(img.pixelColor(0, 0) == QColor(0x10, 0x20, 0x30), "pixel 0,0 distinto del patron");
    CHECK(img.pixelColor(img.width() - 1, img.height() - 1) == QColor(0x10, 0x20, 0x30),
          "pixel final distinto del patron");

    const int allocs = canvas.bufferAllocations();
    CHECK(allocs == 1, "el buffer se reservo mas de una vez");
    CHECK(canvas.paintCount() == 1, "paintCount no contabilizo el render");

    /* Un paint con el mismo tamano NO debe reasignar. */
    QPaintEvent paintEv(QRect(0, 0, 200, 100));
    QApplication::sendEvent(&canvas, &paintEv);
    CHECK(canvas.bufferAllocations() == allocs, "paintEvent reasigno el QImage");
    CHECK(canvas.paintCount() == 2, "paintEvent no contabilizo el paint");

    /* Redimensionar SI debe reasignar (una vez). */
    QResizeEvent resizeEv(QSize(300, 150), QSize(200, 100));
    QApplication::sendEvent(&canvas, &resizeEv);
    CHECK(canvas.bufferAllocations() == allocs + 1, "resizeEvent no reasigno el buffer");

    /* Senales de raton. */
    int clicked = 0, right = 0, dragged = 0, zoomed = 0, pressed = 0, released = 0;
    int hovered = 0, left = 0;
    double lastFactor = 0;
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::clicked, [&](double, double) { clicked++; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::rightClicked, [&](double, double) { right++; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::dragged, [&](double, double) { dragged++; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::zoomed,
                     [&](double f, double, double) { zoomed++; lastFactor = f; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::pressed, [&](double, double) { pressed++; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::released, [&]() { released++; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::hovered,
                     [&](double, double) { hovered++; });
    QObject::connect(&canvas, &ewguiqt::CanvasWidget::left, [&]() { left++; });

    /* Movimiento sin boton: la guia del picking necesita el hover. */
    {
        QMouseEvent move(QEvent::MouseMove, QPointF(40, 50), QPointF(40, 50),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
    }
    CHECK(hovered == 1, "un move sin boton debe emitir hovered");
    {
        QEvent leaveEv(QEvent::Leave);
        QApplication::sendEvent(&canvas, &leaveEv);
    }
    CHECK(left == 1, "leaveEvent debe emitir left");

    {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10), QPointF(10, 10),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &press);
        QApplication::sendEvent(&canvas, &release);
    }
    CHECK(pressed == 1, "no se emitio pressed");
    CHECK(clicked == 1, "un clic sin arrastre debe emitir clicked");
    CHECK(released == 1, "no se emitio released");

    {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(20, 10), QPointF(20, 10),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(20, 10), QPointF(20, 10),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &press);
        QApplication::sendEvent(&canvas, &move);
        QApplication::sendEvent(&canvas, &release);
    }
    CHECK(dragged == 1, "un arrastre de mas de 2 px debe emitir dragged");
    CHECK(clicked == 1, "un arrastre no debe emitir clicked");

    {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                            Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &press);
        QApplication::sendEvent(&canvas, &release);
    }
    CHECK(right == 1, "el clic derecho debe emitir rightClicked");

    {
        const QPointF pos(30, 30);
        QWheelEvent wheel(pos, pos, QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&canvas, &wheel);
    }
    CHECK(zoomed == 1 && lastFactor > 1.19 && lastFactor < 1.21,
          "la rueda debe emitir zoomed(1.2)");

    /* El minimo es el declarado, no el que pida el contenido. */
    CHECK(canvas.minimumSizeHint() == QSize(160, 100), "minimumSizeHint por defecto distinto");
    canvas.setMinSizeHint(300, 200);
    CHECK(canvas.minimumSizeHint() == QSize(300, 200), "setMinSizeHint no se aplica");

    if (g_fail == 0)
        std::printf("OK: canvas entrega BGRA, no reasigna en paint y emite sus senales\n");
    return g_fail;
}
