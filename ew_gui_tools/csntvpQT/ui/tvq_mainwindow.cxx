/* Ventana de csntvpQT (port de csntvp_main.c:97-195 + csntvp_gui.c).
 *
 * La UI no calcula nada sismico: pide el estado a la capa C y le devuelve
 * gestos. Lo unico que decide aqui es el tamano del viewport (y por tanto el
 * alto de pista) y el desplazamiento vertical de la lista de estaciones.
 */
#include "tvq_mainwindow.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "ewguiqt/canvas.h"
#include "ewguiqt/theme.h"

namespace {

const char *kRoleName(int role)
{
    switch (role) {
    case TVQ_COLOUR_WAVE: return "Waveforms";
    case TVQ_COLOUR_BG:   return "Background";
    case TVQ_COLOUR_FONT: return "Font";
    case TVQ_COLOUR_SEP:  return "Separator";
    case TVQ_COLOUR_P:    return "P picks";
    case TVQ_COLOUR_S:    return "S picks";
    case TVQ_COLOUR_Z:    return "Component Z";
    case TVQ_COLOUR_N:    return "Component N";
    case TVQ_COLOUR_E:    return "Component E";
    default:              return "Colour";
    }
}

}  // namespace

TvqMainWindow::TvqMainWindow(TvqConfig *cfg, TvqCore *core, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg), m_core(core)
{
    setWindowTitle(QStringLiteral("CSNtvp - Trace Viewer-Picker (Qt)"));
    resize(900, 700);
    /* Nada puede inflar el minimo por encima de la pantalla: bajo WSLg el
     * compositor corta la conexion (Error 71). */
    setMinimumSize(320, 360);

    buildMenu();
    buildUi();

    m_timer = new QTimer(this);
    m_timer->setInterval(TVQ_REFRESH_MS);
    connect(m_timer, &QTimer::timeout, this, &TvqMainWindow::onTick);
    m_timer->start();

    m_bg = new QTimer(this);
    m_bg->setInterval(TVQ_BACKGROUND_MS);
    connect(m_bg, &QTimer::timeout, this, &TvqMainWindow::onBackground);
    m_bg->start();

    syncPhaseLabel();
    updateScrollRange();
}

TvqMainWindow::~TvqMainWindow()
{
    /* El canvas puede recibir un paint durante la destruccion: se le quita la
     * funcion antes de que el core se libere (main.cxx lo hace despues). */
    if (m_traces)
        m_traces->setPaintFn({});
    if (m_axis)
        m_axis->setPaintFn({});
}

void TvqMainWindow::buildMenu()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("Export view..."), this,
                    &TvqMainWindow::onExport);
    file->addAction(QStringLiteral("Quit"), qApp, &QCoreApplication::quit);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    m_dark = view->addAction(QStringLiteral("Dark mode"));
    m_dark->setCheckable(true);
    connect(m_dark, &QAction::toggled, this, &TvqMainWindow::onToggleDark);

    QMenu *disp = menuBar()->addMenu(QStringLiteral("&Display"));
    disp->addAction(QStringLiteral("Stations per screen..."), this,
                    &TvqMainWindow::onStationsPerScreen);
    disp->addAction(QStringLiteral("Time window..."), this,
                    &TvqMainWindow::onTimeWindow);
    disp->addAction(QStringLiteral("Clean view..."), this,
                    &TvqMainWindow::onCleanView);
    disp->addAction(QStringLiteral("Reload station list"), this,
                    &TvqMainWindow::onReloadStations);

    QMenu *filt = menuBar()->addMenu(QStringLiteral("F&ilter"));
    filt->addAction(QStringLiteral("Filter options..."), this,
                    &TvqMainWindow::onFilterOptions);

    QMenu *col = menuBar()->addMenu(QStringLiteral("&Colours"));
    for (int r = 0; r < TVQ_COLOUR_COUNT; r++) {
        QAction *a = col->addAction(QString::fromUtf8(kRoleName(r)));
        connect(a, &QAction::triggered, this, [this, r]() { onColour(r); });
    }

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Commands..."), this,
                    &TvqMainWindow::onCommands);
}

void TvqMainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QGridLayout *g = new QGridLayout(central);
    g->setContentsMargins(6, 6, 6, 6);
    g->setSpacing(4);

    QHBoxLayout *bar = new QHBoxLayout;
    m_hold = new QPushButton(QStringLiteral("HOLD (freeze screen)"), central);
    m_hold->setCheckable(true);
    connect(m_hold, &QPushButton::toggled, this, &TvqMainWindow::onHoldToggled);
    bar->addWidget(m_hold);

    m_phase = new QLabel(central);
    bar->addWidget(m_phase);
    bar->addStretch(1);

    QLabel *hint = new QLabel(
        QStringLiteral("P/S: arm pick | Esc: disarm | click: place | Up/Down: zoom"),
        central);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    hint->setFont(small);
    bar->addWidget(hint);

    g->addLayout(bar, 0, 0, 1, 2);

    m_traces = new ewguiqt::CanvasWidget(central);
    m_traces->setPaintFn([this](unsigned char *bgra, int w, int h, int stride) {
        tvq_paint_traces(m_core, bgra, w, h, stride,
                         m_traces->devicePixelRatioF(), m_scrollY);
    });
    connect(m_traces, &ewguiqt::CanvasWidget::clicked, this,
            [this](double x, double y) { onPlace(x, y); });
    g->addWidget(m_traces, 1, 0);

    m_scroll = new QScrollBar(Qt::Vertical, central);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_updating)
            return;
        m_scrollY = v;
        m_traces->requestRepaint();
    });
    g->addWidget(m_scroll, 1, 1);

    m_axis = new ewguiqt::CanvasWidget(central);
    m_axis->setFixedHeight(TVQ_BOTTOM_AXIS_H);
    m_axis->setMinSizeHint(200, TVQ_BOTTOM_AXIS_H);
    m_axis->setPaintFn([this](unsigned char *bgra, int w, int h, int stride) {
        tvq_paint_axis(m_core, bgra, w, h, stride, m_axis->devicePixelRatioF());
    });
    g->addWidget(m_axis, 2, 0);
    g->addWidget(new QWidget(central), 2, 1);

    g->setRowStretch(1, 1);
    g->setColumnStretch(0, 1);
    setCentralWidget(central);

    m_traces->setFocusPolicy(Qt::StrongFocus);
}

void TvqMainWindow::onTick()
{
    const unsigned ch = tvq_core_tick(m_core, tvq_now_epoch());

    if (ch & TVQ_CH_STATIONS)
        updateScrollRange();
    if (ch & (TVQ_CH_TRACES | TVQ_CH_PICKS | TVQ_CH_STATIONS))
        m_traces->requestRepaint();
    if (ch & (TVQ_CH_TIME | TVQ_CH_TRACES | TVQ_CH_STATIONS))
        m_axis->requestRepaint();
}

void TvqMainWindow::onBackground()
{
    const double now = tvq_now_epoch();

    if (!m_core)
        return;
    tvq_core_heartbeat(m_core, now);
    if (tvq_core_should_quit(m_core)) {
        tvq_logf("csntvpQT: senal de terminacion recibida\n");
        qApp->quit();
    }
}

void TvqMainWindow::updateScrollRange()
{
    if (!m_traces || !m_scroll || !m_core)
        return;

    const int vp = m_traces->height();
    const int total = tvq_core_canvas_height(m_core, vp);
    int maxv = total - vp;

    if (maxv < 0)
        maxv = 0;

    m_updating = true;
    m_scroll->setRange(0, maxv);
    m_scroll->setPageStep(vp > 0 ? vp : 100);
    m_scroll->setSingleStep(tvq_core_track_height(m_core, vp));
    if (m_scrollY > maxv)
        m_scrollY = maxv;
    m_scroll->setValue(m_scrollY);
    m_updating = false;
}

void TvqMainWindow::resizeEvent(QResizeEvent *ev)
{
    QMainWindow::resizeEvent(ev);
    updateScrollRange();
}

void TvqMainWindow::repaintAll()
{
    if (m_traces)
        m_traces->requestRepaint();
    if (m_axis)
        m_axis->requestRepaint();
}

void TvqMainWindow::syncPhaseLabel()
{
    if (!m_phase || !m_core)
        return;
    {
        const char ph = tvq_core_pick_phase(m_core);

        m_phase->setText(ph ? QStringLiteral("pick: %1").arg(QChar(ph))
                            : QStringLiteral("pick: off"));
    }
    if (m_hold)
        m_hold->setText(tvq_core_is_hold(m_core)
                            ? QStringLiteral("HOLD (active)")
                            : QStringLiteral("HOLD (freeze screen)"));
}

/* Circulo con la letra de la fase bajo el cursor (el "modo picada" de
 * csnhypodbpQT, ui/bpq_mainwindow.cxx:1925-1958). Sin fase armada se restaura
 * el puntero normal: el clic tampoco pica. */
void TvqMainWindow::updatePickCursor()
{
    if (!m_traces)
        return;

    const char ph = tvq_core_pick_phase(m_core);
    if (!ph) {
        m_traces->unsetCursor();
        return;
    }

    double rgb[3] = { 0.0, 0.0, 0.0 };
    tvq_core_get_colour(m_core, ph == 'S' ? TVQ_COLOUR_S : TVQ_COLOUR_P, rgb);

    QPixmap pm(26, 26);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QBrush(QColor::fromRgbF(rgb[0], rgb[1], rgb[2])));
    p.setPen(QPen(QColor(25, 25, 25), 1.5));
    p.drawEllipse(QRectF(3, 3, 20, 20));
    {
        QFont f = p.font();

        f.setBold(true);
        f.setPointSize(9);
        p.setFont(f);
    }
    p.setPen(Qt::white);
    p.drawText(QRectF(3, 3, 20, 20), Qt::AlignCenter, QString(QChar(ph)));
    p.end();

    m_traces->setCursor(QCursor(pm, 13, 13));
}

void TvqMainWindow::onHoldToggled(bool on)
{
    tvq_core_set_hold(m_core, on ? 1 : 0);
    syncPhaseLabel();
    updatePickCursor();   /* soltar HOLD desarma la fase en el core */
    repaintAll();
    if (m_traces)
        m_traces->setFocus();
}

void TvqMainWindow::onPlace(double x, double y)
{
    int row = -1;
    double t = 0.0;

    if (!m_traces || !m_core)
        return;
    if (!tvq_core_is_hold(m_core))
        return;
    if (!tvq_core_pick_phase(m_core))   /* sin fase armada el clic no pica */
        return;
    if (!tvq_core_hit(m_core, (int)x, (int)y, m_traces->width(),
                      m_traces->height(), m_scrollY, &row, &t))
        return;
    if (tvq_core_place_pick(m_core, row, t, tvq_core_pick_phase(m_core)))
        m_traces->requestRepaint();
}

void TvqMainWindow::onStationsPerScreen()
{
    bool ok = false;
    const int cur = m_cfg->stations_per_screen;
    const int v = QInputDialog::getInt(
        this, QStringLiteral("Stations per screen"),
        QStringLiteral("Stations visible per screen (1-512):"), cur, 1,
        TVQ_MAX_STATIONS, 1, &ok);

    if (!ok)
        return;
    tvq_core_set_stations_per_screen(m_core, v);
    updateScrollRange();
    repaintAll();
}

void TvqMainWindow::onTimeWindow()
{
    bool ok = false;
    const int cur = m_cfg->time_window_min;
    const int v = QInputDialog::getInt(
        this, QStringLiteral("Time window"),
        QStringLiteral("Window width (minutes, 1-120):"), cur, 1,
        TVQ_MAX_MINUTES, 1, &ok);

    if (!ok)
        return;
    tvq_core_set_time_window_min(m_core, v);
    repaintAll();
}

void TvqMainWindow::onCleanView()
{
    bool ok = false;
    const int v = QInputDialog::getInt(
        this, QStringLiteral("Clean view"),
        QStringLiteral("Max time without data (minutes):"), 60, 1, 10080, 10,
        &ok);

    if (!ok)
        return;
    const int removed = tvq_core_clean(m_core, (double)v * 60.0);
    tvq_logf("csntvpQT: clean view: %d estaciones fuera (max %d min)\n", removed,
             v);
    updateScrollRange();
    repaintAll();
}

void TvqMainWindow::onReloadStations()
{
    if (tvq_core_reload_stations(m_core) == 0) {
        m_scrollY = 0;
        updateScrollRange();
        repaintAll();
    }
}

void TvqMainWindow::onFilterOptions()
{
    int type = 0, order = 4;
    double f1 = 0.0, f2 = 0.0;

    tvq_core_get_filter(m_core, &type, &f1, &f2, &order);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Filter options"));
    QVBoxLayout *v = new QVBoxLayout(&dlg);

    QGroupBox *gbType = new QGroupBox(QStringLiteral("Type"), &dlg);
    QVBoxLayout *vt = new QVBoxLayout(gbType);
    QRadioButton *rRaw = new QRadioButton(QStringLiteral("Raw"), gbType);
    QRadioButton *rHp = new QRadioButton(QStringLiteral("High-Pass"), gbType);
    QRadioButton *rLp = new QRadioButton(QStringLiteral("Low-Pass"), gbType);
    QRadioButton *rBp = new QRadioButton(QStringLiteral("Band-Pass"), gbType);
    vt->addWidget(rRaw);
    vt->addWidget(rHp);
    vt->addWidget(rLp);
    vt->addWidget(rBp);
    (type == 1 ? rHp : type == 2 ? rLp : type == 3 ? rBp : rRaw)->setChecked(true);
    v->addWidget(gbType);

    QHBoxLayout *hf = new QHBoxLayout;
    QDoubleSpinBox *sbF1 = new QDoubleSpinBox(&dlg);
    QDoubleSpinBox *sbF2 = new QDoubleSpinBox(&dlg);
    sbF1->setLocale(QLocale::c());
    sbF2->setLocale(QLocale::c());
    sbF1->setRange(0.01, 100.0);
    sbF2->setRange(0.01, 100.0);
    sbF1->setSingleStep(0.1);
    sbF2->setSingleStep(0.1);
    sbF1->setValue(f1 > 0.0 ? f1 : 0.7);
    sbF2->setValue(f2 > 0.0 ? f2 : 2.0);
    hf->addWidget(new QLabel(QStringLiteral("F1 (Hz)"), &dlg));
    hf->addWidget(sbF1);
    hf->addWidget(new QLabel(QStringLiteral("F2 (Hz)"), &dlg));
    hf->addWidget(sbF2);
    v->addLayout(hf);

    QGroupBox *gbOrder = new QGroupBox(QStringLiteral("Order"), &dlg);
    QHBoxLayout *ho = new QHBoxLayout(gbOrder);
    QRadioButton *o2 = new QRadioButton(QStringLiteral("2"), gbOrder);
    QRadioButton *o4 = new QRadioButton(QStringLiteral("4"), gbOrder);
    ho->addWidget(o2);
    ho->addWidget(o4);
    (order == 2 ? o2 : o4)->setChecked(true);
    v->addWidget(gbOrder);

    QDialogButtonBox *bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const int nt = rHp->isChecked() ? 1 : rLp->isChecked() ? 2 : rBp->isChecked() ? 3 : 0;
    const int no = o2->isChecked() ? 2 : 4;
    tvq_core_set_filter(m_core, nt, sbF1->value(), sbF2->value(), no);
    repaintAll();
}

void TvqMainWindow::onColour(int role)
{
    double rgb[3];

    tvq_core_get_colour(m_core, role, rgb);
    const QColor cur = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    const QColor nc = QColorDialog::getColor(cur, this,
                                             QString::fromUtf8(kRoleName(role)));
    if (!nc.isValid())
        return;

    const double v[3] = { nc.redF(), nc.greenF(), nc.blueF() };
    tvq_core_set_colour(m_core, role, v);
    repaintAll();
    /* El GTK volcaba el bloque copiable al log: misma salida aqui. */
    if (!m_instrumented)
        tvq_core_print_colour_config(m_core, stdout);
}

void TvqMainWindow::onExport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export view"), QStringLiteral("csntvpqt.png"),
        QStringLiteral("Images (*.png)"));
    if (path.isEmpty())
        return;
    if (!grab().save(path))
        QMessageBox::warning(this, QStringLiteral("Export view"),
                             QStringLiteral("No pude escribir %1").arg(path));
}

void TvqMainWindow::onToggleDark(bool on)
{
    ewguiqt::set_dark_mode(*qApp, on);
    repaintAll();
}

void TvqMainWindow::onCommands()
{
    QMessageBox::information(
        this, QStringLiteral("Commands"),
        QStringLiteral(
            "HOLD: freezes the time window (data keeps filling the buffers)\n\n"
            "p / P: arm the P pick mode (cursor shows the phase)\n"
            "s / S: arm the S pick mode\n"
            "Esc: disarm the pick mode\n"
            "Up / Down: vertical zoom (only in HOLD)\n"
            "Left click on a trace: place the pick (HOLD + armed phase)\n\n"
            "Display: stations per screen, time window, clean view, reload\n"
            "Filter: raw / high-pass / low-pass / band-pass, order 2 or 4\n"
            "Colours: waveforms, background, font, separator, P/S picks,\n"
            "         and Z/N/E components"));
}

void TvqMainWindow::keyPressEvent(QKeyEvent *ev)
{
    const Qt::KeyboardModifiers mods = ev->modifiers();
    const bool plain = (mods == Qt::NoModifier || mods == Qt::ShiftModifier);

    if (plain) {
        switch (ev->key()) {
        case Qt::Key_P:
            if (tvq_core_is_hold(m_core)) {
                tvq_core_set_pick_phase(m_core, 'P');
                syncPhaseLabel();
                updatePickCursor();
                ev->accept();
                return;
            }
            break;
        case Qt::Key_S:
            if (tvq_core_is_hold(m_core)) {
                tvq_core_set_pick_phase(m_core, 'S');
                syncPhaseLabel();
                updatePickCursor();
                ev->accept();
                return;
            }
            break;
        case Qt::Key_Escape:
            /* Cancela el modo picada; con el modo ya apagado es no-op. */
            tvq_core_set_pick_phase(m_core, '\0');
            syncPhaseLabel();
            updatePickCursor();
            ev->accept();
            return;
        case Qt::Key_Up:
            if (tvq_core_is_hold(m_core)) {
                tvq_core_adjust_scale(m_core, 1.5);
                repaintAll();
                ev->accept();
                return;
            }
            break;
        case Qt::Key_Down:
            if (tvq_core_is_hold(m_core)) {
                tvq_core_adjust_scale(m_core, 1.0 / 1.5);
                repaintAll();
                ev->accept();
                return;
            }
            break;
        default:
            break;
        }
    }
    QMainWindow::keyPressEvent(ev);
}

QString TvqMainWindow::dumpTree() const
{
    QString out = QStringLiteral("window=%1\n").arg(windowTitle());

    if (m_core) {
        FILE *f = tmpfile();

        if (f) {
            char buf[1024];
            size_t n;

            tvq_core_dump_text(m_core, f);
            rewind(f);
            while ((n = fread(buf, 1, sizeof buf, f)) > 0)
                out += QString::fromUtf8(buf, (int)n);
            fclose(f);
        }
    }

    out += QStringLiteral("menu=%1\n")
               .arg(menuBar() ? menuBar()->actions().size() : 0);
    out += QStringLiteral("canvas=%1x%2\n")
               .arg(m_traces ? m_traces->width() : 0)
               .arg(m_traces ? m_traces->height() : 0);
    out += QStringLiteral("scroll=%1 max=%2\n")
               .arg(m_scrollY)
               .arg(m_scroll ? m_scroll->maximum() : 0);
    return out;
}

QString TvqMainWindow::dumpLayout() const
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
               .arg(m_traces ? m_traces->width() : 0)
               .arg(m_traces ? m_traces->height() : 0);
    out += QStringLiteral("axis=%1x%2\n")
               .arg(m_axis ? m_axis->width() : 0)
               .arg(m_axis ? m_axis->height() : 0);
    out += QStringLiteral("track_h=%1\n")
               .arg(m_core && m_traces
                        ? tvq_core_track_height(m_core, m_traces->height())
                        : 0);
    return out;
}

bool TvqMainWindow::saveShot(const QString &path)
{
    return grab().save(path);
}

QString TvqMainWindow::interactProbe()
{
    QString out;

    if (!m_core || !m_traces)
        return QStringLiteral("error=sin-canvas\n");

    resize(900, 700);
    show();
    QCoreApplication::processEvents();
    updateScrollRange();

    const int w = m_traces->width();
    const int h = m_traces->height();
    if (w <= TVQ_PANEL_WIDTH || h <= 0)
        return QStringLiteral("error=canvas-sin-tamano\n");

    /* 1) HOLD congela t_right a traves de un tick. */
    tvq_core_set_hold(m_core, 1);
    m_hold->setChecked(true);
    syncPhaseLabel();

    double l0 = 0.0, r0 = 0.0, l1 = 0.0, r1 = 0.0;

    (void)tvq_core_tick(m_core, tvq_now_epoch());
    tvq_core_view_range(m_core, &l0, &r0);
    (void)tvq_core_tick(m_core, tvq_now_epoch() + 0.5);
    tvq_core_view_range(m_core, &l1, &r1);
    out += QStringLiteral("hold_t_right=%1\n").arg(r0, 0, 'f', 3);
    out += QStringLiteral("hold_frozen=%1\n")
               .arg(std::fabs(r1 - r0) < 1e-9 ? 1 : 0);

    /* 2) clic -> (fila, tiempo): el tiempo esperado se calcula igual que el
     *    core (fraccion del ancho de trazas), y debe coincidir a <1 ms. */
    const int x = TVQ_PANEL_WIDTH + (w - TVQ_PANEL_WIDTH) / 2;
    const int y = h / 2;
    int row = -1;
    double t = 0.0;

    if (!tvq_core_hit(m_core, x, y, w, h, m_scrollY, &row, &t))
        return out + QStringLiteral("error=hit\n");

    const double expect =
        l0 + ((double)(x - TVQ_PANEL_WIDTH) / (double)(w - TVQ_PANEL_WIDTH)) *
                 ((double)m_cfg->time_window_min * 60.0);
    out += QStringLiteral("hit_row=%1\n").arg(row);
    out += QStringLiteral("hit_dt_ms=%1\n")
               .arg(std::fabs(t - expect) * 1000.0, 0, 'f', 3);

    /* 3) default: sin fase armada el clic no pica (guard de onPlace). */
    out += QStringLiteral("pick_default=%1\n")
               .arg(tvq_core_pick_phase(m_core)
                        ? QString(QChar(tvq_core_pick_phase(m_core)))
                        : QStringLiteral("none"));
    {
        char before[TVQ_PICK_MSG_MAX] = {0};
        char after[TVQ_PICK_MSG_MAX] = {0};

        (void)tvq_core_last_pick(m_core, before, sizeof before);
        onPlace((double)x, (double)y);
        (void)tvq_core_last_pick(m_core, after, sizeof after);
        out += QStringLiteral("click_without_phase_ignored=%1\n")
                   .arg(std::strcmp(before, after) == 0 ? 1 : 0);
    }
    out += QStringLiteral("pick_without_phase=%1\n")
               .arg(tvq_core_place_pick(m_core, row, t, '\0'));

    /* 4) sin HOLD no se pica. */
    tvq_core_set_hold(m_core, 0);
    m_hold->setChecked(false);
    out += QStringLiteral("pick_without_hold=%1\n")
               .arg(tvq_core_place_pick(m_core, row, t, 'P'));
    tvq_core_set_hold(m_core, 1);
    m_hold->setChecked(true);

    /* 5) con HOLD y fase armada si pica; el mensaje lleva el mod del .d y la
     *    fase, y el modo sigue armado (persistente hasta Esc/HOLD). */
    tvq_core_set_pick_phase(m_core, 'P');
    syncPhaseLabel();
    updatePickCursor();
    out += QStringLiteral("arm_p=%1\n")
               .arg(tvq_core_pick_phase(m_core) == 'P' ? 1 : 0);
    out += QStringLiteral("cursor_armed=%1\n")
               .arg(m_traces->cursor().shape() == Qt::BitmapCursor ? 1 : 0);

    const int placed = tvq_core_place_pick(m_core, row, t,
                                           tvq_core_pick_phase(m_core));
    char msg[TVQ_PICK_MSG_MAX] = {0};

    (void)tvq_core_last_pick(m_core, msg, sizeof msg);
    out += QStringLiteral("pick_placed=%1\n").arg(placed);
    out += QStringLiteral("pick_msg=%1").arg(QString::fromUtf8(msg));
    out += QStringLiteral("pick_still_armed=%1\n")
               .arg(tvq_core_pick_phase(m_core) == 'P' ? 1 : 0);

    /* 6) Esc desarma. */
    tvq_core_set_pick_phase(m_core, '\0');
    syncPhaseLabel();
    updatePickCursor();
    out += QStringLiteral("esc_clears=%1\n")
               .arg(tvq_core_pick_phase(m_core) == '\0' ? 1 : 0);
    out += QStringLiteral("cursor_off=%1\n")
               .arg(m_traces->cursor().shape() == Qt::ArrowCursor ? 1 : 0);

    /* 7) soltar HOLD tambien desarma (fuera de HOLD no se pica). */
    tvq_core_set_pick_phase(m_core, 'P');
    tvq_core_set_hold(m_core, 0);
    m_hold->setChecked(false);
    out += QStringLiteral("disarm_on_release=%1\n")
               .arg(tvq_core_pick_phase(m_core) == '\0' ? 1 : 0);
    tvq_core_set_hold(m_core, 1);
    m_hold->setChecked(true);

    /* 8) fase por teclado simulado (P/S solo en HOLD). */
    tvq_core_set_pick_phase(m_core, 'S');
    syncPhaseLabel();
    out += QStringLiteral("phase=%1\n").arg(QChar(tvq_core_pick_phase(m_core)));

    /* 9) zoom vertical con flechas. */
    const double z0 = tvq_core_zoom(m_core);
    tvq_core_adjust_scale(m_core, 1.5);
    const double z1 = tvq_core_zoom(m_core);
    tvq_core_adjust_scale(m_core, 1.0 / 1.5);
    out += QStringLiteral("zoom=%1->%2\n").arg(z0, 0, 'f', 3).arg(z1, 0, 'f', 3);

    /* 7) clean-view + reload station list. Se deja pasar el tiempo de
     *    retencion (20 ms) para que el umbral sea observable sin depender del
     *    reloj de pared. */
    QThread::msleep(20);
    const int removed = tvq_core_clean(m_core, 0.005);
    out += QStringLiteral("clean_removed=%1\n").arg(removed);
    out += QStringLiteral("n_sta_after_clean=%1\n").arg(tvq_core_nstations(m_core));
    const int reloaded = tvq_core_reload_stations(m_core);
    out += QStringLiteral("reload_ok=%1\n").arg(reloaded == 0 ? 1 : 0);
    out += QStringLiteral("n_sta_after_reload=%1\n").arg(tvq_core_nstations(m_core));

    return out;
}
