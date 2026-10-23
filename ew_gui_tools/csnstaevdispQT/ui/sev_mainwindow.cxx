/* Ventana de csnstaevdispQT: port de `on_activate`
 * (csnstaevdisp.c:800-875).
 *
 * La UI solo pinta el snapshot y traduce gestos a operaciones de la capa C:
 *   - zoom = rueda (factor 1.2 de CanvasWidget) ANCLADO AL CURSOR;
 *   - paneo = arrastre, en pixeles fisicos (x devicePixelRatio);
 *   - el refresco de datos (2 s) NO resetea zoom/pan;
 *   - Export view... re-renderiza con cairo a PNG/SVG/PDF;
 *   - Layers... son QCheckBox (WSLg #1299: nada de desplegables).
 *
 * El menu es QMenuBar NATIVO (patron csnrvQT/csnhypodbpQT): no crea superficies
 * xdg_popup que quedan detras de la ventana bajo WSLg.
 */
#include "sev_mainwindow.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ewguiqt/theme.h"

#include "sev_canvas_widget.h"
#include "sev_overlay_dialog.h"

SevMainWindow::SevMainWindow(SevConfig *cfg, SevCore *core, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg), m_core(core)
{
    setWindowTitle(QStringLiteral("Network & Seismicity Viewer"));
    resize(800, 600);

    std::memset(&m_snap, 0, sizeof m_snap);
    m_map = sev_map_new(cfg);
    sev_map_view_init(&m_view);

    buildMenu();
    buildUi();
    if (m_map)
        loadMapImage();

    if (m_core) {
        sev_core_snapshot(m_core, &m_snap);
        const char *dberr = sev_core_db_error(m_core);
        if (dberr && dberr[0])
            sev_logf("BD: %s\n", dberr);
    }
    setSnapshot(m_snap);

    if (m_core) {
        m_dataTimer = new QTimer(this);
        m_dataTimer->setInterval(2000);   /* csnstaevdisp.c:871 */
        connect(m_dataTimer, &QTimer::timeout, this,
                &SevMainWindow::onTickData);
        m_dataTimer->start();

        m_bgTimer = new QTimer(this);
        m_bgTimer->setInterval(1000);     /* csnstaevdisp.c:872 */
        connect(m_bgTimer, &QTimer::timeout, this,
                &SevMainWindow::onTickBackground);
        m_bgTimer->start();
    }
}

SevMainWindow::~SevMainWindow()
{
    /* core y cfg los libera main.cxx despues de app.exec(). El canvas puede
     * recibir un paint durante la destruccion: se le quita el mapa antes de
     * liberarlo (con NULL pinta el lienzo gris). */
    if (m_canvas)
        m_canvas->setMap(nullptr);
    sev_map_free(m_map);
}

void SevMainWindow::buildMenu()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *exp = file->addAction(QStringLiteral("Export view..."));
    connect(exp, &QAction::triggered, this, &SevMainWindow::onExport);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    QAction *events = view->addAction(QStringLiteral("Events number..."));
    connect(events, &QAction::triggered, this, &SevMainWindow::onEventCount);
    m_dark = view->addAction(QStringLiteral("Dark mode"));
    m_dark->setCheckable(true);
    m_dark->setChecked(ewguiqt::dark_mode(*qApp));
    connect(m_dark, &QAction::toggled, this, &SevMainWindow::onToggleDark);

    QMenu *ov = menuBar()->addMenu(QStringLiteral("&Overlay"));
    QAction *layers = ov->addAction(QStringLiteral("Layers..."));
    connect(layers, &QAction::triggered, this, &SevMainWindow::onLayers);
    QAction *reload = ov->addAction(QStringLiteral("Reload"));
    connect(reload, &QAction::triggered, this,
            &SevMainWindow::onReloadOverlays);
}

void SevMainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_canvas = new SevCanvasWidget(this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMap(m_map);
    m_canvas->setView(&m_view);
    m_canvas->setSnapshot(&m_snap);
    /* CanvasWidget emite los gestos en pixeles LOGICOS; el mapa trabaja en
     * FISICOS (su buffer es width()*devicePixelRatioF()), asi que hay que
     * multiplicar por el DPR o el paneo va a media velocidad y el ancla del
     * zoom no cae bajo el puntero. */
    connect(m_canvas, &SevCanvasWidget::zoomed, this,
            [this](double factor, double x, double y) {
        const qreal d = m_canvas->devicePixelRatioF();
        sev_map_zoom_at(&m_view, int(m_canvas->width() * d),
                        int(m_canvas->height() * d), x * d, y * d, factor);
        m_canvas->requestRepaint();
    });
    connect(m_canvas, &SevCanvasWidget::dragged, this,
            [this](double dx, double dy) {
        const qreal d = m_canvas->devicePixelRatioF();
        sev_map_pan(&m_view, dx * d, dy * d);
        m_canvas->requestRepaint();
    });
    v->addWidget(m_canvas, 1);

    setCentralWidget(central);
}

void SevMainWindow::loadMapImage()
{
    if (!m_cfg || !m_cfg->map_image[0])
        return;

    /* Qt6 limita por defecto la decodificacion a 128 MB y el mapa 2:1 de
     * 10800x5400 ocupa 233 MB en ARGB32: sin esto `QImage` lo RECHAZA
     * ("exceeds the current allocation limit") y el visor queda sin fondo.
     * El limite es global del proceso; 1 GB deja margen a mapas mayores. */
    QImageReader::setAllocationLimit(1024);

    QImage img(QString::fromUtf8(m_cfg->map_image));
    if (img.isNull()) {
        std::fprintf(stderr,
                     "csnstaevdispQT: no se pudo cargar el mapa <%s>\n",
                     m_cfg->map_image);
        sev_logf("no se pudo cargar el mapa %s\n", m_cfg->map_image);
        return;
    }
    /* ARGB32 en little-endian es BGRA, lo que espera cairo. */
    img = img.convertToFormat(QImage::Format_ARGB32);
    sev_map_set_background(m_map, img.constBits(), img.width(), img.height(),
                           img.bytesPerLine());
    if (!sev_map_has_background(m_map)) {
        std::fprintf(stderr,
                     "csnstaevdispQT: fondo no aceptado (%dx%d stride=%d)\n",
                     img.width(), img.height(), int(img.bytesPerLine()));
        sev_logf("fondo no aceptado (%dx%d)\n", img.width(), img.height());
    }
}

void SevMainWindow::setSnapshot(const SevSnapshot &s)
{
    m_snap = s;
    if (m_canvas) {
        m_canvas->setSnapshot(&m_snap);
        m_canvas->requestRepaint();
    }
}

void SevMainWindow::onTickData()
{
    if (!m_core)
        return;

    sev_core_refresh(m_core);
    SevSnapshot s;
    sev_core_snapshot(m_core, &s);
    /* OJO: NO tocar m_view. csnstaevdisp conserva zoom/pan entre recargas. */
    setSnapshot(s);
}

void SevMainWindow::onTickBackground()
{
    if (!m_core)
        return;
    if (sev_core_background(m_core, (double)std::time(nullptr)))
        close();
}

void SevMainWindow::onExport()
{
    if (!m_map || !m_canvas)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export view"), QStringLiteral("csn_view.svg"),
        QStringLiteral("PNG (*.png);;SVG (*.svg);;PDF (*.pdf)"));
    if (path.isEmpty())
        return;

    SevFmt fmt = SEV_FMT_PNG;
    if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
        fmt = SEV_FMT_SVG;
    else if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        fmt = SEV_FMT_PDF;

    const qreal d = m_canvas->devicePixelRatioF();
    const int w = std::max(1, int(m_canvas->width() * d));
    const int h = std::max(1, int(m_canvas->height() * d));
    const QByteArray local = path.toLocal8Bit();

    if (sev_map_export(m_map, &m_view, &m_snap, local.constData(),
                       fmt, w, h) != 0)
        QMessageBox::warning(this, QStringLiteral("Export view"),
                             QStringLiteral("No se pudo escribir %1").arg(path));
}

void SevMainWindow::onLayers()
{
    if (!m_map || sev_map_nlayers(m_map) <= 0)
        return;
    SevOverlayDialog dlg(m_map, this,
                         [this]() { m_canvas->requestRepaint(); });
    dlg.exec();
    m_canvas->requestRepaint();
}

void SevMainWindow::onReloadOverlays()
{
    if (!m_map)
        return;
    sev_map_reload_overlays(m_map);
    m_canvas->requestRepaint();
}

void SevMainWindow::onEventCount()
{
    if (!m_map)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Events number"));
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QStringLiteral("Number of events to display:"),
                            &dlg));

    QSpinBox *spin = new QSpinBox(&dlg);
    spin->setRange(1, SEV_MAX_EVENTS);
    spin->setValue(sev_map_max_events(m_map));
    v->addWidget(spin);

    QDialogButtonBox *bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);

    if (dlg.exec() == QDialog::Accepted) {
        sev_map_set_max_events(m_map, spin->value());
        m_canvas->requestRepaint();
    }
}

void SevMainWindow::onToggleDark(bool on)
{
    ewguiqt::set_dark_mode(*qApp, on);
    m_canvas->requestRepaint();
}

QString SevMainWindow::dumpTree() const
{
    QString out;

    out += QStringLiteral("window=%1\n").arg(windowTitle());
    out += QStringLiteral("stations=%1 events=%2\n")
               .arg(int(m_snap.nsta))
               .arg(int(m_snap.nev));
    out += QStringLiteral("elev_min=%1 elev_max=%2\n")
               .arg(m_snap.elev_min, 0, 'f', 1)
               .arg(m_snap.elev_max, 0, 'f', 1);
    out += QStringLiteral("max_events=%1 zoom=%2 pan=%3,%4\n")
               .arg(m_map ? sev_map_max_events(m_map) : 0)
               .arg(m_view.zoom, 0, 'f', 2)
               .arg(m_view.pan_x, 0, 'f', 1)
               .arg(m_view.pan_y, 0, 'f', 1);
    out += QStringLiteral("layers=%1 bg=%2 db=%3\n")
               .arg(m_map ? sev_map_nlayers(m_map) : 0)
               .arg(m_map ? sev_map_has_background(m_map) : 0)
               .arg(m_snap.db_ok);
    out += QStringLiteral("menu=%1\n")
               .arg(menuBar() ? menuBar()->actions().size() : 0);
    return out;
}

QString SevMainWindow::dumpLayout() const
{
    QString out;

    out += QStringLiteral("window=%1x%2\n").arg(width()).arg(height());
    out += QStringLiteral("minimum=%1x%2\n")
               .arg(minimumSizeHint().width())
               .arg(minimumSizeHint().height());
    out += QStringLiteral("sizehint=%1x%2\n")
               .arg(sizeHint().width())
               .arg(sizeHint().height());
    out += QStringLiteral("canvas=%1x%2\n")
               .arg(m_canvas ? m_canvas->width() : 0)
               .arg(m_canvas ? m_canvas->height() : 0);
    return out;
}

bool SevMainWindow::saveShot(const QString &path)
{
    return grab().save(path);
}

QString SevMainWindow::interactProbe()
{
    if (!m_canvas || !m_map)
        return QStringLiteral("error=sin-canvas\n");

    resize(800, 600);
    show();
    QCoreApplication::processEvents();

    const qreal d = m_canvas->devicePixelRatioF();
    const int cw = int(m_canvas->width() * d);
    const int ch = int(m_canvas->height() * d);
    if (cw <= 0 || ch <= 0)
        return QStringLiteral("error=canvas-sin-tamano\n");

    /* Ancla: la primera estacion del fixture (~1 grado al este del centro).
     * No el centro: ahi el zoom no moveria el pan y el test no probaria el
     * anclaje. */
    const double wlat = m_snap.nsta > 0 ? m_snap.sta[0].lat : -30.0;
    const double wlon = m_snap.nsta > 0 ? m_snap.sta[0].lon : -69.0;

    auto project = [&](double *x, double *y) {
        sev_map_project(m_map, &m_view, cw, ch, wlat, wlon, x, y);
    };
    auto sendMouse = [this](QEvent::Type t, const QPointF &p,
                            Qt::MouseButton b, Qt::MouseButtons bs) {
        QMouseEvent ev(t, p, QPointF(m_canvas->mapToGlobal(p.toPoint())),
                       b, bs, Qt::NoModifier);
        QCoreApplication::sendEvent(m_canvas, &ev);
    };

    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    project(&ax, &ay);
    const double z0 = m_view.zoom;

    /* Rueda arriba EXACTAMENTE sobre el ancla (en px logicos). */
    {
        const QPointF pos(ax / d, ay / d);
        const QPoint gp = m_canvas->mapToGlobal(pos.toPoint());
        QWheelEvent ev(pos, QPointF(gp), QPoint(0, 0), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_canvas, &ev);
    }
    const double z1 = m_view.zoom;
    project(&bx, &by);
    const double pw_x = m_view.pan_x, pw_y = m_view.pan_y;

    /* Arrastre izquierdo de 30x20 px logicos. */
    sendMouse(QEvent::MouseButtonPress, QPointF(200, 200),
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPointF(230, 220),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(230, 220),
              Qt::LeftButton, Qt::NoButton);
    const double pd_x = m_view.pan_x, pd_y = m_view.pan_y;

    /* Clic izquierdo sin mover: no panea. */
    sendMouse(QEvent::MouseButtonPress, QPointF(100, 100),
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(100, 100),
              Qt::LeftButton, Qt::NoButton);
    const double pc_x = m_view.pan_x, pc_y = m_view.pan_y;

    /* Arrastre con boton derecho: tampoco panea. */
    sendMouse(QEvent::MouseButtonPress, QPointF(100, 100),
              Qt::RightButton, Qt::RightButton);
    sendMouse(QEvent::MouseMove, QPointF(150, 140),
              Qt::NoButton, Qt::RightButton);
    sendMouse(QEvent::MouseButtonRelease, QPointF(150, 140),
              Qt::RightButton, Qt::NoButton);
    const double pr_x = m_view.pan_x, pr_y = m_view.pan_y;

    /* Recarga de datos: un snapshot nuevo NO debe tocar zoom/pan. */
    SevSnapshot s2 = m_snap;
    if (s2.nev > 0)
        s2.nev--;
    setSnapshot(s2);
    const double ps_x = m_view.pan_x, ps_y = m_view.pan_y;
    const double zs = m_view.zoom;

    QString out;
    out += QStringLiteral("dpr=%1\n").arg(d, 0, 'f', 4);
    out += QStringLiteral("canvas_w=%1\n").arg(cw);
    out += QStringLiteral("canvas_h=%1\n").arg(ch);
    out += QStringLiteral("zoom_before=%1\n").arg(z0, 0, 'f', 6);
    out += QStringLiteral("zoom_after_wheel=%1\n").arg(z1, 0, 'f', 6);
    out += QStringLiteral("anchor_before_x=%1\n").arg(ax, 0, 'f', 3);
    out += QStringLiteral("anchor_before_y=%1\n").arg(ay, 0, 'f', 3);
    out += QStringLiteral("anchor_after_x=%1\n").arg(bx, 0, 'f', 3);
    out += QStringLiteral("anchor_after_y=%1\n").arg(by, 0, 'f', 3);
    out += QStringLiteral("pan_wheel_x=%1\n").arg(pw_x, 0, 'f', 4);
    out += QStringLiteral("pan_wheel_y=%1\n").arg(pw_y, 0, 'f', 4);
    out += QStringLiteral("pan_drag_x=%1\n").arg(pd_x, 0, 'f', 4);
    out += QStringLiteral("pan_drag_y=%1\n").arg(pd_y, 0, 'f', 4);
    out += QStringLiteral("pan_click_x=%1\n").arg(pc_x, 0, 'f', 4);
    out += QStringLiteral("pan_click_y=%1\n").arg(pc_y, 0, 'f', 4);
    out += QStringLiteral("pan_right_x=%1\n").arg(pr_x, 0, 'f', 4);
    out += QStringLiteral("pan_right_y=%1\n").arg(pr_y, 0, 'f', 4);
    out += QStringLiteral("zoom_snapshot=%1\n").arg(zs, 0, 'f', 6);
    out += QStringLiteral("pan_snapshot_x=%1\n").arg(ps_x, 0, 'f', 4);
    out += QStringLiteral("pan_snapshot_y=%1\n").arg(ps_y, 0, 'f', 4);
    return out;
}
