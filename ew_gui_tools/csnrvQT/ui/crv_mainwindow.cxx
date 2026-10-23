/* Ventana de csnrvQT: port de `on_activate` (csnrv.c:699-839).
 *
 * La UI solo pinta el snapshot y traduce gestos a operaciones de la capa C:
 *   - sin datos: "Esperando Datos..." y el mapa en fit al mundo;
 *   - zoom = rueda (factor 1.2 de CanvasWidget) ANCLADO AL CURSOR;
 *   - paneo = arrastre, en pixeles fisicos (x devicePixelRatio);
 *   - qid nuevo => crv_map_reset (InitialZoom y pan 0);
 *   - Export view... re-renderiza con cairo a PNG/SVG/PDF;
 *   - Layers... son QCheckBox (WSLg #1299: nada de desplegables).
 *
 * El menu es QMenuBar NATIVO (patron csnhypodbpQT): no crea superficies
 * xdg_popup que quedan detras de la ventana bajo WSLg.
 */
#include "crv_mainwindow.h"

#include <algorithm>
#include <cstring>
#include <ctime>

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ewguiqt/theme.h"

#include "crv_canvas_widget.h"
#include "crv_overlay_dialog.h"

CrvMainWindow::CrvMainWindow(CrvConfig *cfg, CrvCore *core, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg), m_core(core)
{
    setWindowTitle(QStringLiteral("CSNrv - Report Viewer"));
    resize(450, 780);
    setMinimumSize(360, 480);

    std::memset(&m_snap, 0, sizeof m_snap);
    m_map = crv_map_new(cfg);
    crv_map_view_init(&m_view, cfg ? cfg->initial_zoom : 8.0);

    buildMenu();
    buildUi();
    if (m_map)
        loadMapImage();

    if (m_core) {
        crv_core_snapshot(m_core, &m_snap);
        {
            const char *dberr = crv_core_db_error(m_core);
            if (dberr && dberr[0])
                crv_logf("BD: %s\n", dberr);
        }
    }
    setSnapshot(m_snap);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);   /* main.c:2485: g_timeout_add(1000, ...) */
    connect(m_timer, &QTimer::timeout, this, &CrvMainWindow::onTick);
    m_timer->start();
}

CrvMainWindow::~CrvMainWindow()
{
    /* core y cfg los libera main.cxx despues de app.exec(). El canvas puede
     * recibir un paint durante la destruccion: se le quita el mapa antes de
     * liberarlo (con NULL pinta el lienzo gris). */
    if (m_canvas)
        m_canvas->setMap(nullptr);
    crv_map_free(m_map);
}

void CrvMainWindow::buildMenu()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *exp = file->addAction(QStringLiteral("Export view..."));
    connect(exp, &QAction::triggered, this, &CrvMainWindow::onExport);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    m_dark = view->addAction(QStringLiteral("Dark mode"));
    m_dark->setCheckable(true);
    connect(m_dark, &QAction::toggled, this, &CrvMainWindow::onToggleDark);

    QMenu *ov = menuBar()->addMenu(QStringLiteral("&Overlay"));
    QAction *layers = ov->addAction(QStringLiteral("Layers..."));
    connect(layers, &QAction::triggered, this, &CrvMainWindow::onLayers);
    QAction *reload = ov->addAction(QStringLiteral("Reload"));
    connect(reload, &QAction::triggered, this,
            &CrvMainWindow::onReloadOverlays);
}

void CrvMainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(central);
    v->setContentsMargins(15, 15, 15, 15);
    v->setSpacing(10);

    auto separator = [this]() {
        QFrame *f = new QFrame(this);
        f->setFrameShape(QFrame::HLine);
        f->setFrameShadow(QFrame::Sunken);
        return f;
    };

    m_origin = new QLabel(this);
    m_origin->setTextFormat(Qt::RichText);
    m_origin->setAlignment(Qt::AlignHCenter);
    m_origin->setWordWrap(true);
    v->addWidget(m_origin);

    m_coords = new QLabel(this);
    m_coords->setTextFormat(Qt::RichText);
    m_coords->setAlignment(Qt::AlignHCenter);
    v->addWidget(m_coords);

    m_depth = new QLabel(this);
    m_depth->setTextFormat(Qt::RichText);
    m_depth->setAlignment(Qt::AlignHCenter);
    v->addWidget(m_depth);

    v->addWidget(separator());

    QLabel *timerTitle =
        new QLabel(QStringLiteral("<span style='font-size:large'>"
                                  "Time Since Quake:</span>"),
                   this);
    timerTitle->setTextFormat(Qt::RichText);
    timerTitle->setAlignment(Qt::AlignHCenter);
    v->addWidget(timerTitle);

    m_elapsed = new QLabel(this);
    m_elapsed->setTextFormat(Qt::RichText);
    m_elapsed->setAlignment(Qt::AlignHCenter);
    v->addWidget(m_elapsed);

    v->addWidget(separator());

    m_mags = new QTableWidget(4, 3, this);
    m_mags->setHorizontalHeaderLabels(QStringList()
                                      << QStringLiteral("Type")
                                      << QStringLiteral("Magnitude")
                                      << QStringLiteral("Stations"));
    m_mags->verticalHeader()->setVisible(false);
    m_mags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mags->setSelectionMode(QAbstractItemView::NoSelection);
    m_mags->setFocusPolicy(Qt::NoFocus);
    m_mags->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mags->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mags->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mags->setShowGrid(false);
    m_mags->setFixedHeight(160);

    {
        static const char *const types[4] = { "Ml", "Mwp", "Mb", "Ms" };
        for (int i = 0; i < 4; i++) {
            QTableWidgetItem *t =
                new QTableWidgetItem(QString::fromLatin1(types[i]));
            t->setTextAlignment(Qt::AlignCenter);
            m_mags->setItem(i, 0, t);
            for (int c = 1; c <= 2; c++) {
                QTableWidgetItem *it = new QTableWidgetItem(QStringLiteral("--"));
                it->setTextAlignment(Qt::AlignCenter);
                m_mags->setItem(i, c, it);
            }
        }
    }
    v->addWidget(m_mags);

    m_canvas = new CrvCanvasWidget(this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMinimumSize(320, 320);
    m_canvas->setMap(m_map);
    m_canvas->setView(&m_view);
    m_canvas->setSnapshot(&m_snap);
    /* CanvasWidget emite los gestos en pixeles LOGICOS; el mapa trabaja en
     * FISICOS (su buffer es width()*devicePixelRatioF()), asi que hay que
     * multiplicar por el DPR o el paneo va a media velocidad y el ancla del
     * zoom no cae bajo el puntero. */
    connect(m_canvas, &CrvCanvasWidget::zoomed, this,
            [this](double factor, double x, double y) {
        const qreal d = m_canvas->devicePixelRatioF();
        crv_map_zoom_at(&m_view, int(m_canvas->width() * d),
                        int(m_canvas->height() * d), x * d, y * d, factor);
        m_canvas->requestRepaint();
    });
    connect(m_canvas, &CrvCanvasWidget::dragged, this,
            [this](double dx, double dy) {
        const qreal d = m_canvas->devicePixelRatioF();
        crv_map_pan(&m_view, dx * d, dy * d);
        m_canvas->requestRepaint();
    });
    v->addWidget(m_canvas, 1);

    setCentralWidget(central);
}

void CrvMainWindow::loadMapImage()
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
        std::fprintf(stderr, "csnrvQT: no se pudo cargar el mapa <%s>\n",
                     m_cfg->map_image);
        crv_logf("no se pudo cargar el mapa %s\n", m_cfg->map_image);
        return;
    }
    /* ARGB32 en little-endian es BGRA, lo que espera cairo. */
    img = img.convertToFormat(QImage::Format_ARGB32);
    crv_map_set_background(m_map, img.constBits(), img.width(), img.height(),
                           img.bytesPerLine());
    if (!crv_map_has_background(m_map)) {
        std::fprintf(stderr,
                     "csnrvQT: fondo no aceptado (%dx%d stride=%d)\n",
                     img.width(), img.height(), int(img.bytesPerLine()));
        crv_logf("fondo no aceptado (%dx%d)\n", img.width(), img.height());
    }
}

void CrvMainWindow::setSnapshot(const CrvSnapshot &s)
{
    char buf[512];
    m_snap = s;

    if (s.has_data) {
        crv_fmt_origin(s.origin_time, buf, sizeof buf);
        QString origin = QString::fromUtf8(buf);
        origin.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        m_origin->setText(
            QStringLiteral("<span style='font-size:xx-large;font-weight:bold;"
                           "color:#0055a4'>%1</span>")
                .arg(origin));

        crv_fmt_coords(s.lat, s.lon, buf, sizeof buf);
        m_coords->setText(
            QStringLiteral("<span style='font-size:large'>%1</span>")
                .arg(QString::fromUtf8(buf)));

        if (s.has_depth) {
            crv_fmt_depth(s.depth_km, buf, sizeof buf);
            m_depth->setText(
                QStringLiteral("<span style='font-size:large'>%1</span>")
                    .arg(QString::fromUtf8(buf)));
        } else {
            m_depth->clear();
        }
    } else {
        m_origin->setText(
            QStringLiteral("<span style='font-size:xx-large;font-weight:bold;"
                           "color:gray'>Esperando Datos...</span>"));
        m_coords->clear();
        m_depth->clear();
    }

    refreshMags();
    refreshElapsed();
    m_canvas->setSnapshot(&m_snap);
    m_canvas->requestRepaint();
}

void CrvMainWindow::refreshMags()
{
    const struct {
        double v;
        int n;
    } rows[4] = {
        { m_snap.mag.ml, m_snap.mag.n_ml },
        { m_snap.mag.mwp, m_snap.mag.n_mwp },
        { m_snap.mag.mb, m_snap.mag.n_mb },
        { m_snap.mag.ms, m_snap.mag.n_ms },
    };

    /* Texto normal = WindowText (el mismo de los QLabel; el tema oscuro lo
     * ajusta). Se usa un brush valido en vez de dejar el ForegroundRole como
     * QVariant() para no depender de lo que haga el delegate. */
    const QBrush textBrush = m_mags->palette().windowText();
    const QBrush grayBrush(QColor(128, 128, 128));

    for (int i = 0; i < 4; i++) {
        QTableWidgetItem *type = m_mags->item(i, 0);
        QTableWidgetItem *val = m_mags->item(i, 1);
        QTableWidgetItem *stn = m_mags->item(i, 2);
        if (!val || !stn)
            continue;
        if (type)
            type->setForeground(textBrush);
        if (rows[i].v > 0.0) {
            val->setText(QString::number(rows[i].v, 'f', 1));
            stn->setText(QString::number(rows[i].n));
            val->setForeground(textBrush);
            stn->setForeground(textBrush);
        } else {
            val->setText(QStringLiteral("--"));
            stn->setText(QStringLiteral("--"));
            val->setForeground(grayBrush);
            stn->setForeground(grayBrush);
        }
    }
}

void CrvMainWindow::refreshElapsed()
{
    char buf[64];
    crv_fmt_elapsed(std::time(nullptr), m_snap.origin_time, m_snap.has_data,
                    buf, sizeof buf);
    const bool none = (std::strcmp(buf, "--:--:--") == 0);
    m_elapsed->setText(
        QStringLiteral("<span style='font-size:24pt;font-weight:bold;"
                       "color:%1'>%2</span>")
            .arg(none ? QStringLiteral("gray") : QStringLiteral("blue"),
                 QString::fromUtf8(buf)));
}

void CrvMainWindow::onTick()
{
    if (!m_core) {
        refreshElapsed();
        return;
    }

    const int flags = crv_core_tick(m_core, std::time(nullptr));
    if (flags & CRV_CHANGED_EVENT)
        crv_map_reset(&m_view);

    if (flags & (CRV_CHANGED_SUMMARY | CRV_CHANGED_MAG | CRV_CHANGED_EVENT)) {
        CrvSnapshot s;
        crv_core_snapshot(m_core, &s);
        setSnapshot(s);
    } else {
        refreshElapsed();
    }

    if (flags & CRV_CHANGED_QUIT)
        close();
}

void CrvMainWindow::onExport()
{
    if (!m_map || !m_canvas)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export view"), QString(),
        QStringLiteral("PNG (*.png);;SVG (*.svg);;PDF (*.pdf)"));
    if (path.isEmpty())
        return;

    CrvFmt fmt = CRV_FMT_PNG;
    if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
        fmt = CRV_FMT_SVG;
    else if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        fmt = CRV_FMT_PDF;

    const qreal d = m_canvas->devicePixelRatioF();
    const int w = std::max(1, int(m_canvas->width() * d));
    const int h = std::max(1, int(m_canvas->height() * d));
    const QByteArray local = path.toLocal8Bit();

    if (crv_map_export(m_map, &m_view, &m_snap, local.constData(), fmt, w, h) != 0)
        QMessageBox::warning(this, QStringLiteral("Export view"),
                             QStringLiteral("No se pudo escribir %1").arg(path));
}

void CrvMainWindow::onLayers()
{
    if (!m_map || crv_map_nlayers(m_map) <= 0)
        return;
    CrvOverlayDialog dlg(m_map, this,
                         [this]() { m_canvas->requestRepaint(); });
    dlg.exec();
    m_canvas->requestRepaint();
}

void CrvMainWindow::onReloadOverlays()
{
    if (!m_map)
        return;
    crv_map_reload(m_map);
    m_canvas->requestRepaint();
}

void CrvMainWindow::onToggleDark(bool on)
{
    ewguiqt::set_dark_mode(*qApp, on);
    /* La tabla pinta con el color de SU paleta: refrescarla tras el cambio. */
    m_mags->setPalette(qApp->palette());
    refreshMags();
    m_canvas->requestRepaint();
}

QString CrvMainWindow::dumpTree() const
{
    QString out;
    char buf[64];

    out += QStringLiteral("window=%1\n").arg(windowTitle());
    out += QStringLiteral("has_data=%1 has_depth=%2 qid=%3 version=%4\n")
               .arg(m_snap.has_data)
               .arg(m_snap.has_depth)
               .arg(m_snap.qid)
               .arg(m_snap.version);
    out += QStringLiteral("lat=%1 lon=%2 depth=%3 origin=%4\n")
               .arg(m_snap.lat, 0, 'f', 3)
               .arg(m_snap.lon, 0, 'f', 3)
               .arg(m_snap.depth_km, 0, 'f', 1)
               .arg(m_snap.origin_time, 0, 'f', 2);
    out += QStringLiteral("mag=%1/%2 %3/%4 %5/%6 %7/%8\n")
               .arg(m_snap.mag.ml, 0, 'f', 1)
               .arg(m_snap.mag.n_ml)
               .arg(m_snap.mag.mwp, 0, 'f', 1)
               .arg(m_snap.mag.n_mwp)
               .arg(m_snap.mag.mb, 0, 'f', 1)
               .arg(m_snap.mag.n_mb)
               .arg(m_snap.mag.ms, 0, 'f', 1)
               .arg(m_snap.mag.n_ms);

    crv_fmt_elapsed(std::time(nullptr), m_snap.origin_time, m_snap.has_data,
                    buf, sizeof buf);
    out += QStringLiteral("elapsed=%1\n").arg(QString::fromUtf8(buf));
    out += QStringLiteral("zoom=%1 pan=%2,%3 layers=%4 bg=%5\n")
               .arg(m_view.zoom, 0, 'f', 2)
               .arg(m_view.pan_x, 0, 'f', 1)
               .arg(m_view.pan_y, 0, 'f', 1)
               .arg(m_map ? crv_map_nlayers(m_map) : 0)
               .arg(m_map ? crv_map_has_background(m_map) : 0);
    out += QStringLiteral("table=%1x%2\n")
               .arg(m_mags ? m_mags->rowCount() : 0)
               .arg(m_mags ? m_mags->columnCount() : 0);
    out += QStringLiteral("menu=%1\n")
               .arg(menuBar() ? menuBar()->actions().size() : 0);
    return out;
}

QString CrvMainWindow::dumpLayout() const
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
    out += QStringLiteral("table=%1x%2\n")
               .arg(m_mags ? m_mags->width() : 0)
               .arg(m_mags ? m_mags->height() : 0);
    return out;
}

bool CrvMainWindow::saveShot(const QString &path)
{
    return grab().save(path);
}

QString CrvMainWindow::interactProbe()
{
    if (!m_canvas || !m_map)
        return QStringLiteral("error=sin-canvas\n");

    resize(800, 900);
    show();
    QCoreApplication::processEvents();

    const qreal d = m_canvas->devicePixelRatioF();
    const int cw = int(m_canvas->width() * d);
    const int ch = int(m_canvas->height() * d);
    if (cw <= 0 || ch <= 0)
        return QStringLiteral("error=canvas-sin-tamano\n");

    /* Ancla: un punto del mundo a ~1° al este del epicentro (cae dentro del
     * canvas a InitialZoom 8). No el epicentro: cae en el centro y el zoom no
     * moveria el pan, asi que el test no probaria el anclaje. */
    const double wlon = m_snap.lon + 1.0;
    const double wlat = m_snap.lat;

    auto project = [&](double *x, double *y) {
        crv_map_project(m_map, &m_view, &m_snap, cw, ch, wlat, wlon, x, y);
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

    /* Rueda arriba EXACTAMENTE sobre el ancla (en px logicos): es la
     * condicion que el test verifica. */
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
    return out;
}
