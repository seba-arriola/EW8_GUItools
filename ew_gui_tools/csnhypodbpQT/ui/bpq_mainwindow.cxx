/*
 * bpq_mainwindow.cxx - Ver bpq_mainwindow.h.
 *
 * Notas de port (lecciones del plan, §5 y §14):
 *   - los divisores (QSplitter) se reparten UNA sola vez, sin shrink=false: el
 *     minimo de la ventana no debe crecer por encima de la pantalla (WSLg).
 *   - las tablas llevan sus cabeceras LITERALES (AC12) y anchos acotados.
 *   - el menu es nativo (QMenu con parent): no hay superficie xdg_popup.
 */
#include "bpq_mainwindow.h"

#include "bpq_canvas_widget.h"
#include "bpq_events_model.h"
#include "bpq_filters_dialog.h"
#include "bpq_locate_worker.h"
#include "bpq_mag_model.h"
#include "bpq_mww_panel.h"
#include "bpq_preview_panel.h"
#include "bpq_ring_worker.h"
#include "bpq_search_dialog.h"
#include "bpq_stations_model.h"
#include "bpq_wave_worker.h"

#include <QAction>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCoreApplication>
#include <QEventLoop>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QMenuBar>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <cstdio>
#include <cstdlib>

/* Identidad de una solucion para el pin y la candidata (misma que el modelo). */
static QString solKey(const CsnhypodbpSolution *s)
{
    char buf[BPQ_PIN_KEY_MAX];

    if (!s || bpq_origin_key(s->qid, s->mod, s->version, buf, sizeof(buf)) != 0)
        return QString();
    return QString::fromLatin1(buf);
}

/* Cabeceras de las tablas que aun no tienen modelo propio (fases 3/6). Las de
 * Eventos/Origins viven en la capa C (bpq_event_header/bpq_origin_header), que es
 * donde esta el contrato de paridad con la version GTK. */
static const char *const kStaHeaders[5]  = { "station", "dist[km]", "pick", "stat", "res" };
static const char *const kMagHeaders[8]  = { "station", "dist[km]", "ancla", "t0[s]", "t1[s]",
                                             "mag", "res", "stat" };

static QTableView *make_view(const QString &cls)
{
    QTableView *v = new QTableView;
    v->setObjectName(cls);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    /* El orden lo lleva siempre un SortProxy + wire_header_sort: el orden
     * propio de Qt no conoce el comparador compartido ni la direccion del
     * primer clic, y con el activado los modelos sin sort() no hacian nada. */
    v->setSortingEnabled(false);
    v->setWordWrap(false);
    v->setTextElideMode(Qt::ElideRight);
    v->setShowGrid(true);
    v->setAlternatingRowColors(false);
    v->horizontalHeader()->setSectionsClickable(true);
    v->horizontalHeader()->setStretchLastSection(true);
    /* Anchos acotados: una tabla que pida mucho ancho infla el minimo de la
     * ventana (trampa T6 de WSLg). */
    v->setMinimumWidth(180);
    return v;
}

static QTableView *make_table(const char *const *headers, int n, const QString &cls)
{
    QTableView *v = make_view(cls);
    QStandardItemModel *m = new QStandardItemModel(0, n, v);
    for (int i = 0; i < n; i++) m->setHeaderData(i, Qt::Horizontal, QString::fromUtf8(headers[i]));
    v->setModel(m);
    return v;
}

BpqMainWindow::BpqMainWindow(const BpqConfig *cfg, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg)
{
    setWindowTitle(QStringLiteral("CSNhypodbpQT - Hypocenter database picker (EW8/Qt)"));
    resize(1100, 750);
    /* FilterPhase del .d: el dialogo Filters… lo puede cambiar en memoria. */
    m_phaseZero = cfg && cfg->filtercfg.phase_zero != 0;
    buildUi();
    /* Ajuste inicial (cabeceras): el panel nace con el ancho que necesita la
     * tabla, no con columnas de 100 px que obligan a scroll horizontal. */
    fitProcesarTable(m_staView, m_splitPicking);
    fitProcesarTable(m_magView, m_splitMag);
    /* Repeticion diferida: al construir, el splitter aun no tiene su tamano
     * definitivo y los anchos se calculaban con el viewport inicial. */
    QTimer::singleShot(0, this, [this]() {
        fitProcesarTable(m_staView, m_splitPicking);
        fitProcesarTable(m_magView, m_splitMag);
    });
    /* Las teclas del picking se resuelven en la VENTANA, no en el widget con
     * foco: la tabla de estaciones no debe hacer type-ahead ni robar P/S. */
    qApp->installEventFilter(this);
    buildMenuBar();
    openData();
}

BpqMainWindow::~BpqMainWindow()
{
    /* El filtro de aplicacion se retira ANTES de soltar los widgets: si no, un
     * evento tardio durante el cierre llegaria a una ventana a medio destruir. */
    if (QCoreApplication *app = QCoreApplication::instance())
        app->removeEventFilter(this);
    if (m_worker) { m_worker->stop(); delete m_worker; m_worker = nullptr; }
    if (m_rings)  { bpq_rings_close(m_rings); m_rings = nullptr; }
    if (m_events) { bpq_events_free(m_events); m_events = nullptr; }
    /* El hilo de ondas tiene que parar ANTES de cerrar el store. */
    if (m_waveThread) {
        m_waveThread->quit();
        m_waveThread->wait();
        m_waveWorker = nullptr;      /* el worker se borra en QThread::finished */
        delete m_waveThread;
        m_waveThread = nullptr;
    }
    /* Si hay un locate en curso se pide aborto: la cadena lo ve en el proximo
     * paso y el hilo termina sin dejar el refinador huerfano. */
    if (m_locThread) {
        if (m_locWorker) m_locWorker->requestCancel();
        m_locThread->quit();
        m_locThread->wait();
        m_locWorker = nullptr;
        delete m_locThread;
        m_locThread = nullptr;
    }
    /* El hilo del Mww tambien es hijo de la ventana: si sigue vivo, el
     * ~QThread aborta al destruir la ventana (y se pierde el --dump). */
    if (m_mwwThread) {
        m_mwwThread->quit();
        m_mwwThread->wait();
        m_mwwWorker = nullptr;
        delete m_mwwThread;
        m_mwwThread = nullptr;
    }
    for (BpqTrace *t : m_traces) bpq_trace_free(t);
    m_traces.clear();
    for (BpqStaLta *s : m_stalta) bpq_stalta_free(s);
    m_stalta.clear();
    if (m_profData) { bpq_profile_free(m_profData); m_profData = nullptr; }
    if (m_wave) { bpq_wave_close(m_wave); m_wave = nullptr; }
    if (m_staCat) { bpq_stations_close(m_staCat); m_staCat = nullptr; }
    if (m_db) { bpq_db_close(m_db); m_db = nullptr; }
    if (m_map) { bpq_map_free(m_map); m_map = nullptr; }
}

/* Anillos + store + modelos. Si EarthWorm no esta disponible la ventana abre
 * igual (solo sin datos): un fallo de anillos no debe impedir mirar el GUI. */
void BpqMainWindow::openData()
{
    char err[512] = "";

    m_events = bpq_events_new(m_cfg);
    m_eventsModel = new BpqEventsModel(this);
    m_eventsModel->setStore(m_events);
    m_eventsProxy = new BpqSortProxy(this);
    m_eventsProxy->setSourceModel(m_eventsModel);
    m_eventsView->setModel(m_eventsProxy);
    /* Tabla maestra: primer clic DESC en fecha/numericas y la flecha del header
     * sigue el orden real (misma semantica que el helper GTK). El orden inicial
     * es el de su primer clic: fecha DESC = eventos recientes primero. */
    ewguiqt::wire_header_sort(m_eventsView, m_eventsProxy, 0);

    m_originsModel = new BpqOriginsModel(this);
    m_originsProxy = new ewguiqt::SortProxy(this);
    m_originsProxy->setSourceModel(m_originsModel);
    {
        /* Reparto de la GTK: Mod/Ver/Est./Created/Date + numericas. */
        QVector<ewguiqt::SortKind> kinds(15, ewguiqt::SortKind::Text);

        kinds[0] = ewguiqt::SortKind::Number;   /* Mod */
        kinds[1] = ewguiqt::SortKind::Number;   /* Ver */
        kinds[3] = ewguiqt::SortKind::Date;     /* Created */
        kinds[4] = ewguiqt::SortKind::Date;     /* Date (otime) */
        for (int c = 5; c <= 14; ++c)
            kinds[c] = ewguiqt::SortKind::Number;
        m_originsProxy->setColumnKinds(kinds, 4);
        m_originsProxy->setCellNumberFn(
            [](const QAbstractItemModel &m, int r, int c) {
                const auto *om = qobject_cast<const BpqOriginsModel *>(&m);
                return om ? om->sortValue(r, c) : std::nan("");
            });
        m_originsProxy->setDynamicSortFilter(false);
    }
    m_originsView->setModel(m_originsProxy);
    /* Created DESC: el detalle arranca como la GTK. */
    ewguiqt::wire_header_sort(m_originsView, m_originsProxy, 3);

    /* Columna derecha: el mapa base lo decodifica Qt (QImageReader) y lo pinta
     * cairo; los overlays .lines los carga la capa C desde el .d. El limite de
     * asignacion del proceso lo fija main.cxx antes de existir la ventana. */
    if (m_cfg->map_image_file[0]) {
        QImageReader rd(QString::fromUtf8(m_cfg->map_image_file));
        const QImage img = rd.read();
        if (!img.isNull()) {
            m_mapImage = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        } else {
            /* Un fallo de carga es invisible en pantalla (la barra de estado se
             * tapa con el siguiente mensaje): que quede en stderr con el motivo
             * real (formato no soportado, allocation limit, archivo ausente). */
            std::fprintf(stderr, "csnhypodbpQT: no se pudo cargar el mapa <%s>: %s\n",
                         m_cfg->map_image_file, qPrintable(rd.errorString()));
            statusBar()->showMessage(
                QStringLiteral("MapImageFile no se pudo cargar (%1): mapa virtual")
                    .arg(QString::fromUtf8(m_cfg->map_image_file)), 0);
        }
    }
    m_map = bpq_map_new(m_cfg);
    m_mapPlan->installEventFilter(this);
    m_mapProf->installEventFilter(this);

    /* Picking: con una fase armada (tecla P/S, el cursor es un circulo de color)
     * el clic coloca la picada en el overlay; sin fase armada, dos clics hacen
     * zoom temporal. */
    if (m_waves) {
        m_waves->installEventFilter(this);
        connect(m_waves, &BpqCanvasWidget::clicked, this,
                [this](double x, double y) {
            if (m_pickArmed) {
                const double d = m_waves->devicePixelRatioF();
                int row = -1;
                double t = 0.0;

                if (bpq_traces_hit(&m_tracesCtx, int(m_waves->width() * d),
                                   int(m_waves->height() * d), x * d, y * d,
                                   &row, &t) == 0)
                    emitManualPick(row, t);
                return;
            }
            zoomClickAt(x, y);
        });
        connect(m_waves, &BpqCanvasWidget::rightClicked, this,
                [this](double x, double y) {
            const double d = m_waves->devicePixelRatioF();
            int row = -1;
            double t = 0.0;
            const char *sta = nullptr, *net = nullptr, *chan = nullptr, *loc = nullptr;

            if (!m_staCat || !m_pickArmed) return;
            if (bpq_traces_hit(&m_tracesCtx, int(m_waves->width() * d),
                               int(m_waves->height() * d), x * d, y * d,
                               &row, &t) != 0) return;
            if (row < 0 || row >= m_traceRows.size()) return;
            if (!bpq_stations_scnl(m_staCat, m_traceRows.at(row).idx, &sta, &net,
                                   &chan, &loc)) return;
            if (bpq_manual_remove(sta, m_pickPhase) != 1) {
                statusBar()->showMessage(
                    QStringLiteral("no hay picada %1 manual en %2")
                        .arg(QLatin1Char(m_pickPhase)).arg(QString::fromUtf8(sta)),
                    3000);
                return;
            }
            refreshAfterManual();
            statusBar()->showMessage(
                QStringLiteral("picada %1 de %2 eliminada")
                    .arg(QLatin1Char(m_pickPhase)).arg(QString::fromUtf8(sta)),
                3000);
        });
    }

    /* Capa de ondas: store indexado + SDS de respaldo (fase 3). */
    {
        char werr[256] = "";
        m_wave = bpq_wave_open(m_cfg, werr, sizeof(werr));
        if (werr[0])
            statusBar()->showMessage(QStringLiteral("ondas: %1").arg(QString::fromUtf8(werr)), 0);
    }
    /* El fetch corre en su propio hilo: el store/SDS puede tardar y el picking
     * (fase 4) repica mientras se descargan las trazas. */
    m_waveThread = new QThread(this);
    m_waveWorker = new BpqWaveWorker(m_wave, m_cfg);
    m_waveWorker->moveToThread(m_waveThread);
    connect(m_waveThread, &QThread::finished, m_waveWorker, &QObject::deleteLater);
    connect(m_waveWorker, &BpqWaveWorker::traceReady, this,
            [this](quint64 job, int row, BpqTrace *tr) {
        /* Resultado de un pedido que el usuario ya dejo atras: se tira. */
        if (job != m_waveJob || row < 0 || row >= m_traces.size()) {
            bpq_trace_free(tr);
            return;
        }
        bpq_trace_free(m_traces[row]);
        m_traces[row] = tr;
        if (tr) {
            m_waveWith++;
            filterOne(tr);        /* la vista filtra al llegar, no al dibujar */
        }
        if (m_waves) m_waves->requestRepaint();
        if (m_magDragGrab == 0) renderMagnitudes();   /* bandas de la estacion actual */
    });
    connect(m_waveWorker, &BpqWaveWorker::jobFinished, this,
            [this](quint64 job, int requested, int withData) {
        if (job != m_waveJob) return;
        m_waveDone = job;
        rebuildStalta();          /* ya estan todas las trazas del pedido */
        statusBar()->showMessage(QStringLiteral("ondas: %1/%2 estaciones")
                                     .arg(withData).arg(requested), 5000);
    });
    m_waveThread->start();

    /* Catalogo de estaciones del .d (StaFile), una sola lectura. */
    {
        char serr[256] = "";
        m_staCat = bpq_stations_open(m_cfg, serr, sizeof(serr));
        if (serr[0])
            statusBar()->showMessage(QStringLiteral("estaciones: %1").arg(QString::fromUtf8(serr)), 0);
    }

    /* Overlay de picadas manuales: hermano del ManualStateFile (el `.picks` es
     * formato propio; el `.events` que usa la GTK no se toca). Se carga cuando
     * se selecciona el evento, porque las picadas son de UN evento. */
    m_manualPath = QString::fromUtf8(m_cfg->manual_state_file) + QStringLiteral(".picks");

    /* `locate`: csnloc + refinador corren en su hilo (segundos). El worker no
     * toca el catalogo mas que en lectura (vive lo que la ventana). */
    m_locThread = new QThread(this);
    m_locWorker = new BpqLocateWorker(m_wave, m_cfg, m_staCat);
    m_locWorker->moveToThread(m_locThread);
    connect(m_locThread, &QThread::finished, m_locWorker, &QObject::deleteLater);
    connect(m_locWorker, &BpqLocateWorker::finished, this,
            &BpqMainWindow::locateFinished);
    connect(m_locWorker, &BpqLocateWorker::magFinished, this,
            &BpqMainWindow::magFinished);

    /* Sub-pestana magnitudes: el estado de ventanas (magmarks) nace con el .d ya
     * leido, porque sus ventanas por defecto salen del .d de csnmags. */
    m_magPanel = bpq_magpanel_new(m_cfg);
    if (m_magPanel) {
        bpq_magpanel_set_kind(m_magPanel, m_magKind);
        renderMagnitudes();
    }

    /* Panel Mww: la config de csnwphase se abre una vez (MwwConfigFile) y el
     * motor corre en su propio hilo (tarda minutos: la GUI no se congela). */
    {
        char merr[512] = "";

        m_mww = bpq_mww_new(m_cfg, merr, sizeof(merr));
        if (m_mww) {
            m_mwwThread = new QThread(this);
            m_mwwWorker = new BpqMwwWorker;
            m_mwwWorker->moveToThread(m_mwwThread);
            connect(m_mwwThread, &QThread::finished, m_mwwWorker,
                    &QObject::deleteLater);
            connect(m_mwwWorker, &BpqMwwWorker::finished, this,
                    &BpqMainWindow::mwwFinished);
            m_mwwThread->start();
        }
        if (m_mwwPanel) {
            m_mwwPanel->setMww(m_mww);
            if (!m_mww)
                m_mwwPanel->setStatus(
                    QStringLiteral("Mww: %1").arg(QString::fromUtf8(merr)), true);
        }
    }
    m_locThread->start();
    m_locTick = new QTimer(this);
    m_locTick->setInterval(1000);
    connect(m_locTick, &QTimer::timeout, this, &BpqMainWindow::tickLocate);

    /* La fila elegida en la tabla decide la ESTACION ACTUAL: el canvas muestra
     * sus componentes (en el orden del .d), no una fila por estacion. */
    connect(m_staView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
        const QModelIndex src = m_staProxy ? m_staProxy->mapToSource(cur) : cur;
        const int i = m_staModel ? m_staModel->staIndexAt(src.row()) : -1;

        if (i < 0 || i >= m_staRows.size()) return;
        selectStation(QString::fromUtf8(m_staRows.at(i).sta));
    });

    /* La tabla de magnitudes elige la MISMA estacion actual: canvas de mags,
     * picking y tabla de estaciones quedan sincronizados. */
    connect(m_magView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
        const int src = sourceRowForView(m_magProxy, cur);
        const CsnhypodbpMagRow *r = m_magModel ? m_magModel->row(src) : nullptr;

        if (r) selectStation(QString::fromUtf8(r->sta));
    });

    /* La seleccion de la maestra elige el evento del detalle. La identidad del
     * evento es ESTABLE (eventKeyAt: qid + 1.ª otime), no la preferida: si una
     * confirmacion cambia la preferida, el evento y el pin no se pierden. */
    connect(m_eventsView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
        /* Reset transitorio del modelo (beginResetModel): la seleccion se
         * restaura despues; no hay que soltar el pin ni limpiar claves. */
        if (!cur.isValid()) return;
        const QModelIndex src = m_eventsProxy->mapToSource(cur);
        const int srcRow = src.row();
        const BpqEvent *ev = m_eventsModel->eventAt(srcRow);
        if (!ev) return;
        m_selKey = m_eventsModel->keyAt(srcRow);
        m_selEventKey = m_eventsModel->eventKeyAt(srcRow);

        /* El pin no cruza de evento: si cambia el evento mostrado se suelta. */
        if (ev != m_pinEvent) {
            bpq_pin_clear(&m_pin);
            m_pinEvent = nullptr;
        }

        m_syncOrigins = true;
        m_originsModel->setEvent(ev);
        reapplySort(m_originsProxy);
        const QString keep = bpq_pin_active(&m_pin)
                                 ? QString::fromLatin1(bpq_pin_key(&m_pin))
                                 : QString();
        buildPoints(ev, keep);
        int row = -1;
        if (bpq_pin_active(&m_pin)) {
            row = m_originsModel->rowForKey(keep);
            if (row < 0) {          /* la solucion fijada desaparecio */
                bpq_pin_clear(&m_pin);
                m_pinEvent = nullptr;
            }
        }
        if (row < 0) row = m_originsModel->rowForPreferred();
        if (row >= 0) {
            const int vrow = viewRowForSource(m_originsProxy, row);

            if (vrow >= 0) m_originsView->selectRow(vrow);
        }
        m_syncOrigins = false;

        refreshPanels();
        refreshStations(ev);
    });

    /* La fila elegida en Origins es el marcador verde del mapa y del perfil.
     * Con un origen fijado, el clic simple NO mueve el pin ni Procesar: la
     * seleccion vuelve a la fila fijada (solo el doble clic fija otro). */
    connect(m_originsView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
        if (!cur.isValid()) return;
        const int vrow = sourceRowForView(m_originsProxy, cur);
        const int sol = m_originsModel->solIndexAt(vrow);
        if (sol < 0) return;

        if (bpq_pin_active(&m_pin)) {
            const int prow = m_originsModel->rowForKey(
                QString::fromLatin1(bpq_pin_key(&m_pin)));
            if (prow >= 0 && prow != vrow) {
                const int pview = viewRowForSource(m_originsProxy, prow);

                QSignalBlocker b(m_originsView->selectionModel());
                if (pview >= 0) m_originsView->selectRow(pview);
                return;
            }
        }

        int idx = -1;
        for (int i = 0; i < m_npts; i++)
            if (m_ptSol[i] == sol) { idx = i; break; }
        if (idx < 0) return;
        m_mapCtx.selected = idx;
        m_profCtx.selected = idx;
        m_mapPlan->requestRepaint();
        m_mapProf->requestRepaint();
        if (m_syncOrigins) return;      /* el rebuild ya refresca una sola vez */
        refreshStations(m_originsModel->currentEvent());
    });

    /* Doble clic: fija el origen y lleva a Procesar -> picking. */
    connect(m_originsView, &QTableView::doubleClicked,
            this, &BpqMainWindow::onOriginDoubleClicked);

    /* Zoom, paneo y clic: el estado del encuadre vive en los contextos (C puro),
     * el widget solo entrega pixeles logicos. */
    connect(m_mapPlan, &BpqCanvasWidget::dragged, this,
            [this](double dx, double dy) {
        const double d = m_mapPlan->devicePixelRatioF();
        m_mapUserView = true;
        bpq_map_pan(&m_mapCtx, dx * d, dy * d);
        m_mapPlan->requestRepaint();
    });
    connect(m_mapPlan, &BpqCanvasWidget::zoomed, this,
            [this](double factor, double x, double y) {
        const double d = m_mapPlan->devicePixelRatioF();
        m_mapUserView = true;
        bpq_map_zoom_at(&m_mapCtx, int(m_mapPlan->width() * d),
                        int(m_mapPlan->height() * d), x * d, y * d, factor);
        m_mapPlan->requestRepaint();
    });
    connect(m_mapPlan, &BpqCanvasWidget::clicked, this,
            [this](double x, double y) {
        const double d = m_mapPlan->devicePixelRatioF();
        const int idx = bpq_map_hit_test(&m_mapCtx, int(m_mapPlan->width() * d),
                                         int(m_mapPlan->height() * d),
                                         x * d, y * d, 10.0 * d);
        if (idx < 0 || idx >= m_npts) return;
        const int row = m_originsModel->rowForSolution(m_ptSol[idx]);
        if (row < 0) return;
        /* Sin pin, un clic elige (como siempre). Con pin, el marcador es una
         * eleccion explicita: re-fija ese origen (sin saltar de pestana). */
        if (bpq_pin_active(&m_pin)) {
            pinOrigin(row, false, false);
        } else {
            const int vrow = viewRowForSource(m_originsProxy, row);

            if (vrow >= 0) m_originsView->selectRow(vrow);  /* pinta el verde */
        }
    });
    connect(m_mapProf, &BpqCanvasWidget::dragged, this,
            [this](double dx, double dy) {
        const double d = m_mapProf->devicePixelRatioF();
        m_profUserView = true;
        bpq_profile_pan(&m_profCtx, int(m_mapProf->width() * d),
                        int(m_mapProf->height() * d), dx * d, dy * d);
        m_mapProf->requestRepaint();
    });
    connect(m_mapProf, &BpqCanvasWidget::zoomed, this,
            [this](double factor, double x, double y) {
        const double d = m_mapProf->devicePixelRatioF();
        m_profUserView = true;
        bpq_profile_zoom_at(&m_profCtx, int(m_mapProf->width() * d),
                            int(m_mapProf->height() * d), x * d, y * d, factor);
        m_mapProf->requestRepaint();
    });

    /* Trazas: rueda = zoom temporal alrededor del cursor, arrastre = paneo,
     * dos clics = zoom al intervalo (el inverso deshace) y arrastre en el eje =
     * zoom in de izquierda a derecha / out al reves. La ventana vive en
     * m_winT0/m_winLen (los DOS canvas la comparten). */
    if (m_waves) {
        connect(m_waves, &BpqCanvasWidget::zoomed, this,
                [this](double factor, double x, double) {
            const double d = m_waves->devicePixelRatioF();
            bpq_traces_zoom_at(&m_tracesCtx, int(m_waves->width() * d), x * d,
                               factor);
            applyTracesWindow();
        });
        connect(m_waves, &BpqCanvasWidget::dragged, this,
                [this](double dx, double) {
            const double d = m_waves->devicePixelRatioF();
            bpq_traces_pan(&m_tracesCtx, int(m_waves->width() * d), dx * d);
            applyTracesWindow();
        });
    }
    if (m_axis) {
        const auto axisZoom = [this](double dx) {
            const double d = m_axis->devicePixelRatioF();
            const int wpx = int(m_axis->width() * d);
            /* A la derecha acerca, a la izquierda aleja (factor exponencial:
             * 100 px ~ x2.7). El ancla es el centro del eje. */
            const double factor = std::exp(dx * 0.01);

            bpq_traces_zoom_at(&m_tracesCtx, wpx, wpx * 0.5, factor);
            applyTracesWindow();
        };

        connect(m_axis, &BpqCanvasWidget::dragged, this,
                [axisZoom](double dx, double) { axisZoom(dx); });
        connect(m_axis, &BpqCanvasWidget::zoomed, this,
                [this](double factor, double x, double) {
            const double d = m_axis->devicePixelRatioF();
            bpq_traces_zoom_at(&m_tracesCtx, int(m_axis->width() * d), x * d,
                               factor);
            applyTracesWindow();
        });
    }

    /* Fixture determinista (fase 7): sin anillos ni BD. Los eventos los pone
     * BPQ_TEST_FIXTURE; asi el dump no depende de lo que haya en produccion. */
    const bool test_mode = !qgetenv("BPQ_TEST_FIXTURE").isEmpty();

    m_rings = test_mode ? nullptr : bpq_rings_open(m_cfg, err, sizeof(err));
    loadTestArc();          /* BPQ_TEST_ARC: datos sin EarthWorm (tests/demos) */

    if (m_rings) {
        m_worker = new BpqRingWorker(m_rings, m_events, this);
        connect(m_worker, &BpqRingWorker::eventsChanged,
                this, &BpqMainWindow::refreshFromStore);
        connect(m_worker, &BpqRingWorker::quitRequested,
                this, []() { QCoreApplication::quit(); });
        connect(m_worker, &BpqRingWorker::errorOccurred, this,
                [this](const QString &msg) { statusBar()->showMessage(msg, 10000); });
        m_worker->start();
        m_status->setText(m_status->text() + QStringLiteral("  |  anillos OK"));
    } else {
        m_status->setText(m_status->text()
                          + QStringLiteral("  |  SIN ANILLOS: %1").arg(QString::fromUtf8(err)));
    }
    refreshFromStore();
    refreshPanels();

    /* Base hipocentral: historial de las ultimas `DbHistoryHours` horas. Se
     * carga antes de refrescar para que la maestra arranque con pasado + vivo. */
    if (!test_mode) {
        char derr[512] = "";
        const int hist = (m_cfg->db_history_hours > 0) ? m_cfg->db_history_hours : 24;

        m_db = bpq_db_open(m_cfg, derr, sizeof(derr));
        if (m_db && bpq_db_available(m_db)) {
            const int n = bpq_db_load_recent(m_db, m_cfg, m_events, hist, derr,
                                             sizeof(derr));
            refreshFromStore();
            m_status->setText(m_status->text()
                              + QStringLiteral("  |  historial %1 eventos/%2h")
                                    .arg(n).arg(hist));
        } else {
            m_status->setText(m_status->text()
                              + QStringLiteral("  |  SIN BD: %1")
                                    .arg(QString::fromUtf8(derr)));
        }
    }

    /* BPQ_TAB=picking|procesar|magnitudes: deja esa pestana activa (shots y
     * pruebas manuales sin clics). */
    {
        const QByteArray tab = qgetenv("BPQ_TAB").toLower();
        if (tab == QByteArray("picking") || tab == QByteArray("procesar"))
            m_tabs->setCurrentIndex(1);
        else if (tab == QByteArray("magnitudes")) {
            m_tabs->setCurrentIndex(1);
            m_subtabs->setCurrentIndex(1);
        }
    }
}

/* Soluciones del evento -> puntos del mapa (kind 0 = resto, 1 = preferida).
 * La candidata del locate sera kind 2 en la fase 5. */
void BpqMainWindow::buildPoints(const BpqEvent *ev, const QString &keepKey)
{
    m_npts = 0;
    m_pref = -1;
    /* Evento nuevo: se vuelve al auto-encuadre y al perfil sin zoom. */
    m_mapUserView = false;
    m_profUserView = false;
    if (!ev) {
        m_mapCtx.selected = -1;
        return;
    }
    m_pref = bpq_event_preferred(ev);
    for (int j = 0; j < bpq_event_nsols(ev) && m_npts < 64; j++) {
        const CsnhypodbpSolution *s = bpq_event_sol(ev, j);
        if (!s) continue;
        m_pts[m_npts].lat   = s->lat;
        m_pts[m_npts].lon   = s->lon;
        m_pts[m_npts].depth = s->depth;
        m_pts[m_npts].mag   = 0.0;
        m_pts[m_npts].kind  = (j == m_pref) ? 1 : 0;
        m_ptSol[m_npts]     = j;
        m_npts++;
    }
    /* Sin pin, arranca marcando la preferida; con pin, la solucion fijada
     * (que puede no ser la preferida). El clic simple no mueve el verde si
     * hay pin; el doble clic re-fija. */
    {
        const int keep = keepKey.isEmpty() ? -1 : solutionIndexForKey(ev, keepKey);

        m_mapCtx.selected = (keep >= 0) ? keep
                            : ((m_pref >= 0 && m_pref < m_npts) ? m_pref : -1);
    }

    if (m_pref >= 0 && m_pref < m_npts)
        selectProfileFor(m_pts[m_pref].lat, m_pts[m_pref].lon);
}

/* Indice en m_pts[] de la solucion cuya clave es `key` (-1 si no esta). */
int BpqMainWindow::solutionIndexForKey(const BpqEvent *ev, const QString &key) const
{
    if (!ev || key.isEmpty()) return -1;
    for (int j = 0; j < bpq_event_nsols(ev); j++) {
        const CsnhypodbpSolution *s = bpq_event_sol(ev, j);

        if (!s || solKey(s) != key) continue;
        for (int i = 0; i < m_npts; i++)
            if (m_ptSol[i] == j) return i;
    }
    return -1;
}

void BpqMainWindow::selectProfileFor(double lat, double lon)
{
    const int idx = bpq_profile_nearest(m_cfg, lat, lon);
    if (idx == m_profIndex) return;

    if (m_profData) { bpq_profile_free(m_profData); m_profData = nullptr; }
    m_profIndex = idx;
    if (idx >= 0) {
        char err[256] = "";
        m_profData = bpq_profile_load(m_cfg, idx, err, sizeof(err));
        if (!m_profData)
            statusBar()->showMessage(QStringLiteral("perfil %1: %2")
                                         .arg(idx).arg(QString::fromUtf8(err)), 8000);
    }
    m_mapCtx.active_profile = idx;
}

void BpqMainWindow::refreshPanels()
{
    int cw, ch;

    if (!m_cfg) return;

    /* --- planta --- */
    m_mapCtx.pts = m_pts;
    m_mapCtx.npts = m_npts;
    m_mapCtx.map = m_map;
    m_mapCtx.show_grid = 1;
    m_mapCtx.preferred = m_pref;
    m_mapCtx.dpr = m_mapPlan->devicePixelRatioF();
    m_mapCtx.bg = m_mapImage.isNull() ? nullptr : m_mapImage.constBits();
    m_mapCtx.bg_w = m_mapImage.width();
    m_mapCtx.bg_h = m_mapImage.height();
    m_mapCtx.bg_stride = int(m_mapImage.bytesPerLine());

    cw = qMax(60, m_mapPlan->width());
    ch = qMax(60, m_mapPlan->height());
    {
        double z = 1.0, cx = 0.0, cy = 0.0;
        int ok;
        /* Con soluciones, auto-encuadre a su bbox; sin ellas, el mundo completo
         * para que el mapa y los ejes se vean desde el arranque. Si el usuario ya
         * hizo zoom/paneo NO se toca (solo se re-encuadra al cambiar de evento). */
        if (m_mapUserView) {
            ok = -1;
        } else if (m_npts > 0) {
            m_mapCtx.pan_x = m_mapCtx.pan_y = 0.0;   /* vista nueva, sin paneo */
            ok = bpq_map_fit(m_cfg, &m_mapCtx, cw, ch, 12.0, &z, &cx, &cy);
        } else {
            m_mapCtx.pan_x = m_mapCtx.pan_y = 0.0;
            bpq_map_fit_world(&m_mapCtx, cw, ch, 8.0, &z, &cx, &cy);
            ok = 0;
        }
        if (ok == 0) {
            m_mapCtx.zoom = z;
            m_mapCtx.cx = cx;
            m_mapCtx.cy = cy;
        }
    }
    m_mapPlan->setContext(&m_mapCtx);

    /* --- perfil --- */
    m_profCtx.pts = m_pts;
    m_profCtx.npts = m_npts;
    m_profCtx.preferred = m_pref;
    m_profCtx.selected = m_mapCtx.selected;
    m_profCtx.depth_min_km = m_cfg->profile_depth_min_km;
    if (!m_profUserView) {
        m_profCtx.zoom = 1.0;
        m_profCtx.pan_x = m_profCtx.pan_y = 0.0;
    }
    m_profCtx.dpr = m_mapProf->devicePixelRatioF();
    m_profCtx.prof = m_profData;
    m_mapProf->setContext(&m_profCtx);
}

void BpqMainWindow::refreshStations(const BpqEvent *ev, bool forceFetch)
{
    if (!m_staModel) return;
    m_curEv = ev;
    m_candErr.clear();          /* un error del locate no sobrevive al cambio */

    /* El pin pertenece a UN evento: si el evento mostrado es otro, se suelta. */
    if (bpq_pin_active(&m_pin) && m_pinEvent != ev) {
        bpq_pin_clear(&m_pin);
        m_pinEvent = nullptr;
    }

    /* El overlay de picadas manuales es de UN evento: al cambiar de evento se
     * recarga el del nuevo (si su clave no coincide, el archivo se ignora). Si
     * quedaran las picadas del anterior, aparecerian como 'M' aqui. */
    if (m_selKey != m_manKey && !m_manualPath.isEmpty()) {
        (void)bpq_manual_load(m_manualPath.toUtf8().constData(),
                              m_selKey.toUtf8().constData());
        m_manKey = m_selKey;
    }

    /* La tabla sigue al origen FIJADO; sin pin, a la solucion elegida y si no
     * a la preferida. */
    int pi = pinnedSolutionIndex();
    if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
    if (!m_staCat || !ev || pi < 0 || pi >= m_npts) {
        m_candValid = false;
        m_candKey.clear();
        m_staModel->setRows(nullptr, 0);
        reapplySort(m_staProxy);
        m_staRows.clear();
        fetchTraces(nullptr);
        renderPreview();
        return;
    }

    const int solIdx = m_ptSol[pi];
    const CsnhypodbpSolution *sol = bpq_event_sol(ev, solIdx);
    if (!sol) {
        m_candValid = false;
        m_candKey.clear();
        m_staModel->setRows(nullptr, 0);
        reapplySort(m_staProxy);
        m_staRows.clear();
        fetchTraces(nullptr);
        renderPreview();
        return;
    }

    /* La candidata solo vale para la solucion sobre la que se corrio. Se compara
     * por identidad (el store puede reordenar y cambiar los indices). */
    if (m_candValid && m_candKey != solKey(sol)) {
        m_candValid = false;
        m_candKey.clear();
    }

    rebuildStaTable(sol);
    /* Un refresh del anillo NO debe re-descargar las ondas de la misma solucion
     * (cada tick abriria decenas de canales). El doble clic si fuerza la recarga. */
    if (forceFetch || m_curSolKey != solKey(sol)) fetchTraces(sol);
    renderPreview();
}

/* Tabla de estaciones SIN volver al store/SDS: tras un `locate` solo cambian
 * los residuales (los del localizador), que es lo que pinta la columna `res`. */
void BpqMainWindow::rebuildStaTable(const CsnhypodbpSolution *sol)
{
    CsnhypodbpManualPick man[CSNHYPODBP_MANUAL_MAX];
    const CsnhypodbpSolution *resid;
    int nman;

    if (!m_staCat || !sol || !m_staModel) return;
    resid = csnhypodbp_resid_source(m_candValid ? &m_cand : nullptr, m_candValid, sol);
    /* El overlay manual entra aca: es lo que hace que un clic en la traza se vea
     * como 'M' en la tabla y la marca de la traza se mueva. */
    nman = bpq_manual_list(man, CSNHYPODBP_MANUAL_MAX);
    m_staRows.resize(BPQ_MAX_STA_ROWS);
    {
        const int n = bpq_stations_build_resid(m_staCat, sol, resid, man, nman,
                                               m_staRows.data(), m_staRows.size());
        m_staRows.resize(n);
        m_staModel->setRows(m_staRows.constData(), n);
        reapplySort(m_staProxy);
        fitProcesarTable(m_staView, m_splitPicking);
        syncStationViews();
    }
}

/* Ondas de las estaciones picadas de la solucion elegida. El fetch se delega al
 * hilo del worker: aqui solo se arma la lista (SCNL ya resuelto) y se dejan las
 * filas visibles con su traza en NULL hasta que llegue. */
void BpqMainWindow::fetchTraces(const CsnhypodbpSolution *sol)
{
    QVector<BpqFetchReq> req;
    const CsnhypodbpStaRow *base = nullptr;

    ++m_waveJob;                 /* invalida cualquier resultado en vuelo */
    m_waveWith = 0;
    m_curSolKey.clear();         /* se fija solo si el pedido sale */
    for (BpqTrace *t : m_traces) bpq_trace_free(t);
    m_traces.clear();
    m_traceRows.clear();
    m_chanLabels.clear();
    m_chanPtr.clear();
    m_tracesCtx.chan = nullptr;

    if (!sol || !m_staCat) {
        updateTracesContext();
        renderMagnitudes();
        return;
    }
    /* La solucion ya tiene su vista de trazas preparada (aunque no haya
     * estaciones): un refresh con el mismo pin no debe volver a pedir ondas. */
    m_curSolKey = solKey(sol);
    m_winT0 = sol->otime - 5.0;
    m_winLen = 120.0;
    m_defT0 = m_winT0;           /* Shift+0 vuelve aca */
    m_defWin = m_winLen;
    /* Evento nuevo: la ventana vuelve al default y el zoom por dos clics se
     * limpia (su ancla y su historial son de la ventana anterior). */
    m_zoomAnchorX = -1.0;
    m_zoomAnchorT = 0.0;
    m_zoomUndo.clear();
    m_tracesCtx.zoom_anchor_t = 0.0;
    m_tracesCtx.use_filtered = 1;   /* al llegar, cada traza se demeana/filtra */

    /* La vista muestra SOLO las componentes de la estacion actual, en el orden
     * del .d (channelsOrder x componentsOrder). Sin eleccion, la mas cercana
     * que tenga picada, para no dejar el panel vacio. */
    for (int i = 0; i < m_staRows.size(); i++) {
        if (QString::fromUtf8(m_staRows.at(i).sta) == m_selSta) {
            base = &m_staRows.at(i);
            break;
        }
    }
    if (!base) {
        m_selSta = pickDefaultStation();
        for (int i = 0; i < m_staRows.size(); i++) {
            if (QString::fromUtf8(m_staRows.at(i).sta) == m_selSta) {
                base = &m_staRows.at(i);
                break;
            }
        }
    }
    if (!base) {
        updateTracesContext();
        renderMagnitudes();
        return;
    }

    buildTraceRows(base);
    m_tracesCtx.scroll_row = 0;

    for (int i = 0; i < m_traceRows.size() && req.size() < 40; i++) {
        const CsnhypodbpStaRow &r = m_traceRows.at(i);
        const char *sta = nullptr, *net = nullptr, *chan = nullptr, *loc = nullptr;
        const QByteArray ch = (i < m_chanLabels.size()) ? m_chanLabels.at(i)
                                                        : QByteArray();

        m_traces.append(nullptr);
        if (ch.isEmpty()) continue;
        if (!bpq_stations_scnl(m_staCat, r.idx, &sta, &net, &chan, &loc)) continue;
        /* El canal sale del orden del .d (HHZ/HHN/HHE...), no del catalogo: el
         * catalogo solo aporta net/loc y sirve para saber la banda. */
        req.append(BpqFetchReq{ QString::fromUtf8(sta), QString::fromUtf8(net),
                                QString::fromUtf8(ch), QString::fromUtf8(loc) });
    }
    updateTracesContext();
    renderMagnitudes();      /* el canvas de mags sigue a la estacion actual */
    syncStationViews();

    /* Fixture determinista: las filas ya estan armadas, pero no se pide onda al
     * store/SDS (el test no depende de datos ni del I/O de la capa SDS). */
    if (!qgetenv("BPQ_TEST_FIXTURE").isEmpty()) {
        m_waveDone = m_waveJob;
        return;
    }

    if (req.isEmpty() || !m_waveWorker)
        return;

    const quint64 id = m_waveJob;
    BpqWaveWorker *w = m_waveWorker;
    const double t0 = m_winT0;
    const double len = m_winLen;
    QMetaObject::invokeMethod(w, [w, id, req, t0, len]() {
        w->runJob(id, req, t0, len);
    }, Qt::QueuedConnection);
}

/* Filas del canvas para UNA estacion: sus canales en el orden del .d, con las
 * picadas P/S de esa estacion (todas las componentes las comparten). */
void BpqMainWindow::buildTraceRows(const CsnhypodbpStaRow *base)
{
    CsnhypodbpStaRow rows[16];
    char chans[16][8];
    int n, i;

    m_traceRows.clear();
    m_chanLabels.clear();
    m_chanPtr.clear();
    if (!base || !m_staCat || !m_cfg) return;

    n = bpq_stations_chan_rows(m_staCat, base->sta, m_cfg->channels_order,
                               m_cfg->components_order, base, rows, chans, 16);
    for (i = 0; i < n; i++) {
        m_traceRows.append(rows[i]);
        m_chanLabels.append(QByteArray(chans[i]).left(8));
    }
    for (const QByteArray &b : m_chanLabels) m_chanPtr.append(b.constData());
    m_tracesCtx.chan = m_chanPtr.isEmpty() ? nullptr : m_chanPtr.constData();
}

/* Estacion por defecto: la primera de la tabla (ordenada por distancia) que tenga
 * picada; asi el panel muestra algo util al arrancar. */
QString BpqMainWindow::pickDefaultStation() const
{
    for (const CsnhypodbpStaRow &r : m_staRows)
        if (r.p.present || r.s.present) return QString::fromUtf8(r.sta);
    return QString();
}

/* El proxy cambia la numeracion de filas: todo lo que venia usando indices de
 * la vista/`selectRow` tiene que pasar por aqui. */
int BpqMainWindow::viewRowForSource(const QSortFilterProxyModel *p, int srcRow) const
{
    if (!p || !p->sourceModel() || srcRow < 0) return -1;
    {
        const QModelIndex idx = p->mapFromSource(p->sourceModel()->index(srcRow, 0));

        return idx.isValid() ? idx.row() : -1;
    }
}

int BpqMainWindow::sourceRowForView(const QSortFilterProxyModel *p,
                                    const QModelIndex &idx) const
{
    if (!p) return idx.isValid() ? idx.row() : -1;
    {
        const QModelIndex src = p->mapToSource(idx);

        return src.isValid() ? src.row() : -1;
    }
}

/* Tras un reset del modelo fuente el proxy puede perder el orden (y la flecha
 * seguiria mintiendo); se re-aplica el ultimo clic si hubo uno. */
void BpqMainWindow::reapplySort(ewguiqt::SortProxy *p)
{
    if (!p || p->lastColumn() < 0) return;
    p->sort(p->lastColumn(), p->lastOrder());
}

void BpqMainWindow::fitProcesarTable(QTableView *view, QSplitter *split)
{
    QList<int> sz;
    int total, need, avail, rest;

    if (!view || !split) return;
    total = ewguiqt::fit_columns(view, 10);
    if (total <= 0) return;
    /* La barra vertical tambien come ancho: sin ella el viewport se queda
     * corto y aparece el scroll horizontal que se quiere evitar. */
    /* +8 de holgura: el marco/handle del splitter y el redondeo del header
     * dejaban el viewport 1-2 px corto y aparecia el scroll horizontal. */
    need = total + view->verticalScrollBar()->sizeHint().width()
           + 2 * view->frameWidth() + 8;
    sz = split->sizes();
    if (sz.size() < 2 || sz.at(0) >= need) return;
    avail = sz.at(0) + sz.at(1);
    if (need >= avail) return;  /* no cabe ni con todo el panel: scroll */
    rest = avail - need;
    split->setSizes(QList<int>() << need << rest);
}

/* Unico camino de cambio de estacion: m_selSta manda en los dos canvas. */
void BpqMainWindow::selectStation(const QString &sta)
{
    if (sta.isEmpty()) return;
    if (sta == m_selSta) {
        syncStationViews();
        return;
    }
    m_selSta = sta;
    syncStationViews();
    fetchTraces(currentSolution());
}

/* Pinta la estacion actual en ambas tablas sin disparar sus slots (bloqueo de
 * senales): la seleccion programatica no debe provocar otro fetch. */
void BpqMainWindow::syncStationViews()
{
    int si = -1, mi = -1;
    bool staAlready = false, magAlready = false;

    if (m_selSta.isEmpty()) return;
    for (int i = 0; i < m_staRows.size(); i++) {
        if (m_selSta == QString::fromUtf8(m_staRows.at(i).sta)) {
            si = i;
            break;
        }
    }
    /* Si la fila elegida YA es de esta estacion, no se toca: el usuario puede
     * haber elegido la S y no debe saltar a la P (las filas son autonomas). */
    if (m_staView && m_staProxy && m_staView->currentIndex().isValid()) {
        const int src = sourceRowForView(m_staProxy, m_staView->currentIndex());
        const int ci = m_staModel ? m_staModel->staIndexAt(src) : -1;

        if (ci >= 0 && ci < m_staRows.size() &&
            m_selSta == QString::fromUtf8(m_staRows.at(ci).sta))
            staAlready = true;
    }
    if (!staAlready && m_staView && m_staProxy && m_staView->selectionModel()) {
        QSignalBlocker b(m_staView->selectionModel());

        if (si >= 0) {
            const int vr = viewRowForSource(m_staProxy, si * 2);

            if (vr >= 0) {
                m_staView->selectRow(vr);
                m_staView->scrollTo(m_staView->model()->index(vr, 0));
            }
        } else {
            m_staView->clearSelection();
        }
    }

    for (int i = 0; i < m_magRows.size(); i++) {
        if (m_selSta == QString::fromUtf8(m_magRows.at(i).sta)) {
            mi = i;
            break;
        }
    }
    if (m_magView && m_magProxy && m_magView->currentIndex().isValid()) {
        const int src = sourceRowForView(m_magProxy, m_magView->currentIndex());
        const CsnhypodbpMagRow *cr = m_magModel ? m_magModel->row(src) : nullptr;

        if (cr && m_selSta == QString::fromUtf8(cr->sta))
            magAlready = true;
    }
    if (!magAlready && m_magView && m_magProxy && m_magView->selectionModel()) {
        QSignalBlocker b(m_magView->selectionModel());

        if (mi >= 0) {
            const int vr = viewRowForSource(m_magProxy, mi);

            if (vr >= 0) {
                m_magView->selectRow(vr);
                m_magView->scrollTo(m_magView->model()->index(vr, 0));
            }
        } else {
            m_magView->clearSelection();
        }
    }
}

const CsnhypodbpSolution *BpqMainWindow::currentSolution() const
{
    int pi = pinnedSolutionIndex();

    if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
    if (!m_curEv || pi < 0 || pi >= m_npts) return nullptr;
    return bpq_event_sol(m_curEv, m_ptSol[pi]);
}

/* Indice en m_pts[] del origen FIJADO, o -1 si no hay pin (o es de otro
 * evento). Es la unica fuente de verdad de "que solucion muestra Procesar". */
int BpqMainWindow::pinnedSolutionIndex() const
{
    if (!bpq_pin_active(&m_pin) || !m_originsModel || m_pinEvent != m_curEv)
        return -1;

    const int row = m_originsModel->rowForKey(
        QString::fromLatin1(bpq_pin_key(&m_pin)));
    if (row < 0) return -1;
    const int sol = m_originsModel->solIndexAt(row);
    for (int i = 0; i < m_npts; i++)
        if (m_ptSol[i] == sol) return i;
    return -1;
}

void BpqMainWindow::onOriginDoubleClicked(const QModelIndex &idx)
{
    const int row = sourceRowForView(m_originsProxy, idx);

    if (row >= 0) pinOrigin(row, true, true);
}

/* Fija la fila `row` de Origins. Es el UNICO camino que cambia el pin (doble
 * clic o clic en un marcador del mapa); el clic simple nunca lo mueve. */
void BpqMainWindow::pinOrigin(int row, bool forceReload, bool switchTab)
{
    if (!m_originsModel || row < 0 || row >= m_originsModel->rowCount()) return;
    const int sol = m_originsModel->solIndexAt(row);
    if (sol < 0) return;

    const QString key = m_originsModel->keyAt(row);
    if (key.isEmpty() ||
        bpq_pin_set(&m_pin, key.toLatin1().constData()) != 0)
        return;

    int idx = -1;
    for (int i = 0; i < m_npts; i++)
        if (m_ptSol[i] == sol) { idx = i; break; }
    if (idx < 0) {                      /* fuera del tope de 64 puntos */
        bpq_pin_clear(&m_pin);
        m_pinEvent = nullptr;
        return;
    }

    m_pinEvent = m_originsModel->currentEvent();
    m_mapCtx.selected = idx;
    m_profCtx.selected = idx;
    m_mapPlan->requestRepaint();
    m_mapProf->requestRepaint();

    if (switchTab) {
        if (m_tabs) m_tabs->setCurrentIndex(1);      /* Procesar */
        if (m_subtabs) m_subtabs->setCurrentIndex(0);/* picking */
    }

    {
        const int vrow = viewRowForSource(m_originsProxy, row);

        QSignalBlocker b(m_originsView->selectionModel());
        if (vrow >= 0) m_originsView->selectRow(vrow);
    }
    refreshStations(m_originsModel->currentEvent(), forceReload);
}

/* Los canvas leen del contexto: hay que repuntarlo tras cada cambio del vector. */
void BpqMainWindow::updateTracesContext()
{
    m_tracesCtx.rows = m_traceRows.constData();    m_tracesCtx.n = m_traceRows.size();
    m_tracesCtx.tr = m_traces.constData();
    m_tracesCtx.t0 = m_winT0;
    m_tracesCtx.window = m_winLen;
    m_tracesCtx.show_p = 1;
    m_tracesCtx.show_s = 1;
    m_tracesCtx.colors_p = m_cfg ? &m_cfg->colors_p : nullptr;
    m_tracesCtx.colors_s = m_cfg ? &m_cfg->colors_s : nullptr;
    /* tracesPerScreen del .d (1..3; fuera de rango => 1) y etiquetas de canal. */
    m_tracesCtx.per_screen = 1;
    if (m_cfg) {
        const int per = csnhypodbp_traces_per_screen_norm(m_cfg->traces_per_screen);

        if (per > 0) m_tracesCtx.per_screen = per;
    }
    m_tracesCtx.chan = m_chanPtr.isEmpty() ? nullptr : m_chanPtr.constData();

    if (m_waves) {
        m_tracesCtx.dpr = m_waves->devicePixelRatioF();
        m_waves->setContext(&m_tracesCtx);
    }
    if (m_axis) {
        m_tracesCtx.dpr = m_axis->devicePixelRatioF();
        m_axis->setContext(&m_tracesCtx);
    }
    syncTracesScroll();
    if (m_waves) m_waves->requestRepaint();
    if (m_axis) m_axis->requestRepaint();
}

/* La barra es la unica fuente visible del scroll: rango = filas que quedan
 * fuera, pageStep = bandas que caben. Se actualiza tras cada cambio de filas,
 * de tracesPerScreen o de tamano del canvas. */
void BpqMainWindow::syncTracesScroll()
{
    int hpx = 1, nvis = 1, max = 0;

    if (!m_tracesScroll) return;
    if (m_waves) {
        const double d = m_waves->devicePixelRatioF();

        hpx = int(m_waves->height() * d);
    }
    if (hpx < 1) hpx = 1;
    nvis = bpq_traces_page_rows(&m_tracesCtx, hpx);
    if (nvis < 1) nvis = 1;
    max = bpq_traces_scroll_max(&m_tracesCtx, hpx);
    if (max < 0) max = 0;
    {
        const QSignalBlocker b(m_tracesScroll);

        m_tracesScroll->setRange(0, max);
        m_tracesScroll->setPageStep(nvis);
        m_tracesScroll->setSingleStep(1);
        m_tracesScroll->setValue(qBound(0, m_tracesCtx.scroll_row, max));
        m_tracesScroll->setEnabled(max > 0);
    }
    m_tracesCtx.scroll_row = m_tracesScroll->value();
}

bool BpqMainWindow::waitForWaves(int ms)
{
    QElapsedTimer t;

    t.start();
    while (m_waveDone != m_waveJob && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return m_waveDone == m_waveJob;
}

static bool isTextInputWidget(const QWidget *w)
{
    if (!w) return false;
    if (qobject_cast<const QLineEdit *>(w) ||
        qobject_cast<const QTextEdit *>(w) ||
        qobject_cast<const QPlainTextEdit *>(w) ||
        qobject_cast<const QAbstractSpinBox *>(w))
        return true;
    {
        const QComboBox *cb = qobject_cast<const QComboBox *>(w);

        if (cb && cb->isEditable()) return true;
    }
    return false;
}

bool BpqMainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Resize) {
        if (obj == m_mapPlan || obj == m_mapProf)
            refreshPanels();
        else if (obj == m_waves)
            syncTracesScroll();
    }

    /* Rueda sobre las trazas: sin Ctrl desplaza filas (lo que espera el
     * operador); Ctrl la deja pasar al canvas para el zoom temporal. Asi la
     * rueda no compite con el zoom de Shift+Left/Right. */
    if (obj == m_waves && ev->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent *>(ev);

        if (!(we->modifiers() & Qt::ControlModifier)) {
            const int dy = we->angleDelta().y();

            if (dy != 0) {
                const double d = m_waves->devicePixelRatioF();
                const int hpx = int(m_waves->height() * d);
                int steps = dy / 120;

                if (steps == 0) steps = (dy > 0) ? 1 : -1;
                bpq_traces_scroll_to(&m_tracesCtx, hpx,
                                     m_tracesCtx.scroll_row - steps);
                syncTracesScroll();
                if (m_waves) m_waves->requestRepaint();
                if (m_axis) m_axis->requestRepaint();
                we->accept();
                return true;
            }
        }
    }

    /* Teclas del picking: se resuelven en la ventana, sin depender del widget
     * con foco. La tabla de estaciones no hace busqueda incremental. */
    if (ev->type() == QEvent::KeyPress) {
        QWidget *w = qobject_cast<QWidget *>(obj);

        if (w && w->window() == this && !isTextInputWidget(w) &&
            !QApplication::activeModalWidget()) {
            if (handlePickingKey(static_cast<QKeyEvent *>(ev)))
                return true;
            if (m_staView && (w == m_staView || m_staView->isAncestorOf(w))) {
                const int k = static_cast<QKeyEvent *>(ev)->key();

                if ((k >= Qt::Key_A && k <= Qt::Key_Z) ||
                    (k >= Qt::Key_0 && k <= Qt::Key_9))
                    return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

/* Teclas del picking. true = consumida. Se llama desde el filtro de la ventana
 * (cualquier foco) y desde keyPressEvent, asi que la tabla no puede quedarse
 * con P/S ni con las flechas. */
bool BpqMainWindow::handlePickingKey(QKeyEvent *ke)
{
    if (!ke) return false;
    if (!m_subtabs || m_subtabs->currentIndex() != 0) return false;
    if (focusIsTextInput() || QApplication::activeModalWidget()) return false;

    const int k = ke->key();
    const bool shift = (ke->modifiers() & Qt::ShiftModifier) != 0;
    const bool blocked =
        (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) != 0;

    /* Shift+flechas (paridad GTK): arriba/abajo exageran la amplitud vertical
     * (x1.5 / /1.5, tope 0.1..50) y derecha/izquierda expanden o contraen la
     * ventana temporal (x1.5 / /1.5, centrada). Shift+0 vuelve a la ventana y
     * a la exageracion del evento. */
    if (!blocked && shift && (k == Qt::Key_Up || k == Qt::Key_Down)) {
        double g = (m_tracesCtx.y_gain > 0.0) ? m_tracesCtx.y_gain : 1.0;

        g = (k == Qt::Key_Up) ? g * 1.5 : g / 1.5;
        if (g < 0.1) g = 0.1;
        if (g > 50.0) g = 50.0;
        m_tracesCtx.y_gain = g;
        if (m_waves) m_waves->requestRepaint();
        statusBar()->showMessage(
            QStringLiteral("exageracion vertical x%1").arg(g, 0, 'f', 2), 2000);
        return true;
    }
    if (!blocked && shift && (k == Qt::Key_Left || k == Qt::Key_Right)) {
        bpq_traces_zoom_span(&m_tracesCtx.t0, &m_tracesCtx.window,
                             (k == Qt::Key_Right) ? 1 : -1);
        applyTracesWindow();
        statusBar()->showMessage(
            QStringLiteral("ventana %1 s").arg(m_winLen, 0, 'f', 1), 2000);
        return true;
    }
    if (!blocked && shift && k == Qt::Key_0) {
        m_tracesCtx.y_gain = 1.0;
        m_tracesCtx.t0 = m_defT0;
        m_tracesCtx.window = m_defWin;
        applyTracesWindow();
        statusBar()->showMessage(QStringLiteral("vista reseteada"), 2000);
        return true;
    }
    /* Flechas/Page/Home/End: desplazan la banda visible cuando la estacion
     * tiene mas componentes que las que caben (tracesPerScreen). */
    if (!blocked && !shift && (k == Qt::Key_Up || k == Qt::Key_Down ||
                               k == Qt::Key_PageUp || k == Qt::Key_PageDown ||
                               k == Qt::Key_Home || k == Qt::Key_End)) {
        const double d = (m_waves ? m_waves->devicePixelRatioF() : 1.0);
        const int hpx = m_waves ? int(m_waves->height() * d) : 1;
        const int vis = bpq_traces_page_rows(&m_tracesCtx, hpx);
        int row = m_tracesCtx.scroll_row;

        if (k == Qt::Key_Up) row--;
        else if (k == Qt::Key_Down) row++;
        else if (k == Qt::Key_PageUp) row -= (vis > 0 ? vis : 1);
        else if (k == Qt::Key_PageDown) row += (vis > 0 ? vis : 1);
        else if (k == Qt::Key_Home) row = 0;
        else if (k == Qt::Key_End)
            row = bpq_traces_scroll_max(&m_tracesCtx, hpx);
        bpq_traces_scroll_to(&m_tracesCtx, hpx, row);
        syncTracesScroll();
        if (m_waves) m_waves->requestRepaint();
        if (m_axis) m_axis->requestRepaint();
        statusBar()->showMessage(
            QStringLiteral("canal %1/%2")
                .arg(m_tracesCtx.scroll_row + 1)
                .arg(m_traceRows.size()),
            2000);
        return true;
    }

    if (!blocked && (k == Qt::Key_P || k == Qt::Key_S)) {
        m_pickPhase = (k == Qt::Key_P) ? 'P' : 'S';
        m_pickArmed = true;
        updatePickCursor();
        statusBar()->showMessage(
            QStringLiteral("modo picada: %1 (clic izquierdo pica, derecho "
                           "quita, Esc sale)")
                .arg(QLatin1Char(m_pickPhase)), 4000);
        return true;
    }
    if (k == Qt::Key_Escape) {
        m_pickArmed = false;
        m_zoomAnchorX = -1.0;
        m_zoomAnchorT = 0.0;
        m_tracesCtx.zoom_anchor_t = 0.0;
        updatePickCursor();
        if (m_waves) m_waves->requestRepaint();
        statusBar()->showMessage(QStringLiteral("modo picada: off"), 2000);
        return true;
    }

    /* Filtros/STA-LTA del .d: solo se consumen si la tecla tiene binding. */
    if (!blocked && !shift) {
        char key = 0;

        if (k >= Qt::Key_A && k <= Qt::Key_Z)
            key = char('a' + (k - Qt::Key_A));
        else if (k >= Qt::Key_0 && k <= Qt::Key_9)
            key = char('0' + (k - Qt::Key_0));
        if (key && applyFilterKey(key)) return true;
    }
    return false;
}

/* Clic sobre una traza: coloca la picada de la fase activa en el OVERLAY local
 * (no toca el anillo). Se ve al instante -- la marca de la traza y la fila de la
 * tabla pasan a origen M -- y se publica recien al Confirmar, igual que la GTK:
 * asi el operador mueve el pick sin que csnloc asocie nada a medio hacer. */
void BpqMainWindow::emitManualPick(int row, double t)
{
    const char *sta = nullptr, *net = nullptr, *chan = nullptr, *loc = nullptr;

    if (!m_staCat || row < 0 || row >= m_traceRows.size()) return;
    {
        const CsnhypodbpStaRow &r = m_traceRows.at(row);

        if (!bpq_stations_scnl(m_staCat, r.idx, &sta, &net, &chan, &loc)) return;
    }
    if (bpq_manual_set(sta, chan, m_pickPhase, t) != 0) return;

    refreshAfterManual();

    statusBar()->showMessage(
        QStringLiteral("pick %1 %2  t=%3  (local; se publica al Confirmar)")
            .arg(QString::fromUtf8(sta))
            .arg(QLatin1Char(m_pickPhase))
            .arg(t, 0, 'f', 3),
        4000);
}

/* Todo lo que cambia cuando el overlay manual cambia: la tabla (origen M), las
 * filas dibujadas, el repintado y el archivo. */
void BpqMainWindow::refreshAfterManual()
{
    int pi = pinnedSolutionIndex();
    if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
    const CsnhypodbpSolution *sol = (m_curEv && pi >= 0 && pi < m_npts)
        ? bpq_event_sol(m_curEv, m_ptSol[pi]) : nullptr;

    if (sol) rebuildStaTable(sol);
    syncTraceRows();
    updateTracesContext();
    if (m_waves) m_waves->requestRepaint();
    saveManualPicks();
}

/* Circulo con la letra de la fase bajo el cursor (el "modo picada" de la GTK):
 * el color es el pColor/sColor manual del .d. */
void BpqMainWindow::updatePickCursor()
{
    if (!m_waves || !m_cfg) return;
    if (!m_pickArmed) {
        m_waves->unsetCursor();
        return;
    }
    {
        const double *rgb = (m_pickPhase == 'P') ? m_cfg->colors_p.manual
                                                 : m_cfg->colors_s.manual;
        QPixmap pm(26, 26);

        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);

            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]));
            p.setPen(QPen(QColor(25, 25, 25), 1.5));
            p.drawEllipse(QRectF(3.0, 3.0, 20.0, 20.0));
            p.setPen(Qt::white);
            {
                QFont f = p.font();

                f.setBold(true);
                f.setPointSize(9);
                p.setFont(f);
            }
            p.drawText(QRectF(3.0, 3.0, 20.0, 20.0), Qt::AlignCenter,
                       QString(QLatin1Char(m_pickPhase)));
        }
        m_waves->setCursor(QCursor(pm, 13, 13));
    }
}

/* ctx -> m_winT0/m_winLen (fuente de verdad de la ventana) + repintado de las
 * trazas y del eje. */
void BpqMainWindow::applyTracesWindow()
{
    m_winT0 = m_tracesCtx.t0;
    m_winLen = m_tracesCtx.window;
    updateTracesContext();
    if (m_waves) m_waves->requestRepaint();
    if (m_axis) m_axis->requestRepaint();
}

/* Dos clics: el primero deja un ancla (linea discontinua) y el segundo, a la
 * DERECHA, acerca al intervalo; a la IZQUIERDA deshace el ultimo zoom. Es el
 * gesto de la GTK, con el ancla visible para no adivinar. */
void BpqMainWindow::zoomClickAt(double x, double y)
{
    const double d = m_waves ? m_waves->devicePixelRatioF() : 1.0;
    const int wpx = m_waves ? int(m_waves->width() * d) : 0;

    (void)y;
    if (!m_waves || wpx <= 0) return;

    if (m_zoomAnchorX < 0.0) {
        m_zoomAnchorX = x * d;
        m_zoomAnchorT = bpq_traces_time_at(&m_tracesCtx, wpx, x * d);
        m_tracesCtx.zoom_anchor_t = m_zoomAnchorT;
        m_waves->requestRepaint();
        if (m_axis) m_axis->requestRepaint();
        statusBar()->showMessage(
            QStringLiteral("zoom: segundo clic a la derecha acerca; a la izquierda deshace"),
            5000);
        return;
    }

    {
        const double x0 = m_zoomAnchorX;
        const double x1 = x * d;

        m_zoomAnchorX = -1.0;
        m_zoomAnchorT = 0.0;
        m_tracesCtx.zoom_anchor_t = 0.0;

        if (x1 > x0) {
            const double t0 = m_tracesCtx.t0;
            const double win = m_tracesCtx.window;

            if (bpq_traces_zoom_range(&m_tracesCtx, wpx, x0, x1) == 1) {
                m_zoomUndo.append(QPointF(t0, win));
                applyTracesWindow();
                statusBar()->showMessage(
                    QStringLiteral("zoom: ventana %1 s").arg(m_tracesCtx.window, 0, 'f', 1),
                    3000);
            } else {
                applyTracesWindow();   /* dos clics en el mismo pixel */
            }
        } else if (!m_zoomUndo.isEmpty()) {
            const QPointF prev = m_zoomUndo.takeLast();

            m_tracesCtx.t0 = prev.x();
            m_tracesCtx.window = prev.y();
            applyTracesWindow();
            statusBar()->showMessage(
                QStringLiteral("zoom deshecho: ventana %1 s").arg(m_tracesCtx.window, 0, 'f', 1),
                3000);
        } else {
            applyTracesWindow();
            statusBar()->showMessage(
                QStringLiteral("zoom: no hay zoom que deshacer"), 3000);
        }
    }
}

/* Copia a las filas dibujadas lo que la tabla acaba de recalcular (misma
 * estacion, mismo canal): asi la marca P/S sale de la picada elegida. */
void BpqMainWindow::syncTraceRows()
{
    for (int i = 0; i < m_traceRows.size(); i++) {
        for (int j = 0; j < m_staRows.size(); j++) {
            if (std::strcmp(m_traceRows.at(i).sta, m_staRows.at(j).sta) != 0)
                continue;
            m_traceRows[i] = m_staRows.at(j);
            break;
        }
    }
}

void BpqMainWindow::saveManualPicks()
{
    if (m_manualPath.isEmpty()) return;
    (void)bpq_manual_save(m_manualPath.toUtf8().constData(),
                          m_manKey.toUtf8().constData());
}

/* Filtro de VISTA sobre una traza. `idx` < 0 = Raw (solo demean, para que la
 * linea base quede centrada). El buffer crudo NO se toca: `locate` manda ese. */void BpqMainWindow::filterOne(BpqTrace *t)
{
    if (!t || !m_cfg) return;
    if (m_filterIdx < 0 || m_filterIdx >= m_cfg->filtercfg.nf) {
        (void)bpq_trace_filter(t, 0, 0.0, 0.0, 0, 1);
        return;
    }
    {
        const CsnhypodbpFilterPreset &f = m_cfg->filtercfg.f[m_filterIdx];
        (void)bpq_trace_filter(t, f.type, f.f1, f.f2, f.order,
                               m_phaseZero ? 1 : 0);
    }
}

void BpqMainWindow::applyFilter(int idx){
    if (!m_cfg || idx >= m_cfg->filtercfg.nf) idx = -1;
    m_filterIdx = idx;

    for (BpqTrace *t : m_traces) filterOne(t);
    m_tracesCtx.use_filtered = 1;     /* el buffer filtrado lleva el demean */

    rebuildStalta();
    updateTracesContext();
    if (m_waves) m_waves->requestRepaint();

    statusBar()->showMessage(
        QStringLiteral("filtro: %1%2")
            .arg(idx < 0 ? QStringLiteral("Raw")
                         : QString::fromUtf8(m_cfg->filtercfg.f[idx].name))
            .arg(m_phaseZero ? QStringLiteral(" (fase cero)")
                                             : QStringLiteral(" (causal)")),
        4000);
}

/* ¿El foco esta en una caja de texto? Entonces las teclas son TEXTO y los
 * atajos del picking no se consumen. */
bool BpqMainWindow::focusIsTextInput() const
{
    return isTextInputWidget(QApplication::focusWidget());
}

/* Atajo de filtro del .d; la logica -- incluido "la tecla del filtro activo lo
 * apaga" -- es del nucleo puro csnhypodbp_filtercfg_key_action. */
bool BpqMainWindow::applyFilterKey(int key)
{
    char k = char(key);
    char sk;
    int act;

    if (!m_cfg) return false;

    /* STA/LTA tiene su propia tecla, no es un preset. */
    sk = m_cfg->filtercfg.stalta_key;
    if (sk >= 'A' && sk <= 'Z') sk = char(sk + 32);
    if (sk && sk == k) {
        m_staltaOn = !m_staltaOn;
        rebuildStalta();
        updateTracesContext();
        if (m_waves) m_waves->requestRepaint();
        statusBar()->showMessage(
            QStringLiteral("STA/LTA %1").arg(m_staltaOn ? QStringLiteral("on")
                                                        : QStringLiteral("off")),
            2000);
        return true;
    }

    act = csnhypodbp_filtercfg_key_action(&m_cfg->filtercfg, m_filterIdx, k);
    if (act == CSNHYPODBP_FILTER_KEY_NONE) return false;
    if (act == CSNHYPODBP_FILTER_KEY_MENU) {
        openFilters();
        return true;
    }
    applyFilter(act);       /* RAW (-1) o el preset elegido */
    return true;
}

void BpqMainWindow::openFilters()
{
    BpqFiltersDialog dlg(m_cfg, m_filterIdx, m_phaseZero, m_staltaOn, this);

    if (dlg.exec() != QDialog::Accepted) return;
    m_phaseZero = dlg.phaseZero();   /* antes de reaplicar: filterOne lo usa */
    m_staltaOn = dlg.stalta();
    applyFilter(dlg.selected());
}

/* Atajos SOLO en la sub-pestana `picking` y nunca con el foco en una caja de
 * texto. El filtro de aplicación ya los intercepta antes del widget con foco;
 * esto es el camino de respaldo cuando el evento llega a la ventana. */
void BpqMainWindow::keyPressEvent(QKeyEvent *ev)
{
    if (handlePickingKey(ev)) {
        ev->accept();
        return;
    }
    QMainWindow::keyPressEvent(ev);
}

void BpqMainWindow::rebuildStalta()
{
    const CsnhypodbpFilterCfg *fc = m_cfg ? &m_cfg->filtercfg : nullptr;

    for (BpqStaLta *s : m_stalta) bpq_stalta_free(s);
    m_stalta.clear();

    if (!fc || !m_staltaOn || !fc->stalta_declared) {
        m_tracesCtx.show_stalta = 0;
        m_tracesCtx.stalta = nullptr;
        return;
    }

    for (int i = 0; i < m_traces.size(); i++) {
        BpqStaLta *s = nullptr;
        if (m_traces.at(i)) {
            s = bpq_stalta_new(m_traces.at(i), fc->stalta_sta, fc->stalta_lta,
                               fc->stalta_on_thr, fc->stalta_off_thr,
                               fc->stalta_corr);
        }
        m_stalta.append(s);
    }
    m_tracesCtx.show_stalta = 1;
    m_tracesCtx.stalta = m_stalta.constData();
}

/* Barra inferior del picking (paridad con la GTK): `Cancel` a la izquierda y, a
 * la derecha, los checks A/M/Teo -- que solo filtran el ENVIO, no la tabla --,
 * el localizador activo y `locate`. `Fix`/`depth` llegan con el preview. */
void BpqMainWindow::buildPickingBar(QWidget *parent, QVBoxLayout *lay)
{
    QWidget *bar = new QWidget(parent);
    QHBoxLayout *hb = new QHBoxLayout(bar);
    hb->setContentsMargins(2, 0, 2, 0);
    hb->setSpacing(6);

    bar->setObjectName(QStringLiteral("picking_bar"));
    m_locCancel = new QPushButton(QStringLiteral("Cancel"), bar);
    m_locCancel->setObjectName(QStringLiteral("locate_cancel"));
    m_locCancel->setEnabled(false);
    m_locCancel->setToolTip(QStringLiteral(
        "Aborta el locate en curso (el refinador se corta en el proximo paso)"));
    connect(m_locCancel, &QPushButton::clicked, this, &BpqMainWindow::cancelLocate);
    hb->addWidget(m_locCancel, 0);
    hb->addStretch(1);

    auto mkchk = [&](const char *txt, const char *name, bool on) {
        QCheckBox *c = new QCheckBox(QString::fromUtf8(txt), bar);
        c->setObjectName(QString::fromUtf8(name));
        c->setChecked(on);
        c->setToolTip(QStringLiteral(
            "Solo filtra lo que se manda al localizador (la tabla no cambia)"));
        return c;
    };
    /* Default de la GTK: M (csnhypodbp_main.c:202-204 auto=0 manual=1 theo=0). */
    m_chkA = mkchk("A", "chk_auto", false);
    m_chkM = mkchk("M", "chk_manual", true);
    m_chkT = mkchk("Teo", "chk_theo", false);
    hb->addWidget(m_chkA, 0);
    hb->addWidget(m_chkM, 0);
    hb->addWidget(m_chkT, 0);

    /* `Fix` + profundidad fija: se copia al `.d` temporal del refinador
     * (FixedDepthKm) solo cuando la casilla esta puesta. La caja se acota al
     * valor canonico + 2 para no inflar el minimo de la ventana (WSLg). */
    m_chkFix = new QCheckBox(QStringLiteral("Fix"), bar);
    m_chkFix->setObjectName(QStringLiteral("chk_fix"));
    m_chkFix->setChecked(m_cfg->fix_depth_on != 0);
    m_chkFix->setToolTip(QStringLiteral(
        "Profundidad fija: el refinador recibe FixedDepthKm en su .d temporal"));
    hb->addWidget(m_chkFix, 0);
    m_editFix = new QLineEdit(bar);
    m_editFix->setObjectName(QStringLiteral("edit_fix_depth"));
    {
        const double d0 = m_cfg->fix_depth_km > 0.0 ? m_cfg->fix_depth_km : 10.0;
        const QString txt = QString::number(d0, 'f', 1);

        m_editFix->setText(txt);
        m_editFix->setMaxLength(8);
        m_editFix->setMaximumWidth(
            m_editFix->fontMetrics().horizontalAdvance(txt + QStringLiteral("00")));
    }
    m_editFix->setEnabled(m_cfg->fix_depth_on != 0);
    connect(m_chkFix, &QCheckBox::toggled, this, [this](bool on) {
        if (m_editFix) m_editFix->setEnabled(on);
    });
    hb->addWidget(m_editFix, 0);

    m_locCombo = new QComboBox(bar);
    m_locCombo->setObjectName(QStringLiteral("locator_combo"));
    m_locCombo->setToolTip(QStringLiteral("localizador activo (locator del .d)"));
    for (int i = 0; i < m_cfg->n_locators; i++) {
        m_locCombo->addItem(QString::fromUtf8(m_cfg->locators[i].name));
        if (m_cfg->active_locator[0] &&
            std::strcmp(m_cfg->locators[i].name, m_cfg->active_locator) == 0)
            m_locCombo->setCurrentIndex(i);
    }
    hb->addWidget(m_locCombo, 0);

    m_locBtn = new QPushButton(QStringLiteral("locate"), bar);
    m_locBtn->setObjectName(QStringLiteral("locate_button"));
    m_locBtn->setToolTip(QStringLiteral(
        "Envia al localizador las picadas efectivas (M > A > Teo) de la tabla"));
    connect(m_locBtn, &QPushButton::clicked, this, &BpqMainWindow::locateNow);
    hb->addWidget(m_locBtn, 0);

    lay->addWidget(bar, 0);
}

/* Manda las picadas de la tabla al localizador. La cadena (csnloc + refinador)
 * corre en el worker: aqui solo se arman copias y se deshabilita el boton. */
void BpqMainWindow::locateNow()
{
    int pi = pinnedSolutionIndex();
    if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
    const CsnhypodbpSolution *sol = nullptr;

    if (m_locBusy) return;
    m_candErr.clear();

    if (!m_cfg || !m_staCat || m_staRows.isEmpty() || !m_curEv) {
        m_candErr = QStringLiteral("sin tabla de estaciones");
    } else if (pi >= 0 && pi < m_npts) {
        sol = bpq_event_sol(m_curEv, m_ptSol[pi]);
        if (!sol) m_candErr = QStringLiteral("sin solucion elegida");
    } else {
        m_candErr = QStringLiteral("sin solucion elegida");
    }
    if (!sol) {
        statusBar()->showMessage(QStringLiteral("locate: %1").arg(m_candErr), 0);
        renderPreview();
        return;
    }

    {
        QVector<CsnhypodbpStaRow> rows = m_staRows;
        QVector<int> on;
        const CsnhypodbpSolution base = *sol;
        const int am = (m_chkA && m_chkA->isChecked()) ? 1 : 0;
        const int mm = (m_chkM && m_chkM->isChecked()) ? 1 : 0;
        const int tm = (m_chkT && m_chkT->isChecked()) ? 1 : 0;
        const double fix = fixDepthKm();
        QByteArray loc = m_locCombo ? m_locCombo->currentText().toUtf8()
                                    : QByteArray();
        QVector<BpqFetchReq> magReq;
        double magT0 = 0.0, magWin = 700.0;
        quint64 id;
        BpqLocateWorker *w = m_locWorker;

        on.reserve(rows.size() * 2);
        for (int i = 0; i < rows.size(); i++) {
            on.append(1);
            on.append(1);
        }

        /* Estaciones con P (segun los checks) de las que se baja onda para el
         * TANK de csnmags: sin tank la candidata no trae magnitudes. */
        {
            QVector<BpqFetchReq> mreq;

            if (m_staCat) {
                for (int i = 0; i < rows.size() && mreq.size() < 24; i++) {
                    const CsnhypodbpStaRow &r = rows.at(i);
                    const char *sta = nullptr, *net = nullptr, *chan = nullptr,
                               *loc = nullptr;

                    if (!r.p.present) continue;
                    if (r.p.origin == 'A' && !am) continue;
                    if (r.p.origin == 'M' && !mm) continue;
                    if (r.p.origin == 'T' && !tm) continue;
                    if (!bpq_stations_scnl(m_staCat, r.idx, &sta, &net, &chan,
                                           &loc)) continue;
                    mreq.append(BpqFetchReq{ QString::fromUtf8(sta),
                                             QString::fromUtf8(net),
                                             QString::fromUtf8(chan),
                                             QString::fromUtf8(loc) });
                }
            }
            magReq = mreq;
        }
        /* Ventana de magnitudes: MagFetchSeconds del .d (Ms necesita su ventana). */
        magT0 = base.otime - 5.0;
        magWin = (m_cfg && m_cfg->mag_fetch_s > 0.0) ? m_cfg->mag_fetch_s : 700.0;

        if (!w) {
            m_candErr = QStringLiteral("worker no disponible");
            statusBar()->showMessage(QStringLiteral("locate: %1").arg(m_candErr), 0);
            return;
        }

        ++m_locJob;
        id = m_locJob;
        m_locBusy = true;
        m_locTimer.start();
        if (m_locTick) m_locTick->start();
        if (m_locBtn) m_locBtn->setEnabled(false);
        if (m_locCancel) m_locCancel->setEnabled(true);
        renderPreview();          /* RUNNING: guiones + "calculando ..." */
        tickLocate();

        QMetaObject::invokeMethod(w, [w, id, base, rows, on, am, mm, tm, fix, loc,
                                      magReq, magT0, magWin]() {
            w->runJob(id, base, rows, on, am, mm, tm, fix, loc, magReq, magT0,
                      magWin);
        }, Qt::QueuedConnection);
    }
}

void BpqMainWindow::cancelLocate()
{
    if (!m_locBusy || !m_locWorker) return;
    /* Directo, no por cola: el worker esta bloqueado dentro de la cadena. */
    m_locWorker->requestCancel();
    statusBar()->showMessage(QStringLiteral("locate: cancelando..."), 0);
}

void BpqMainWindow::tickLocate()
{
    if (!m_locBusy) {
        if (m_locTick) m_locTick->stop();
        return;
    }
    /* El texto "calculando <loc>... N s" vive en el preview, al lado del Origin
     * time (paridad GTK); la barra de estado se queda como eco. */
    renderPreview();
    statusBar()->showMessage(
        QStringLiteral("calculando %1... %2 s")
            .arg(m_locCombo ? m_locCombo->currentText() : QStringLiteral("locate"))
            .arg((qint64)(m_locTimer.elapsed() / 1000)),
        0);
}

void BpqMainWindow::locateFinished(quint64 job, bool ok, BpqLocateOut *out,
                                   const QString &err, double secs)
{
    if (job != m_locJob) {          /* corrida que el usuario dejo atras */
        delete out;
        return;
    }

    m_locBusy = false;
    m_candSecs = secs;
    if (m_locTick) m_locTick->stop();
    if (m_locBtn) m_locBtn->setEnabled(true);
    if (m_locCancel) m_locCancel->setEnabled(false);

    if (ok && out) {
        std::memcpy(&m_cand, &out->sol, sizeof(m_cand));
        std::memcpy(&m_candMag, &out->mag, sizeof(m_candMag));
        m_candValid = true;
        m_candKey = solKey(&m_cand);
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "locate: lat %.4f  lon %.4f  prof %.1f km  nph %d  "
                          "rms %.2f s  (%.0f s)",
                          m_cand.lat, m_cand.lon, m_cand.depth,
                          m_cand.nph, m_cand.rms, secs);
            statusBar()->showMessage(QString::fromUtf8(buf), 0);
        }
        /* Residuales del localizador en la tabla, sin volver a pedir ondas. */
        if (m_curEv) {
            int pi = pinnedSolutionIndex();
            if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
            const CsnhypodbpSolution *sol = (pi >= 0 && pi < m_npts)
                ? bpq_event_sol(m_curEv, m_ptSol[pi]) : nullptr;
            if (sol) {
                rebuildStaTable(sol);
                for (int i = 0; i < m_traceRows.size(); i++) {
                    for (int j = 0; j < m_staRows.size(); j++) {
                        if (std::strcmp(m_traceRows.at(i).sta,
                                        m_staRows.at(j).sta) == 0) {
                            m_traceRows[i] = m_staRows.at(j);
                            break;
                        }
                    }
                }
                updateTracesContext();
                if (m_waves) m_waves->requestRepaint();
            }
        }
    } else {
        m_candErr = err.isEmpty()
                        ? QStringLiteral("sin solucion nueva") : err;
        statusBar()->showMessage(QStringLiteral("locate: %1").arg(m_candErr), 0);
    }
    renderPreview();
    delete out;
}

/* Profundidad fija de la barra: < 0 = libre (el nucleo usa el `Fix` del .d solo
 * si el llamador no la fuerza). */
double BpqMainWindow::fixDepthKm() const
{
    bool ok = false;
    double v;

    if (!m_chkFix || !m_chkFix->isChecked()) return -1.0;
    v = m_editFix ? m_editFix->text().trimmed().toDouble(&ok) : 0.0;
    return (ok && v >= 0.0) ? v : -1.0;
}

/* UNICO camino de pintado del preview: arma los valores con el nucleo puro
 * (csnhypodbp_preview_*) y los vuelca de una vez. Precedencia del GTK:
 * RUNNING > ERROR > CANDIDATA > ELEGIDA > IDLE. */
void BpqMainWindow::renderPreview()
{
    CsnhypodbpPreviewVals v;
    int pi = pinnedSolutionIndex();
    if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
    const QByteArray loc = (m_locCombo && !m_locCombo->currentText().isEmpty())
                               ? m_locCombo->currentText().toUtf8()
                               : QByteArray("csnloc");
    const QString piKey = (m_curEv && pi >= 0 && pi < m_npts)
                              ? solKey(bpq_event_sol(m_curEv, m_ptSol[pi]))
                              : QString();
    const bool cand = m_candValid && !m_candKey.isEmpty() &&
                      m_candKey == piKey;
    int pref;

    if (!m_preview) return;
    std::memset(&v, 0, sizeof(v));

    if (m_locBusy) {
        csnhypodbp_preview_set_busy(&v, loc.constData(),
                                    (long)(m_locTimer.elapsed() / 1000));
    } else if (cand) {
        csnhypodbp_preview_from_candidate(&v, m_cand.otime, loc.constData(),
                                          m_cand.lat, m_cand.lon, m_cand.depth,
                                          m_cand.nph, m_cand.rms,
                                          m_candMag.ml, m_candMag.mwp,
                                          m_candMag.mb, m_candMag.ms, "");
    } else if (!m_candErr.isEmpty()) {
        csnhypodbp_preview_set_error(&v, loc.constData(),
                                     m_candErr.toUtf8().constData());
    } else if (m_curEv && pi >= 0 && pi < m_npts) {
        const int j = m_ptSol[pi];
        const CsnhypodbpSolution *sol = bpq_event_sol(m_curEv, j);
        char res[32] = "", nph[32] = "", ml[32] = "", mwp[32] = "", mb[32] = "",
             ms[32] = "";

        if (sol) {
            bpq_origin_cell(m_curEv, j, 8, res, sizeof(res));
            bpq_origin_cell(m_curEv, j, 9, nph, sizeof(nph));
            bpq_origin_cell(m_curEv, j, 11, ml, sizeof(ml));
            bpq_origin_cell(m_curEv, j, 12, mwp, sizeof(mwp));
            bpq_origin_cell(m_curEv, j, 13, mb, sizeof(mb));
            bpq_origin_cell(m_curEv, j, 14, ms, sizeof(ms));
            csnhypodbp_preview_from_selected(&v, sol->otime, sol->lat, sol->lon,
                                             sol->depth, res, nph,
                                             ml, mwp, mb, ms, "");
        }
    }

    /* Magnitud preferida: la que el usuario marco; si no, la regla de csnmags. */
    if (v.state == CSNHYPODBP_PREVIEW_IDLE && !v.candidate) {
        pref = -1;
    } else if (m_prefMagUser && m_prefMag >= 0) {
        pref = m_prefMag;
    } else {
        pref = csnhypodbp_mag_pref_default(
            v.val[CSNHYPODBP_PREVIEW_NPARAMS + 0],
            v.val[CSNHYPODBP_PREVIEW_NPARAMS + 1],
            v.val[CSNHYPODBP_PREVIEW_NPARAMS + 2],
            v.val[CSNHYPODBP_PREVIEW_NPARAMS + 3]);
        m_prefMag = pref;
    }
    m_preview->render(v, pref);
    m_previewVals = v;
    /* La tabla de magnitudes y su canvas siguen a la misma solucion que el
     * preview (misma fuente de verdad para estado y valores). */
    renderMagnitudes();
}

/* Confirm: el ARC sale de la MISMA funcion pura que usa el modulo GTK
 * (csnhypodbp_confirmation_arc -> arc_build con la marca manual), asi que es
 * byte a byte el de csnhypodbp_confirm_manual. Se publica en ConfRing y se
 * incorpora al store local como snapshot confirmado (sin esperar al anillo). */
void BpqMainWindow::confirmCandidate()
{
    char arc[8192] = "";
    int rc = -2;
    int fed = 0;
    int magn = 0;

    if (!m_candValid || m_locBusy) return;
    if (csnhypodbp_confirmation_arc(&m_cand, arc, sizeof(arc)) != 0) {
        statusBar()->showMessage(
            QStringLiteral("confirm: el ARC de confirmacion no se pudo construir"),
            0);
        return;
    }
    if (m_rings) rc = bpq_emit_confirm(m_rings, &m_cand, arc, sizeof(arc));

    /* Las picadas manuales se publican AHORA -- es el UNICO momento, igual que
     * la GTK: el ARC confirmado ya las lleva, y PICK_RING alimenta a csnloc y a
     * csntvp. Antes del Confirmar no se mando nada al anillo. */
    {
        CsnhypodbpManualPick mp[CSNHYPODBP_MANUAL_MAX];
        const int nmp = bpq_manual_list(mp, CSNHYPODBP_MANUAL_MAX);

        m_lastPubPicks = 0;
        for (int i = 0; i < nmp; i++) {
            const char *sta = nullptr, *net = nullptr, *chan = nullptr, *loc = nullptr;
            int idx = -1;

            for (int j = 0; j < m_staRows.size(); j++)
                if (std::strcmp(m_staRows.at(j).sta, mp[i].sta) == 0) {
                    idx = m_staRows.at(j).idx;
                    break;
                }
            if (idx >= 0 && m_staCat)
                (void)bpq_stations_scnl(m_staCat, idx, &sta, &net, &chan, &loc);
            if (!chan || !chan[0]) chan = mp[i].chan;
            if (m_rings && bpq_emit_pick(m_rings, mp[i].sta, chan,
                                         net ? net : "", loc ? loc : "",
                                         mp[i].phase, mp[i].t, 'M') == 0)
                m_lastPubPicks++;
        }
    }

    {
        CsnhypodbpSolution conf;

        std::memset(&conf, 0, sizeof(conf));
        if (csnhypodbp_parse_solution(arc, std::strlen(arc), 0, &conf) == 0 &&
            m_events) {
            conf.confirmed = 1;      /* snapshot confirmado a mano */
            fed = (bpq_events_feed_arc(m_events, &conf) == 0) ? 1 : 0;
        }
    }

    /* Magnitud de red elegida (radio ML/Mwp/Mb/Ms): entra al origin del store con
     * el algorithm de csnmags ('CSNNet'), que es lo que muestra la maestra. */
    {
        const char *cells[4] = { m_candMag.ml, m_candMag.mwp, m_candMag.mb,
                                 m_candMag.ms };
        const char *names[4] = { "ML", "Mwp", "Mb", "Ms" };
        double val = 0.0;
        int nst = 0;

        if (m_prefMag >= 0 && m_prefMag < 4 && m_events &&
            csnhypodbp_mag_split(cells[m_prefMag], &val, &nst) == 1) {
            BpqMagMsg mm;

            std::memset(&mm, 0, sizeof(mm));
            mm.qid = m_cand.qid;
            mm.mod = m_cand.mod;
            mm.version = m_cand.version;
            mm.value = val;
            mm.nsta = nst;
            std::snprintf(mm.type, sizeof(mm.type), "%s", names[m_prefMag]);
            std::snprintf(mm.algorithm, sizeof(mm.algorithm), "%s", "CSNNet");
            (void)bpq_events_feed_mag(m_events, &mm);
            magn = 1;
        }
    }

    /* 6.4: el Mww y el mecanismo focal de la fase W, si son de ESTA solucion.
     * El `origin` puede existir ya (lo crea hypodb con el pipeline); si no,
     * `bpq_mww_save` lo crea, igual que la GTK al guardar el Mww. */
    {
        char msg[512] = "";
        const QString key = QStringLiteral("%1/%2/%3/%4").arg(m_cand.qid)
                                .arg(m_cand.mod)
                                .arg(m_cand.version)
                                .arg(m_cand.otime, 0, 'f', 3);

        if (m_mww && bpq_mww_tensor_ready(m_mww) && m_mwwFor == key) {
            const int mod = (m_cand.mod > 0)
                                ? m_cand.mod
                                : bpq_mod_for_locator(
                                      m_locCombo ? m_locCombo->currentText()
                                                       .toUtf8()
                                                       .constData()
                                                 : "csnloc");

            m_mwwSaved = bpq_mww_save(m_db, &m_cand, mod, m_mww, msg,
                                      sizeof(msg))
                             ? 1
                             : 0;
            if (!msg[0]) snprintf(msg, sizeof(msg), "sin base de datos");
        } else if (m_mww && bpq_mww_tensor_ready(m_mww)) {
            snprintf(msg, sizeof(msg), "el Mww es de otra solucion (no se guarda)");
        } else {
            snprintf(msg, sizeof(msg), "sin inversion de fase W");
        }
        m_mwwSaveMsg = QString::fromUtf8(msg);
    }

    m_candValid = false;
    m_candKey.clear();
    m_candErr.clear();
    /* Confirmado: las picadas ya viajaron en el ARC y a PICK_RING, asi que el
     * overlay local se limpia (si quedara, seguirian como 'M' sobre la elegida). */
    bpq_manual_clear();
    saveManualPicks();
    refreshFromStore();
    /* El feed puede haber reubicado el store: el puntero al evento se toma del
     * modelo, que es quien acaba de recibir el evento recien seleccionado. */
    m_curEv = m_originsModel ? m_originsModel->currentEvent() : nullptr;
    renderPreview();
    statusBar()->showMessage(
        QStringLiteral("confirm: ARC %1 bytes, ConfRing rc=%2, store=%3, picks=%4, mag=%5, mww=%6 (%7)")
            .arg((int)std::strlen(arc)).arg(rc).arg(fed).arg(m_lastPubPicks)
            .arg(magn).arg(m_mwwSaved).arg(m_mwwSaveMsg),
        0);
}

/* Discard: se descarta la candidata y el cuadro vuelve a la elegida (la tablita
 * ya no se oculta, igual que en la GTK). */
void BpqMainWindow::discardCandidate()
{
    if (!m_candValid) return;
    m_candValid = false;
    m_candKey.clear();
    renderPreview();
    if (m_curEv) {
        int pi = pinnedSolutionIndex();
        if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
        const CsnhypodbpSolution *sol =
            (pi >= 0 && pi < m_npts) ? bpq_event_sol(m_curEv, m_ptSol[pi]) : nullptr;

        if (sol) rebuildStaTable(sol);
    }
    statusBar()->showMessage(
        QStringLiteral("discard: se vuelve a la solucion elegida"), 4000);
}

/* --- magnitudes (fase 6.1) ------------------------------------------------ */

/* Un solo camino de pintado de la sub-pestana: filas (nucleo magrow + ventanas
 * del panel), canvas de la estacion actual y linea de estado. */
void BpqMainWindow::renderMagnitudes()
{
    int n = 0;

    if (!m_magModel) return;
    if (m_magPanel)
        bpq_magpanel_set_masks(m_magPanel,
                               m_chkA ? m_chkA->isChecked() : 1,
                               m_chkM ? m_chkM->isChecked() : 1,
                               m_chkT ? m_chkT->isChecked() : 1);

    if (m_magPanel && !m_staRows.isEmpty()) {
        /* `has_data` = "el calculo tendra su onda": `calculate` baja el tank de
         * las estaciones con P que estan en el catalogo. Sin esto la columna
         * `stat` y el recuento "en uso" dirian que ninguna entra, aunque si
         * entren (el nucleo puro no conoce las trazas en memoria). */
        QVector<CsnhypodbpStaRow> rowsIn = m_staRows;

        for (CsnhypodbpStaRow &r : rowsIn) {
            const char *sta = nullptr, *net = nullptr, *chan = nullptr,
                       *loc = nullptr;

            if (r.p.present && m_staCat &&
                bpq_stations_scnl(m_staCat, r.idx, &sta, &net, &chan, &loc))
                r.has_data = 1;
        }
        m_magRows.resize(rowsIn.size());
        n = bpq_magpanel_rows(m_magPanel, rowsIn.constData(), rowsIn.size(),
                              nullptr, 0, NAN, m_magRows.data(),
                              m_magRows.size());
    }
    m_magRows.resize(n);
    /* Durante el arrastre de la franja no se resetea la tabla: el modelo
     * cambiaria t0/t1 en cada movimiento y perderia la seleccion. Al soltar,
     * m_magDragGrab vuelve a 0 y este mismo camino refresca las celdas. */
    if (m_magDragGrab == 0 && m_magModel->setRows(m_magRows)) {
        reapplySort(m_magProxy);
        fitProcesarTable(m_magView, m_splitMag);
        /* El reset del modelo pierde la fila seleccionada: se restaura por
         * estacion (cambio de magnitud, puerta o solucion). */
        syncStationViews();
    }

    fillMagCtx();
    if (m_magBar) {
        char st[200] = "";

        if (m_magPanel)
            bpq_magpanel_status(m_magRows.constData(), n, st, sizeof(st));
        else
            snprintf(st, sizeof(st), "magnitudes sin configuracion");
        m_magBar->setText(QString::fromUtf8(st));
        m_magBar->setToolTip(QString::fromUtf8(st));
    }
    if (m_magGateLo && m_magGateHi && m_magPanel) {
        char lo[24] = "", hi[24] = "";

        bpq_magpanel_gate_texts(m_magPanel, m_magKind, lo, sizeof(lo), hi,
                                sizeof(hi));
        if (!m_magGateLo->hasFocus()) m_magGateLo->setText(QString::fromUtf8(lo));
        if (!m_magGateHi->hasFocus()) m_magGateHi->setText(QString::fromUtf8(hi));
    }
    /* Semilla del panel Mww con la misma solucion (nunca mientras el motor
     * corre: el objeto lo esta usando el hilo de la inversion). */
    if (!m_mwwBusy) seedMww();
}

/* El canvas de magnitudes sigue a la estacion actual del picking y muestra
 * TODAS sus componentes en el orden del .d, no solo la Z. */
void BpqMainWindow::fillMagCtx()
{
    const CsnhypodbpMagRow *row = nullptr;
    const int nrows = int(qMax(m_traces.size(), m_traceRows.size()));
    int rowIdx = -1, i;

    std::memset(&m_magCtx, 0, sizeof(m_magCtx));
    m_magCtx.magcfg = m_cfg ? &m_cfg->magcfg : nullptr;
    m_magCtx.kind = m_magKind;
    m_magCtx.dpr = m_magCanvas ? m_magCanvas->devicePixelRatioF() : 1.0;
    m_magCtx.y_gain = 1.0;
    m_magStaLabel = m_selSta.toUtf8();
    m_magCtx.sta = m_magStaLabel.isEmpty() ? nullptr : m_magStaLabel.constData();

    for (i = 0; i < m_magRows.size(); i++) {
        if (QString::fromUtf8(m_magRows.at(i).sta) == m_selSta) {
            rowIdx = i;
            break;
        }
    }
    if (rowIdx >= 0) row = &m_magRows.at(rowIdx);
    if (row) {
        m_magCtx.anchor = row->anchor_t;
        m_magCtx.t0 = row->t0;
        m_magCtx.t1 = row->t1;
        m_magCtx.touched = m_magPanel
                               ? bpq_magpanel_touched(m_magPanel, row->sta,
                                                      m_magKind)
                               : 0;
    }

    m_magTrs.clear();
    for (i = 0; i < nrows && i < BPQ_MAG_MAX_TRACES; i++) {
        BpqMagTrace mt;

        mt.tr = (i < m_traces.size()) ? m_traces.at(i) : nullptr;
        mt.chan = (i < m_chanPtr.size()) ? m_chanPtr.at(i) : "";
        mt.dist_km = (i < m_traceRows.size()) ? m_traceRows.at(i).dist_km : NAN;
        m_magTrs.append(mt);
    }
    m_magCtx.trs = m_magTrs.isEmpty() ? nullptr : m_magTrs.constData();
    m_magCtx.ntr = int(m_magTrs.size());

    /* Durante el arrastre el encuadre queda CONGELADO: si se recalculara solo,
     * la escala cambiaria bajo el puntero y el arrastre no seria lineal. */
    if (m_magDragGrab) {
        m_magCtx.view_t0 = m_magViewT0;
        m_magCtx.view_span = m_magViewSpan;
    }
    if (m_magCanvas) {
        m_magCanvas->setContext(&m_magCtx);
        m_magCanvas->requestRepaint();
    }
    if (m_magAxis) {
        m_magAxis->setContext(&m_magCtx);
        m_magAxis->requestRepaint();
    }
}

void BpqMainWindow::setMagKind(int kind)
{
    if (kind < 0 || kind >= BPQ_MAG_KINDS) return;
    m_magKind = kind;
    if (m_magPanel) bpq_magpanel_set_kind(m_magPanel, kind);
    if (kind < m_magRadios.size() && !m_magRadios.at(kind)->isChecked())
        m_magRadios.at(kind)->setChecked(true);
    renderMagnitudes();
}

/* Cajas de la puerta de distancia: se aplican al confirmar el campo y solo si
 * las dos valen; si no, se repone lo vigente (nunca a medias). */
void BpqMainWindow::applyMagGate()
{
    char lo[24] = "", hi[24] = "";

    if (!m_magPanel || !m_magGateLo || !m_magGateHi) return;
    if (!bpq_magpanel_gate_set(m_magPanel,
                               m_magGateLo->text().toUtf8().constData(),
                               m_magGateHi->text().toUtf8().constData())) {
        statusBar()->showMessage(
            QStringLiteral("magnitudes: distancia invalida (no se aplica)"), 4000);
    }
    bpq_magpanel_gate_texts(m_magPanel, m_magKind, lo, sizeof(lo), hi, sizeof(hi));
    m_magGateLo->setText(QString::fromUtf8(lo));
    m_magGateHi->setText(QString::fromUtf8(hi));
    renderMagnitudes();
}

/* `calculate`: vuelve a correr csnmags con las ventanas ajustadas a mano sobre
 * la CANDIDATA (no toca la localizacion). `allKinds` = las cuatro. */
void BpqMainWindow::calculateMagnitudes(bool allKinds)
{
    QVector<BpqFetchReq> req;
    QVector<CsnhypodbpMagOvrRow> ovr;
    CsnhypodbpMagOvrRow buf[600];
    const unsigned kindMask = allKinds ? 0u : (1u << m_magKind);
    double t0 = 0.0, win = 700.0;
    int nm = 0, i;

    if (!m_magPanel || !m_locWorker || !m_cfg) return;
    if (m_magBusy) {
        statusBar()->showMessage(
            QStringLiteral("magnitudes: ya hay un calculo en curso"), 3000);
        return;
    }
    if (!m_candValid) {
        statusBar()->showMessage(
            QStringLiteral("magnitudes: primero `locate` (no hay candidata)"), 0);
        return;
    }

    if (m_staCat) {
        for (i = 0; i < m_staRows.size() && req.size() < 24; i++) {
            const CsnhypodbpStaRow &r = m_staRows.at(i);
            const char *sta = nullptr, *net = nullptr, *chan = nullptr,
                       *loc = nullptr;

            if (!r.p.present) continue;
            if (r.p.origin == 'A' && m_chkA && !m_chkA->isChecked()) continue;
            if (r.p.origin == 'M' && m_chkM && !m_chkM->isChecked()) continue;
            if (r.p.origin == 'T' && m_chkT && !m_chkT->isChecked()) continue;
            if (!bpq_stations_scnl(m_staCat, r.idx, &sta, &net, &chan, &loc))
                continue;
            req.append(BpqFetchReq{ QString::fromUtf8(sta),
                                    QString::fromUtf8(net),
                                    QString::fromUtf8(chan),
                                    QString::fromUtf8(loc) });
        }
    }
    nm = bpq_magpanel_ovr(m_magPanel, m_staRows.constData(), m_staRows.size(),
                          kindMask, buf, 600);
    for (i = 0; i < nm; i++) ovr.append(buf[i]);

    t0 = m_cand.otime - 5.0;
    win = (m_cfg->mag_fetch_s > 0.0) ? m_cfg->mag_fetch_s : 700.0;

    m_magBusy = true;
    const quint64 id = ++m_magJob;
    if (m_magCalc) m_magCalc->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("magnitudes: calculando…"), 0);

    BpqLocateWorker *w = m_locWorker;
    const CsnhypodbpSolution base = m_cand;
    const int km = (int)kindMask;
    QMetaObject::invokeMethod(w, [w, id, base, req, t0, win, ovr, km]() {
        w->runMagJob(id, base, req, t0, win, ovr, km);
    }, Qt::QueuedConnection);
}

void BpqMainWindow::magFinished(quint64 job, bool ok, CsnhypodbpMagOut *out,
                                const QString &err, double secs)
{
    if (job != m_magJob) {
        delete out;
        return;
    }
    m_magBusy = false;
    if (m_magCalc) m_magCalc->setEnabled(true);

    if (ok && out && bpq_mag_result_ok(out)) {
        std::memcpy(&m_candMag, out, sizeof(m_candMag));
        statusBar()->showMessage(
            QStringLiteral("magnitudes (%.1f s): ML %1 | Mwp %2 | Mb %3 | Ms %4")
                .arg(QString::fromUtf8(m_candMag.ml))
                .arg(QString::fromUtf8(m_candMag.mwp))
                .arg(QString::fromUtf8(m_candMag.mb))
                .arg(QString::fromUtf8(m_candMag.ms))
                .arg(secs),
            0);
        renderPreview();
    } else {
        statusBar()->showMessage(
            QStringLiteral("magnitudes: %1")
                .arg(err.isEmpty() ? QStringLiteral("sin resultado") : err),
            0);
    }
    delete out;
    renderMagnitudes();
}

void BpqMainWindow::magPress(double x, double y)
{
    const qreal dpr = m_magCanvas ? m_magCanvas->devicePixelRatioF() : 1.0;
    int grab = 0;
    double t = 0.0;

    m_magDragGrab = 0;
    if (!m_magPanel || !m_magCanvas || !m_magCtx.sta) return;
    if (!bpq_mag_hit(&m_magCtx, int(m_magCanvas->width() * dpr),
                     int(m_magCanvas->height() * dpr), x * dpr, y * dpr,
                     &grab, &t))
        return;

    /* Congela el encuadre y la escala del dibujo antes de mover nada. */
    {
        const double m = bpq_mag_margin(m_magCtx.t0, m_magCtx.t1);

        m_magViewT0 = m_magCtx.anchor + m_magCtx.t0 - m;
        m_magViewSpan = (m_magCtx.t1 - m_magCtx.t0) + 2.0 * m;
    }
    m_magDragGrab = grab;
    m_magDragX = x * dpr;
    m_magDragPressT = t;
    m_magDragT0 = m_magCtx.t0;
    m_magDragT1 = m_magCtx.t1;
}

void BpqMainWindow::magDragMove(double dx, double dy)
{
    const qreal dpr = m_magCanvas ? m_magCanvas->devicePixelRatioF() : 1.0;
    double t, dt, t0, t1;

    (void)dy;
    if (!m_magDragGrab || !m_magPanel || !m_magCanvas || !m_magCtx.sta) return;
    m_magDragX += dx * dpr;
    t = bpq_mag_time_at(&m_magCtx, int(m_magCanvas->width() * dpr),
                        int(m_magCanvas->height() * dpr), m_magDragX);
    if (!std::isfinite(t) || t <= 0.0) return;
    dt = t - m_magDragPressT;
    t0 = m_magDragT0;
    t1 = m_magDragT1;
    if (m_magDragGrab == 1) t0 += dt;
    else if (m_magDragGrab == 2) t1 += dt;
    else { t0 += dt; t1 += dt; }
    if (!(t1 - t0 >= 1.0)) return;      /* no se colapsa la ventana */
    bpq_magpanel_set_window(m_magPanel, m_magCtx.sta, m_magKind, t0, t1);
    renderMagnitudes();
}

void BpqMainWindow::magRelease()
{
    if (!m_magDragGrab) return;
    m_magDragGrab = 0;
    m_magViewT0 = 0.0;
    m_magViewSpan = 0.0;
    renderMagnitudes();
}

/* --- panel Mww (fase 6.3) ------------------------------------------------ */

/* Semilla del panel: solucion (candidata si hay, si no la elegida) + magnitud.
 * Solo se re-siembra cuando cambia la identidad o la magnitud: asi las celdas
 * editadas por el usuario sobreviven a los repintados del panel. */
void BpqMainWindow::seedMww()
{
    int pi = pinnedSolutionIndex();
    if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
    const QString piKey = (m_curEv && pi >= 0 && pi < m_npts)
                              ? solKey(bpq_event_sol(m_curEv, m_ptSol[pi]))
                              : QString();
    const bool cand = m_candValid && !m_candKey.isEmpty() &&
                      m_candKey == piKey;
    const CsnhypodbpSolution *sol = cand ? &m_cand : currentSolution();
    const char *cells[4] = { "", "", "", "" };
    char ml[32] = "", mwp[32] = "", mb[32] = "", ms[32] = "";
    double mag = NAN;
    QString key;

    if (!m_mww || !m_mwwPanel) return;
    if (!sol) {
        m_mwwPanel->refresh();
        return;
    }

    if (cand) {
        cells[0] = m_candMag.ml;
        cells[1] = m_candMag.mwp;
        cells[2] = m_candMag.mb;
        cells[3] = m_candMag.ms;
    } else if (m_curEv && pi >= 0 && pi < m_npts) {
        const int j = m_ptSol[pi];

        bpq_origin_cell(m_curEv, j, 11, ml, sizeof(ml));
        bpq_origin_cell(m_curEv, j, 12, mwp, sizeof(mwp));
        bpq_origin_cell(m_curEv, j, 13, mb, sizeof(mb));
        bpq_origin_cell(m_curEv, j, 14, ms, sizeof(ms));
        cells[0] = ml;
        cells[1] = mwp;
        cells[2] = mb;
        cells[3] = ms;
    }

    /* Magnitud semilla: la del radio si tiene valor; si no Mwp > Ms > Mb > ML.
     * `BPQ_MWW_MAG` la fuerza (diagnostico: soluciones de la BD sin magnitud). */
    {
        const int order[4] = { 1, 3, 2, 0 };
        double v = 0.0;
        int nst = 0, k;

        if (m_prefMag >= 0 && m_prefMag < 4 &&
            csnhypodbp_mag_split(cells[m_prefMag], &v, &nst) == 1) {
            mag = v;
        } else {
            for (k = 0; k < 4; k++) {
                if (csnhypodbp_mag_split(cells[order[k]], &v, &nst) == 1) {
                    mag = v;
                    break;
                }
            }
        }
    }
    if (!std::isfinite(mag)) {
        const char *forced = std::getenv("BPQ_MWW_MAG");

        if (forced && forced[0]) mag = std::atof(forced);
    }

    key = QStringLiteral("%1/%2/%3/%4/%5")
              .arg(sol->qid)
              .arg(sol->mod)
              .arg(sol->version)
              .arg(sol->otime, 0, 'f', 3)
              .arg(std::isfinite(mag) ? mag : -1.0, 0, 'f', 2);
    if (key == m_mwwKey) {
        m_mwwPanel->refresh();
        return;
    }

    {
        char err[256] = "";

        if (bpq_mww_seed(m_mww, sol, mag, err, sizeof(err))) {
            m_mwwKey = key;
            m_mwwFor = QStringLiteral("%1/%2/%3/%4").arg(sol->qid).arg(sol->mod)
                           .arg(sol->version).arg(sol->otime, 0, 'f', 3);
            m_mwwPanel->refresh();
            m_mwwPanel->setStatus(
                QStringLiteral("Mww: semilla E%1 v%2 (Mw %3) — pulsa Calculate")
                    .arg(sol->qid)
                    .arg(sol->version)
                    .arg(std::isfinite(mag) ? QString::number(mag, 'f', 2)
                                            : QStringLiteral("-")),
                false);
        } else {
            m_mwwKey.clear();
            m_mwwPanel->refresh();
            m_mwwPanel->setStatus(
                QStringLiteral("Mww: %1").arg(QString::fromUtf8(err)), true);
        }
    }
}

/* Calculate: la corrida va al hilo del motor (bloqueante y de minutos). */
void BpqMainWindow::startMww()
{
    if (!m_mww || !m_mwwWorker || m_mwwBusy) return;
    if (!bpq_mww_seeded(m_mww)) {
        statusBar()->showMessage(
            QStringLiteral("Mww: falta la semilla (solucion + magnitud)"), 0);
        return;
    }
    m_mwwBusy = true;
    const quint64 id = ++m_mwwJob;
    if (m_mwwPanel) {
        m_mwwPanel->setBusy(true);
        m_mwwPanel->setStatus(
            QStringLiteral("corriendo csnwphase… (prep + motor; puede tardar "
                           "minutos)"),
            false);
    }
    BpqMwwWorker *w = m_mwwWorker;
    BpqMww *m = m_mww;
    QMetaObject::invokeMethod(w, [w, id, m]() { w->runJob(id, m, 0); },
                              Qt::QueuedConnection);
}

/* Cancel: el motor no expone cancelacion, asi que se DESCARTA el resultado en
 * vuelo (el hilo termina solo). La tabla queda deshabilitada mientras corre. */
void BpqMainWindow::cancelMww()
{
    if (!m_mwwBusy) return;
    ++m_mwwJob;
    m_mwwBusy = false;
    if (m_mwwPanel) {
        m_mwwPanel->setBusy(false);
        m_mwwPanel->setStatus(
            QStringLiteral("cancelado: resultado descartado (el motor sigue "
                           "hasta terminar)"),
            true);
    }
}

void BpqMainWindow::mwwFinished(quint64 job, bool ok, const QString &err,
                                double secs)
{
    if (job != m_mwwJob) return;      /* cancelado: se descarta */
    m_mwwBusy = false;
    if (m_mwwPanel) {
        m_mwwPanel->setBusy(false);
        m_mwwPanel->refresh();
        if (ok) {
            const QString first = QString::fromUtf8(bpq_mww_summary(m_mww))
                                      .section(QLatin1Char('\n'), 0, 0);

            m_mwwPanel->setStatus(
                QStringLiteral("%1  —  %2 (%3 s)")
                    .arg(first)
                    .arg(QString::fromUtf8(bpq_mww_rundir(m_mww)))
                    .arg(secs, 0, 'f', 0),
                false);
        } else {
            m_mwwPanel->setStatus(
                QStringLiteral("sin solucion: %1")
                    .arg(err.isEmpty() ? QStringLiteral("ver el log de la corrida")
                                       : err),
                true);
        }
    }
    statusBar()->showMessage(
        ok ? QStringLiteral("Mww: %1").arg(QString::fromUtf8(bpq_mww_summary(m_mww))
                                               .section(QLatin1Char('\n'), 0, 0))
           : QStringLiteral("Mww sin solucion: %1").arg(err),
        0);
}

/* Hook headless (BPQ_MODE=--locate): corre el mismo camino que el boton sobre
 * la solucion elegida y vuelca el resultado. Las mascaras salen de
 * BPQ_LOCATE_MASK ("AMT"); por defecto las tres, para que un evento historico
 * (todo automatico) no se quede en "sin picadas". */
bool BpqMainWindow::runLocateForTest()
{
    const char *m = std::getenv("BPQ_LOCATE_MASK");
    const QString mask = QString::fromUtf8(m && m[0] ? m : "AMT").toUpper();
    const char *l = std::getenv("BPQ_LOCATE_LOC");

    if (m_chkA) m_chkA->setChecked(mask.contains(QLatin1Char('A')));
    if (m_chkM) m_chkM->setChecked(mask.contains(QLatin1Char('M')));
    if (m_chkT) m_chkT->setChecked(mask.contains(QLatin1Char('T')));
    if (l && l[0] && m_locCombo) {
        const int idx = m_locCombo->findText(QString::fromUtf8(l));

        if (idx >= 0) m_locCombo->setCurrentIndex(idx);
    }

    locateNow();
    while (m_locBusy) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }

    if (!m_candValid) {
        std::printf("locate=0 err=\"%s\"\n", m_candErr.toUtf8().constData());
        std::fflush(stdout);
        return false;
    }

    std::printf("locate=1 secs=%.1f qid=%d ver=%u mod=%d lat=%.4f lon=%.4f "
                "depth=%.1f nph=%d rms=%.3f npicks=%d\n",
                m_candSecs, m_cand.qid, m_cand.version, m_cand.mod,
                m_cand.lat, m_cand.lon, m_cand.depth, m_cand.nph, m_cand.rms,
                m_cand.npicks);
    {
        int res = 0, np = 0, ns = 0, i;
        double rmax = 0.0;

        for (i = 0; i < m_staRows.size(); i++)
            if (std::isfinite(m_staRows.at(i).p.resid) ||
                std::isfinite(m_staRows.at(i).s.resid)) res++;
        for (i = 0; i < m_cand.npicks; i++) {
            const CsnhypodbpPick *pk = &m_cand.picks[i];

            if (pk->phase == 'P') np++;
            else ns++;
            if (std::isfinite(pk->residual) &&
                std::fabs(pk->residual) > rmax) rmax = std::fabs(pk->residual);
        }
        std::printf("stations rows=%d res=%d\n", (int)m_staRows.size(), res);
        std::printf("picks P=%d S=%d max|res|=%.2f\n", np, ns, rmax);
    }
    std::fflush(stdout);
    return true;
}

/* Hook headless (BPQ_MODE=--mww): semilla + tabla + corrida. Por defecto en
 * DRY-RUN (prep + run.sh, sin motor: la inversion real tarda minutos y necesita
 * GfDir); `BPQ_MWW_REAL=1` la corre de verdad. */
bool BpqMainWindow::runMwwForTest()
{
    const char *real = std::getenv("BPQ_MWW_REAL");
    const int dry = !(real && real[0] == '1');
    BpqMwwParamRow rows[32];
    BpqMwwResult *res;
    char err[512] = "";
    int n, i, editable = 0;

    if (!runLocateForTest())
        std::printf("mww locate=0 (semilla desde la solucion elegida)\n");
    renderMagnitudes();          /* refresca filas y siembra el Mww */
    seedMww();
    if (!m_mww || !bpq_mww_seeded(m_mww)) {
        std::printf("mww=0 err=\"sin semilla (solucion o magnitud)\"\n");
        std::fflush(stdout);
        return false;
    }

    n = bpq_mww_rows(m_mww, rows, 32);
    for (i = 0; i < n; i++)
        if (rows[i].field != 0) editable++;
    std::printf("mww seed=1 rows=%d editable=%d rundir=%s\n", n, editable,
                bpq_mww_rundir(m_mww));
    for (i = 0; i < n; i++)
        std::printf("mww param %s = %s (%s)\n", rows[i].name, rows[i].value,
                    rows[i].source);

    res = bpq_mww_run(m_mww, dry, err, sizeof(err));
    std::printf("mww run=%s dry=%d err=\"%s\"\n", res ? "ok" : "fail", dry,
                err[0] ? err : "-");
    if (res && bpq_mww_result_ok(res)) {
        const QString s = QString::fromUtf8(bpq_mww_summary(m_mww));
        const QStringList lines = s.split(QLatin1Char('\n'));

        for (const QString &ln : lines)
            std::printf("mww summary %s\n", ln.toUtf8().constData());
    }
    std::printf("mww log_bytes=%d tensor_ready=%d\n",
                (int)std::strlen(bpq_mww_log(m_mww)),
                bpq_mww_tensor_ready(m_mww));

    /* Mismo guardado que hace el Confirm (6.4), para poder verificarlo headless. */
    {
        const char *save = std::getenv("BPQ_MWW_SAVE");
        char smsg[512] = "";
        int pi = pinnedSolutionIndex();
        if (pi < 0) pi = (m_mapCtx.selected >= 0) ? m_mapCtx.selected : m_pref;
        const QString piKey = (m_curEv && pi >= 0 && pi < m_npts)
                                  ? solKey(bpq_event_sol(m_curEv, m_ptSol[pi]))
                                  : QString();
        const CsnhypodbpSolution *sol =
            (m_candValid && !m_candKey.isEmpty() && m_candKey == piKey)
                ? &m_cand
                : currentSolution();

        if (save && save[0] == '1' && sol && bpq_mww_tensor_ready(m_mww)) {
            const int mod = (sol->mod > 0)
                                ? sol->mod
                                : bpq_mod_for_locator("csnloc");
            const int ok = bpq_mww_save(m_db, sol, mod, m_mww, smsg,
                                        sizeof(smsg));

            std::printf("mww save=%d mod=%d msg=\"%s\"\n", ok, mod, smsg);
            std::fflush(stdout);
        }
    }
    std::fflush(stdout);
    /* Un dry-run "falla" en el parseo (no hay wphase.json) pero deja el motivo
     * real: eso es exito del hook. */
    return res != NULL || err[0] != '\0';
}

/* Hook headless (BPQ_MODE=--magcalc): locate + boton `calculate`. Corre el mismo
 * camino que el clic (tank + csnmags con las ventanas ajustadas) y espera el
 * resultado girando el bucle, porque el trabajo va al hilo del localizador. */
bool BpqMainWindow::runMagCalcForTest()
{
    QElapsedTimer t;

    if (!runLocateForTest()) return false;

    /* Ajusta una ventana a mano para que el override viaje de verdad. */
    if (m_magPanel && !m_magRows.isEmpty()) {
        const CsnhypodbpMagRow *r0 = &m_magRows.first();
        double t0 = r0->t0, t1 = r0->t1;

        if (std::isfinite(t0) && std::isfinite(t1) && t1 > t0 + 2.0) {
            bpq_magpanel_set_window(m_magPanel, r0->sta, m_magKind, t0, t1 - 1.0);
            renderMagnitudes();
            std::printf("magdrag sta=%s kind=%d win=[%.0f,%.0f] touched=%d\n",
                        r0->sta, m_magKind, t0, t1 - 1.0,
                        bpq_magpanel_touched_count(m_magPanel));
        }
    }

    calculateMagnitudes(false);
    t.start();
    while (m_magBusy && t.elapsed() < 240000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    std::printf("magcalc busy=%d ml='%s' mwp='%s' mb='%s' ms='%s'\n",
                m_magBusy ? 1 : 0, m_candMag.ml, m_candMag.mwp, m_candMag.mb,
                m_candMag.ms);
    {
        const QString d = dumpTree();

        for (const QString &ln : d.split(QLatin1Char('\n')))
            if (ln.startsWith(QStringLiteral("mag ")))
                std::printf("%s\n", ln.toUtf8().constData());
    }
    std::fflush(stdout);
    return m_candValid && !m_magBusy;
}

/* Hook headless (BPQ_MODE=--confirm): parte de una candidata ya calculada
 * (`--locate`) y confirma, para ver el ARC y la publicacion sin clics. */
bool BpqMainWindow::runConfirmForTest()
{
    char arc[8192] = "";
    int built;

    if (!m_candValid) {
        std::printf("confirm=0 err=\"sin candidata\"\n");
        std::fflush(stdout);
        return false;
    }
    built = csnhypodbp_confirmation_arc(&m_cand, arc, sizeof(arc));
    /* Magnitud preferida elegida por radio; el hook la puede forzar con
     * BPQ_PREF_MAG=ML|Mwp|Mb|Ms|Mww (la regla de csnmags a veces no prefiere
     * ninguna, p.ej. Mwp < 5.5). */
    {
        const char *pm = std::getenv("BPQ_PREF_MAG");
        const char *names[5] = { "ML", "Mwp", "Mb", "Ms", "Mww" };

        if (pm && pm[0]) {
            for (int k = 0; k < 5; k++) {
                if (strcasecmp(pm, names[k]) != 0) continue;
                m_prefMag = k;
                m_prefMagUser = true;
                break;
            }
        }
    }
    std::printf("confirm arc_build=%d len=%d qid=%d ver=%u nph=%d\n",
                built, (int)std::strlen(arc), m_cand.qid, m_cand.version,
                m_cand.nph);
    std::printf("confirm mags ml='%s' mwp='%s' mb='%s' ms='%s' pref=%d\n",
                m_candMag.ml, m_candMag.mwp, m_candMag.mb, m_candMag.ms,
                m_prefMag);
    {
        /* Marca manual en el byte 174 y localizador en 182..194 del ARC. */
        const size_t n = std::strlen(arc);

        std::printf("confirm arc_mark=%c vmodel=%s\n",
                    n > 174 ? arc[174] : '?',
                    n > 194 ? (arc + 182) : "-");
    }
    confirmCandidate();
    std::printf("confirm=1 origins=%d manual=%d picks_published=%d\n",
                m_originsModel ? m_originsModel->rowCount() : 0,
                bpq_manual_count(), m_lastPubPicks);
    std::printf("confirm mww_save=%d msg=\"%s\"\n", m_mwwSaved,
                m_mwwSaveMsg.toUtf8().constData());
    std::fflush(stdout);
    return true;
}

/* Hook headless (BPQ_MODE=--mouse): manda eventos REALES de raton a los tres
 * canvas. Es la prueba de que el camino widget -> señal -> contexto funciona sin
 * que nadie haga clic: rueda y arrastre en planta, perfil y trazas, y clic para
 * picar. */
bool BpqMainWindow::runMouseForTest()
{
    const double mapZoom0 = m_mapCtx.zoom, mapPanX0 = m_mapCtx.pan_x;
    const double profZoom0 = m_profCtx.zoom;
    const double win0 = m_winLen, t0_0 = m_winT0;
    int npicked = 0, man0 = bpq_manual_count();
    QString firstSta;
    double firstT = 0.0;
    char firstPh = 0;

    const auto sendWheel = [](BpqCanvasWidget *w, int dy) {
        const QPointF pos(w->width() / 2.0, w->height() / 2.0);
        QWheelEvent ev(pos, w->mapToGlobal(pos.toPoint()), QPoint(),
                       QPoint(0, dy), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);

        QCoreApplication::sendEvent(w, &ev);
    };
    const auto sendDrag = [](BpqCanvasWidget *w, int dx) {
        const QPointF p0(w->width() / 2.0, w->height() / 2.0);
        const QPointF p1 = p0 + QPointF(dx, 0);
        QMouseEvent press(QEvent::MouseButtonPress, p0,
                          w->mapToGlobal(p0.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, p1, w->mapToGlobal(p1.toPoint()),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent rel(QEvent::MouseButtonRelease, p1,
                        w->mapToGlobal(p1.toPoint()), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);

        QCoreApplication::sendEvent(w, &press);
        QCoreApplication::sendEvent(w, &move);
        QCoreApplication::sendEvent(w, &rel);
    };
    const auto sendClick = [](BpqCanvasWidget *w, double fx, double fy) {
        const QPointF p(w->width() * fx, w->height() * fy);
        QMouseEvent press(QEvent::MouseButtonPress, p,
                          w->mapToGlobal(p.toPoint()), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent rel(QEvent::MouseButtonRelease, p,
                        w->mapToGlobal(p.toPoint()), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);

        QCoreApplication::sendEvent(w, &press);
        QCoreApplication::sendEvent(w, &rel);
    };
    const auto sendRightClick = [](BpqCanvasWidget *w, double fx, double fy) {
        const QPointF p(w->width() * fx, w->height() * fy);
        QMouseEvent press(QEvent::MouseButtonPress, p,
                          w->mapToGlobal(p.toPoint()), Qt::RightButton,
                          Qt::RightButton, Qt::NoModifier);
        QMouseEvent rel(QEvent::MouseButtonRelease, p,
                        w->mapToGlobal(p.toPoint()), Qt::RightButton,
                        Qt::NoButton, Qt::NoModifier);

        QCoreApplication::sendEvent(w, &press);
        QCoreApplication::sendEvent(w, &rel);
    };
    const auto sendKey = [](BpqCanvasWidget *w, int key) {
        QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);

        QCoreApplication::sendEvent(w, &ev);
    };
    const auto sendKeyShift = [](BpqCanvasWidget *w, int key, bool shift) {
        QKeyEvent ev(QEvent::KeyPress, key,
                     shift ? Qt::ShiftModifier : Qt::NoModifier);

        QCoreApplication::sendEvent(w, &ev);
    };
    const auto sendKeyTo = [](QWidget *w, int key) {
        QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);

        QCoreApplication::sendEvent(w, &ev);
    };

    if (m_mapPlan) {
        sendWheel(m_mapPlan, 120);
        std::printf("mouse map wheel zoom %.6f -> %.6f\n", mapZoom0, m_mapCtx.zoom);
        sendDrag(m_mapPlan, 40);
        std::printf("mouse map drag pan_x %.6f -> %.6f\n", mapPanX0,
                    m_mapCtx.pan_x);
    }
    if (m_mapProf) {
        sendWheel(m_mapProf, 120);
        std::printf("mouse prof wheel zoom %.6f -> %.6f\n", profZoom0,
                    m_profCtx.zoom);
    }
    if (m_waves) {
        sendWheel(m_waves, 120);
        std::printf("mouse traces wheel win %.1f -> %.1f (t0 %.2f -> %.2f)\n",
                    win0, m_winLen, t0_0, m_winT0);
        {
            const double before = m_winT0;

            sendDrag(m_waves, 60);
            std::printf("mouse traces drag t0 %.2f -> %.2f\n", before, m_winT0);
        }
        /* Modo picada: la tecla P arma la fase (cursor = circulo), el clic pica y
         * el clic derecho quita. Esc desarma. */
        sendKey(m_waves, Qt::Key_P);
        std::printf("mouse pick_armed=%d phase=%c\n", m_pickArmed ? 1 : 0,
                    m_pickPhase);
        sendClick(m_waves, 0.60, 0.08);
        npicked = bpq_manual_count() - man0;
        {
            CsnhypodbpManualPick v[CSNHYPODBP_MANUAL_MAX];
            const int n = bpq_manual_list(v, CSNHYPODBP_MANUAL_MAX);

            if (n > 0) {
                firstSta = QString::fromUtf8(v[n - 1].sta);
                firstPh = v[n - 1].phase;
                firstT = v[n - 1].t;
            }
        }
        std::printf("mouse pick n=%d row0=%s %c t=%.3f\n", npicked,
                    firstSta.toUtf8().constData(), firstPh, firstT);

        sendRightClick(m_waves, 0.60, 0.08);
        std::printf("mouse right_click manual=%d\n", bpq_manual_count());
        sendClick(m_waves, 0.60, 0.08);   /* se deja puesta para la captura */
        std::printf("mouse pick_again manual=%d\n", bpq_manual_count());

        sendKey(m_waves, Qt::Key_Escape);
        std::printf("mouse esc armed=%d\n", m_pickArmed ? 1 : 0);

        /* Dos clics: izquierda->derecha acerca; al reves deshace. */
        {
            const double w0 = m_winLen;

            sendClick(m_waves, 0.30, 0.30);
            sendClick(m_waves, 0.70, 0.30);
            std::printf("mouse zoom2 win %.1f -> %.1f\n", w0, m_winLen);
            sendClick(m_waves, 0.70, 0.30);
            sendClick(m_waves, 0.30, 0.30);
            std::printf("mouse zoom2_undo win -> %.1f\n", m_winLen);
        }
        /* Arrastre en el eje: derecha acerca, izquierda aleja. */
        if (m_axis) {
            const double w0 = m_winLen;

            sendDrag(m_axis, 80);
            std::printf("mouse axis_drag+ win %.1f -> %.1f\n", w0, m_winLen);
            sendDrag(m_axis, -80);
            std::printf("mouse axis_drag- win -> %.1f\n", m_winLen);
        }
        /* Shift+flechas (exageracion vertical y span) y flecha sola (canal). */
        {
            const double g0 = (m_tracesCtx.y_gain > 0.0) ? m_tracesCtx.y_gain : 1.0;
            const double w0 = m_winLen;

            sendKeyShift(m_waves, Qt::Key_Up, true);
            std::printf("mouse shift_up gain %.2f -> %.2f\n", g0,
                        m_tracesCtx.y_gain);
            sendKeyShift(m_waves, Qt::Key_Right, true);
            std::printf("mouse shift_right win %.1f -> %.1f\n", w0, m_winLen);
            sendKeyShift(m_waves, Qt::Key_Down, false);
            std::printf("mouse down scroll=%d nchan=%d per_screen=%d\n",
                        m_tracesCtx.scroll_row, (int)m_traceRows.size(),
                        m_tracesCtx.per_screen);
            if (!m_chanLabels.isEmpty())
                std::printf("mouse chan0=%s\n",
                            m_chanLabels.at(0).constData());
        }
        /* Atajos de filtro (4.3): llegan a la VENTANA y solo cuentan en picking.
         * La segunda pulsacion de la misma tecla apaga el filtro (vuelve a Raw). */
        {
            const int f0 = m_filterIdx;

            sendKeyTo(this, Qt::Key_1);
            std::printf("mouse filter_key1 %d -> %d\n", f0, m_filterIdx);
            sendKeyTo(this, Qt::Key_1);
            std::printf("mouse filter_key1_again -> %d\n", m_filterIdx);
            sendKeyTo(this, Qt::Key_T);
            std::printf("mouse filter_key_t stalta=%d\n", m_staltaOn ? 1 : 0);
        }
        /* Persistencia del overlay: guardar, vaciar y recargar con la MISMA
         * clave de evento (lo que hace el arranque). */
        {
            const int before = bpq_manual_count();
            const QString key = m_manKey;

            saveManualPicks();
            bpq_manual_clear();
            (void)bpq_manual_load(m_manualPath.toUtf8().constData(),
                                  key.toUtf8().constData());
            std::printf("mouse pick save_reload=%d/%d key=%s\n",
                        bpq_manual_count(), before, key.toUtf8().constData());
        }
    }
    std::fflush(stdout);
    return npicked > 0;
}

/* Hook headless (BPQ_MODE=--input): manda UNA tecla o una rueda a un widget
 * concreto. Sirve para probar el enrutado de teclado con el foco en la tabla,
 * sin xdotool. */
bool BpqMainWindow::runInputForTest()
{
    const QByteArray target = qgetenv("BPQ_INPUT_TARGET").toLower();
    const QByteArray mods = qgetenv("BPQ_INPUT_MODS").toLower();
    Qt::KeyboardModifiers km = Qt::NoModifier;
    QWidget *w = nullptr;

    if (target == QByteArray("sta")) w = m_staView;
    else if (target == QByteArray("waves")) w = m_waves;
    else if (target == QByteArray("mag")) w = m_magCanvas;
    else if (target == QByteArray("text")) w = m_editFix;
    if (!w) {
        std::fprintf(stderr, "csnhypodbpQT: BPQ_INPUT_TARGET desconocido\n");
        return false;
    }
    if (mods.contains("shift")) km |= Qt::ShiftModifier;
    if (mods.contains("ctrl")) km |= Qt::ControlModifier;
    /* La caja de profundidad fija nace deshabilitada si Fix esta apagado; para
     * probar el guardia de "foco en texto" hace falta que acepte la tecla. */
    if (target == QByteArray("text") && m_editFix) m_editFix->setEnabled(true);
    if (target == QByteArray("text")) QApplication::setActiveWindow(this);
    w->setFocus(Qt::OtherFocusReason);

    /* Seleccion programatica de una estacion (misma que un clic): dispara el
     * slot de la vista, no solo pinta la fila. `BPQ_INPUT_REFRESH=1` fuerza
     * ademas el refresco de magnitudes para comprobar que no pierde la fila. */
    {
        const QByteArray sel = qgetenv("BPQ_INPUT_SELECT");

        if (!sel.isEmpty()) {
            int vr = -1;
            QTableView *tv = nullptr;

            if (target == QByteArray("sta")) {
                for (int i = 0; i < m_staRows.size(); i++) {
                    if (sel == m_staRows.at(i).sta) {
                        vr = viewRowForSource(m_staProxy, i * 2);
                        break;
                    }
                }
                tv = m_staView;
            } else if (target == QByteArray("mag")) {
                for (int i = 0; i < m_magRows.size(); i++) {
                    if (sel == m_magRows.at(i).sta) {
                        vr = viewRowForSource(m_magProxy, i);
                        break;
                    }
                }
                tv = m_magView;
            } else {
                std::fprintf(stderr, "csnhypodbpQT: SELECT solo en sta|mag\n");
                return false;
            }
            if (!tv || vr < 0) {
                std::fprintf(stderr, "csnhypodbpQT: estacion <%s> no esta en la tabla\n",
                             sel.constData());
                return false;
            }
            {
                const QModelIndex ix = tv->model()->index(vr, 0);

                tv->setFocus(Qt::OtherFocusReason);
                tv->setCurrentIndex(ix);
                tv->selectionModel()->select(
                    ix, QItemSelectionModel::ClearAndSelect |
                            QItemSelectionModel::Rows);
            }
            if (qgetenv("BPQ_INPUT_REFRESH") == QByteArray("1"))
                renderMagnitudes();
            {
                QString msel = QStringLiteral("-");

                if (m_magView && m_magProxy &&
                    m_magView->currentIndex().isValid()) {
                    const int src = sourceRowForView(m_magProxy,
                                                     m_magView->currentIndex());
                    const CsnhypodbpMagRow *r = m_magModel ? m_magModel->row(src)
                                                           : nullptr;

                    if (r) msel = QString::fromUtf8(r->sta);
                }
                std::printf("input select=%s target=%s sel_sta=%s mag_sel=%s "
                            "traces_sta=%s ntr=%d\n",
                            sel.constData(), target.constData(),
                            m_selSta.toUtf8().constData(), msel.toUtf8().constData(),
                            m_selSta.toUtf8().constData(), m_magCtx.ntr);
            }
            std::fflush(stdout);
            return true;
        }
    }

    {
        const QByteArray wheel = qgetenv("BPQ_INPUT_WHEEL");

        if (!wheel.isEmpty()) {
            int dy = 0;

            if (wheel == QByteArray("up")) dy = 120;
            else if (wheel == QByteArray("down")) dy = -120;
            else dy = wheel.toInt();
            {
                const QPointF pos(w->width() / 2.0, w->height() / 2.0);
                QWheelEvent ev(pos, w->mapToGlobal(pos.toPoint()), QPoint(),
                               QPoint(0, dy), Qt::NoButton, km,
                               Qt::NoScrollPhase, false);

                QCoreApplication::sendEvent(w, &ev);
            }
            std::printf("input wheel=%d target=%s scroll=%d win=%.1f\n", dy,
                        target.constData(), m_tracesCtx.scroll_row, m_winLen);
            std::fflush(stdout);
            return true;
        }
    }

    {
        const QByteArray key = qgetenv("BPQ_INPUT_KEY").toLower();
        int k = 0;

        if (key.isEmpty()) {
            std::fprintf(stderr, "csnhypodbpQT: falta BPQ_INPUT_KEY/WHEEL\n");
            return false;
        }
        if (key.size() == 1) {
            const char c = key.at(0);

            if (c >= 'a' && c <= 'z') k = Qt::Key_A + (c - 'a');
            else if (c >= 'A' && c <= 'Z') k = Qt::Key_A + (c - 'A');
            else if (c >= '0' && c <= '9') k = Qt::Key_0 + (c - '0');
        } else if (key == QByteArray("escape") || key == QByteArray("esc")) {
            k = Qt::Key_Escape;
        } else if (key == QByteArray("up")) {
            k = Qt::Key_Up;
        } else if (key == QByteArray("down")) {
            k = Qt::Key_Down;
        } else if (key == QByteArray("pageup")) {
            k = Qt::Key_PageUp;
        } else if (key == QByteArray("pagedown")) {
            k = Qt::Key_PageDown;
        } else if (key == QByteArray("home")) {
            k = Qt::Key_Home;
        } else if (key == QByteArray("end")) {
            k = Qt::Key_End;
        }
        if (!k) {
            std::fprintf(stderr, "csnhypodbpQT: BPQ_INPUT_KEY desconocida <%s>\n",
                         key.constData());
            return false;
        }
        {
            QKeyEvent ev(QEvent::KeyPress, k, km);

            QCoreApplication::sendEvent(w, &ev);
        }
        std::printf("input key=%s target=%s armed=%d phase=%c scroll=%d win=%.1f\n",
                    key.constData(), target.constData(), m_pickArmed ? 1 : 0,
                    m_pickPhase, m_tracesCtx.scroll_row, m_winLen);
        std::fflush(stdout);
        return true;
    }
}

/* Hook headless (BPQ_MODE=--sort): emite el `sectionClicked` REAL de la
 * cabecera (pasa por la conexion de wire_header_sort) y vuelca el orden. */
bool BpqMainWindow::runSortForTest()
{
    const QByteArray table = qgetenv("BPQ_SORT_TABLE").toLower();
    const int col = qgetenv("BPQ_SORT_COL").toInt();
    int clicks = qgetenv("BPQ_SORT_CLICKS").toInt();
    QTableView *view = nullptr;
    ewguiqt::SortProxy *proxy = nullptr;

    if (table == QByteArray("events")) {
        view = m_eventsView;
        proxy = m_eventsProxy;
    } else if (table == QByteArray("origins")) {
        view = m_originsView;
        proxy = m_originsProxy;
    } else if (table == QByteArray("sta")) {
        view = m_staView;
        proxy = m_staProxy;
    } else if (table == QByteArray("mag")) {
        view = m_magView;
        proxy = m_magProxy;
    }
    if (!view || !proxy || !view->model() || !view->horizontalHeader()) {
        std::fprintf(stderr, "csnhypodbpQT: BPQ_SORT_TABLE desconocida\n");
        return false;
    }
    if (col < 0 || col >= view->model()->columnCount()) {
        std::fprintf(stderr, "csnhypodbpQT: columna %d fuera de rango\n", col);
        return false;
    }
    if (clicks <= 0) clicks = 1;
    for (int i = 0; i < clicks; i++) {
        QMetaObject::invokeMethod(view->horizontalHeader(), "sectionClicked",
                                  Qt::DirectConnection, Q_ARG(int, col));
    }

    QStringList rows;
    const int n = qMin(3, view->model()->rowCount());
    for (int r = 0; r < n; r++) {
        const int src = sourceRowForView(proxy, view->model()->index(r, 0));

        if (table == QByteArray("events")) {
            rows << (m_eventsModel ? m_eventsModel->keyAt(src)
                                   : QStringLiteral("-"));
        } else if (table == QByteArray("origins")) {
            rows << (m_originsModel ? m_originsModel->keyAt(src)
                                    : QStringLiteral("-"));
        } else if (table == QByteArray("sta")) {
            const int i = m_staModel ? m_staModel->staIndexAt(src) : -1;

            rows << (i >= 0 && i < m_staRows.size()
                         ? QString::fromUtf8(m_staRows.at(i).sta)
                               + QLatin1Char((src % 2) ? 'S' : 'P')
                         : QStringLiteral("-"));
        } else {
            const CsnhypodbpMagRow *mr = m_magModel ? m_magModel->row(src)
                                                    : nullptr;

            rows << (mr ? QString::fromUtf8(mr->sta) : QStringLiteral("-"));
        }
    }
    std::printf("sort table=%s col=%d clicks=%d last=%d order=%s rows=%s\n",
                table.constData(), col, clicks, proxy->lastColumn(),
                proxy->lastOrder() == Qt::DescendingOrder ? "desc" : "asc",
                rows.join(QLatin1Char(',')).toUtf8().constData());
    std::fflush(stdout);
    return true;
}

void BpqMainWindow::searchEvents()
{    if (!m_db || !bpq_db_available(m_db)) {
        QMessageBox::information(this, QStringLiteral("Search events"),
                                 QStringLiteral("Sin conexion a la base hipocentral."));
        return;
    }

    /* Prefill: el ultimo rango usado o la ultima hora, en UTC como el .d. */
    const double now = double(std::time(nullptr));
    const double from = (m_searchTo > 0.0) ? m_searchFrom : now - 3600.0;
    const double to = (m_searchTo > 0.0) ? m_searchTo : now;
    char b1[40] = "", b2[40] = "";

    (void)bpq_db_fmt_utc(from, b1, sizeof(b1));
    (void)bpq_db_fmt_utc(to, b2, sizeof(b2));

    BpqSearchDialog dlg(QString::fromUtf8(b1), QString::fromUtf8(b2),
                        (m_cfg->search_max_events > 0) ? m_cfg->search_max_events : 200,
                        this);
    if (dlg.exec() != QDialog::Accepted) return;

    double t0 = 0.0, t1 = 0.0;
    if (bpq_db_parse_utc(dlg.fromText().toUtf8().constData(), &t0) != 0 ||
        bpq_db_parse_utc(dlg.toText().toUtf8().constData(), &t1) != 0 ||
        !(t1 > t0)) {
        QMessageBox::warning(this, QStringLiteral("Search events"),
                             QStringLiteral("Rango invalido. Formato UTC: "
                                            "YYYY-MM-DD HH:MM:SS"));
        return;
    }

    char err[512] = "";
    bpq_events_clear(m_events);          /* el resultado REEMPLAZA la vista */
    m_selKey.clear();
    m_selEventKey.clear();
    bpq_pin_clear(&m_pin);
    m_pinEvent = nullptr;
    const int ne = bpq_db_search(m_db, m_cfg, m_events, t0, t1, dlg.maxEvents(),
                                 err, sizeof(err));
    m_searchFrom = t0;
    m_searchTo = t1;
    m_searchMode = true;
    refreshFromStore();
    statusBar()->showMessage(
        QStringLiteral("busqueda: %1 eventos%2")
            .arg(ne)
            .arg(err[0] ? QStringLiteral(" — ") + QString::fromUtf8(err) : QString()),
        8000);
}

void BpqMainWindow::clearSearch()
{
    if (!m_searchMode) return;

    m_searchMode = false;
    m_searchFrom = m_searchTo = 0.0;
    m_selKey.clear();
    m_selEventKey.clear();
    bpq_pin_clear(&m_pin);
    m_pinEvent = nullptr;
    bpq_events_clear(m_events);

    /* Vuelve a vivo + historial. */
    if (m_db && bpq_db_available(m_db)) {
        char err[512] = "";
        const int hist = (m_cfg->db_history_hours > 0) ? m_cfg->db_history_hours : 24;
        (void)bpq_db_load_recent(m_db, m_cfg, m_events, hist, err, sizeof(err));
    }
    refreshFromStore();
    statusBar()->showMessage(QStringLiteral("vista: vivo + historial"), 5000);
}

void BpqMainWindow::refreshFromStore()
{
    /* Identidad ESTABLE del evento, no la preferida: una confirmacion puede
     * cambiarla y no queremos perder el evento (ni el pin) por eso. */
    const QString keep = m_selEventKey;
    m_eventsModel->refresh();
    if (m_eventsModel->rowCount() == 0) {
        bpq_pin_clear(&m_pin);
        m_pinEvent = nullptr;
        m_selEventKey.clear();
        return;
    }

    int src = keep.isEmpty() ? -1 : m_eventsModel->rowForEventKey(keep);
    /* Sin seleccion previa: se elige la primera fila para que el detalle
     * (Origins) no quede vacio al arrancar (la version GTK hace lo mismo). */
    if (src < 0 && keep.isEmpty()) src = 0;
    if (src < 0) {
        /* El evento mostrado desaparecio (busqueda/limpieza): sin pin huerfano. */
        bpq_pin_clear(&m_pin);
        m_pinEvent = nullptr;
        return;
    }

    const QModelIndex idx = m_eventsProxy->mapFromSource(m_eventsModel->index(src, 0));
    if (idx.isValid()) m_eventsView->selectRow(idx.row());
}

void BpqMainWindow::loadTestArc()
{
    const char *p = std::getenv("BPQ_TEST_ARC");
    QByteArray data;
    CsnhypodbpSolution sol;
    QFile f;

    if (!p || !p[0] || !m_events) return;
    f.setFileName(QString::fromUtf8(p));
    if (!f.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(QStringLiteral("BPQ_TEST_ARC: no puedo abrir %1")
                                     .arg(QString::fromUtf8(p)), 0);
        return;
    }
    data = f.readAll();
    f.close();
    if (csnhypodbp_parse_solution(data.constData(), (size_t)data.size(), 163, &sol) != 0) {
        statusBar()->showMessage(QStringLiteral("BPQ_TEST_ARC: ARC invalido"), 0);
        return;
    }
    bpq_events_feed_arc(m_events, &sol);
}

/* --- hooks de test de la fase 7 (BPQ_TEST_*) ------------------------------ */

/* Solucion sintetica determinista (misma estructura que el store real). */
static void fillTestSol(CsnhypodbpSolution *s, int qid, int mod, unsigned ver,
                        double otime, double lat, double lon, double depth,
                        int nph, double rms, int confirmed, const char *sta)
{
    memset(s, 0, sizeof(*s));
    s->qid = qid;
    s->mod = mod;
    s->version = ver;
    s->otime = otime;
    s->lat = lat;
    s->lon = lon;
    s->depth = depth;
    s->rms = rms;
    s->nph = nph;
    s->gap = 120;
    s->confirmed = confirmed;
    s->pref_mag = 'L';
    s->npicks = 1;
    snprintf(s->picks[0].sta, sizeof(s->picks[0].sta), "%s", sta);
    snprintf(s->picks[0].chan, sizeof(s->picks[0].chan), "HHZ");
    snprintf(s->picks[0].net, sizeof(s->picks[0].net), "CX");
    snprintf(s->picks[0].loc, sizeof(s->picks[0].loc), "--");
    s->picks[0].phase = 'P';
    s->picks[0].t_pick = otime + 5.0;
    s->picks[0].weight = 0.5;
    s->picks[0].residual = NAN;
}

static void feedTestMag(BpqEvents *e, int qid, int mod, const char *type,
                        double value, int nsta)
{
    BpqMagMsg m;

    memset(&m, 0, sizeof(m));
    m.qid = qid;
    m.mod = mod;
    m.value = value;
    m.nsta = nsta;
    snprintf(m.type, sizeof(m.type), "%s", type);
    bpq_events_feed_mag(e, &m);
}

void BpqMainWindow::loadPinFixture()
{
    CsnhypodbpSolution s;
    const double t0 = 1767323045.0;   /* evento A: el mas nuevo (fila 0) */
    const double t1 = t0 - 86400.0;   /* evento B: un dia antes */

    if (!m_events) return;
    /* Estacion del catalogo (StaFile): el fixture tambien ejercita el canvas de
     * trazas con sus tres componentes HHZ/HHN/HHE. */
    fillTestSol(&s, 100, 162, 1, t0, -20.100, -70.100, 10.0, 12, 0.20, 0, "PB01");
    /* Segunda estacion picada: ejercita la seleccion cruzada picking <-> mags. */
    s.npicks = 2;
    snprintf(s.picks[1].sta, sizeof(s.picks[1].sta), "%s", "PB02");
    snprintf(s.picks[1].chan, sizeof(s.picks[1].chan), "%s", "HHZ");
    snprintf(s.picks[1].net, sizeof(s.picks[1].net), "%s", "CX");
    snprintf(s.picks[1].loc, sizeof(s.picks[1].loc), "%s", "--");
    s.picks[1].phase = 'P';
    s.picks[1].t_pick = t0 + 7.0;
    s.picks[1].weight = 0.5;
    s.picks[1].residual = NAN;
    /* `created` explicito: el historial de la BD trae horas distintas y el
     * detalle las muestra (antes todas caian en el time(NULL) de la carga). */
    bpq_events_feed_arc_at(m_events, &s, t0 + 200.0);
    fillTestSol(&s, 100, 163, 2, t0 + 10.0, -20.900, -70.900, 33.0, 8, 0.50, 0, "PB01");
    bpq_events_feed_arc_at(m_events, &s, t0 + 100.0);
    fillTestSol(&s, 200, 162, 1, t1, -30.000, -71.000, 20.0, 6, 0.40, 0, "PB01");
    bpq_events_feed_arc_at(m_events, &s, t1 + 50.0);

    feedTestMag(m_events, 100, 162, "ML", 4.1, 8);
    feedTestMag(m_events, 100, 163, "ML", 3.2, 5);
    feedTestMag(m_events, 100, 163, "Mwp", 3.5, 4);
    feedTestMag(m_events, 200, 162, "ML", 2.5, 3);

    m_selEventKey.clear();
    refreshFromStore();
    (void)runEventForTest(QStringLiteral("100:162:1"));
}

/* Solucion confirmada nueva del evento mostrado: pasa a preferida y reordena
 * Origins, sin cambiar la identidad estable del evento. */
void BpqMainWindow::feedConfirmedAfter()
{
    CsnhypodbpSolution s;
    const CsnhypodbpSolution *b;
    double t0;

    if (!m_events || !m_curEv) return;
    b = bpq_event_sol(m_curEv, 0);
    if (!b) return;
    t0 = b->otime;
    fillTestSol(&s, b->qid, 164, 3, t0 + 20.0, -21.500, -71.500, 45.0,
                15, 0.10, 1, "PST");
    bpq_events_feed_arc(m_events, &s);
    feedTestMag(m_events, b->qid, 164, "ML", 4.8, 9);
    refreshFromStore();
}

bool BpqMainWindow::runPinForTest(const QString &key)
{
    if (!m_originsModel) return false;
    const int row = m_originsModel->rowForKey(key);
    if (row < 0) return false;
    pinOrigin(row, true, true);
    return bpq_pin_matches(&m_pin, key.toLatin1().constData()) ? true : false;
}

bool BpqMainWindow::runClickForTest(const QString &key)
{
    if (!m_originsModel || !m_originsView) return false;
    const int row = m_originsModel->rowForKey(key);
    if (row < 0) return false;
    {
        const int vrow = viewRowForSource(m_originsProxy, row);

        if (vrow < 0) return false;
        m_originsView->selectRow(vrow);
    }
    return true;
}

bool BpqMainWindow::runEventForTest(const QString &key)
{
    if (!m_eventsModel || !m_eventsView || !m_eventsProxy) return false;
    int src = m_eventsModel->rowForKey(key);
    if (src < 0) src = m_eventsModel->rowForEventKey(key);
    if (src < 0) return false;
    const QModelIndex idx = m_eventsProxy->mapFromSource(m_eventsModel->index(src, 0));
    if (!idx.isValid()) return false;
    m_eventsView->selectRow(idx.row());
    return true;
}

bool BpqMainWindow::runCandForTest(bool discard)
{
    if (discard) {
        if (!m_candValid) return false;
        discardCandidate();
        return !m_candValid;
    }

    const CsnhypodbpSolution *sol = currentSolution();
    if (!sol) return false;
    m_cand = *sol;
    m_cand.depth += 5.0;
    m_cand.lat += 0.5;
    m_candKey = solKey(&m_cand);
    m_candValid = true;
    m_candErr.clear();
    renderPreview();
    return true;
}

void BpqMainWindow::runTestHooks()
{
    const QByteArray fx = qgetenv("BPQ_TEST_FIXTURE");
    if (fx == QByteArray("pins")) loadPinFixture();

    const QByteArray pins = qgetenv("BPQ_TEST_PIN");
    if (!pins.isEmpty()) {
        const QList<QByteArray> list = pins.split(',');

        for (const QByteArray &k : list)
            if (!k.isEmpty()) (void)runPinForTest(QString::fromUtf8(k));
    }
    if (qgetenv("BPQ_TEST_PIN_AGAIN") == QByteArray("1") && bpq_pin_active(&m_pin))
        (void)runPinForTest(QString::fromLatin1(bpq_pin_key(&m_pin)));

    const QByteArray click = qgetenv("BPQ_TEST_CLICK");
    if (!click.isEmpty()) (void)runClickForTest(QString::fromUtf8(click));

    if (qgetenv("BPQ_TEST_FEED_AFTER") == QByteArray("1")) feedConfirmedAfter();

    /* Cambio de evento DESPUES de fijar: comprueba que el pin no cruza. */
    const QByteArray ev = qgetenv("BPQ_TEST_EVENT");
    if (!ev.isEmpty()) (void)runEventForTest(QString::fromUtf8(ev));

    if (qgetenv("BPQ_TEST_REFRESH") == QByteArray("1")) refreshFromStore();

    if (qgetenv("BPQ_TEST_CAND") == QByteArray("1")) (void)runCandForTest(false);
    if (qgetenv("BPQ_TEST_CAND_DISCARD") == QByteArray("1")) (void)runCandForTest(true);
}

void BpqMainWindow::buildUi()
{
    m_splitH = new QSplitter(Qt::Horizontal, this);
    m_splitH->setObjectName("split_main");

    m_tabs = new QTabWidget(m_splitH);
    m_tabs->setObjectName("tabs_main");
    buildEventsTab();
    buildProcesarTab();
    m_splitH->addWidget(m_tabs);
    /* Al cambiar de sub-pestana el viewport real cambia: se reajustan los
     * anchos y el reparto para que no aparezca scroll horizontal. */
    connect(m_subtabs, &QTabWidget::currentChanged, this, [this](int) {
        QTimer::singleShot(0, this, [this]() {
            fitProcesarTable(m_staView, m_splitPicking);
            fitProcesarTable(m_magView, m_splitMag);
        });
    });

    buildMapsColumn();
    m_splitH->setStretchFactor(0, 1);
    m_splitH->setStretchFactor(1, 0);
    m_splitH->setSizes(QList<int>() << 760 << 340);

    setCentralWidget(m_splitH);

    m_status = new QLabel(this);
    const QString extra = (m_cfg && m_cfg->state_dir[0])
        ? QStringLiteral("state=%1").arg(QString::fromUtf8(m_cfg->state_dir))
        : QStringLiteral("state=<del .d>");
    m_status->setText(QStringLiteral("config OK  in=%1  ref=%2  %3")
                          .arg(m_cfg ? QString::fromUtf8(m_cfg->in_ring) : QStringLiteral("?"))
                          .arg(m_cfg ? QString::fromUtf8(m_cfg->in_ring_raw) : QStringLiteral("?"))
                          .arg(extra));
    statusBar()->addWidget(m_status, 1);
}

void BpqMainWindow::buildEventsTab()
{
    m_splitEvents = new QSplitter(Qt::Vertical);
    m_splitEvents->setObjectName("split_events");

    m_eventsView  = make_view("master-events");
    m_originsView = make_view("detail-solutions");
    m_splitEvents->addWidget(m_eventsView);
    m_splitEvents->addWidget(m_originsView);
    m_splitEvents->setSizes(QList<int>() << 260 << 260);

    m_tabs->addTab(m_splitEvents, QStringLiteral("Eventos"));
}

void BpqMainWindow::buildProcesarTab()
{
    QWidget *page = new QWidget;

    m_subtabs = new QTabWidget(page);
    m_subtabs->setObjectName("tabs_procesar");

    /* --- sub-pestaña picking: tabla de estaciones + trazas + eje --- */
    {
        QWidget *pk = new QWidget;
        m_splitPicking = new QSplitter(Qt::Horizontal, pk);
        m_splitPicking->setObjectName("split_picking");

        m_staView = make_table(kStaHeaders, 5, "sta-table");
        m_staModel = new BpqStationsModel(this);
        m_staProxy = new ewguiqt::SortProxy(this);
        m_staProxy->setSourceModel(m_staModel);
        m_staProxy->setColumnKinds({ewguiqt::SortKind::Text,
                                    ewguiqt::SortKind::Number,
                                    ewguiqt::SortKind::Text,
                                    ewguiqt::SortKind::Text,
                                    ewguiqt::SortKind::Number}, 0);
        m_staProxy->setCellNumberFn(
            [](const QAbstractItemModel &m, int r, int c) {
                const auto *sm = qobject_cast<const BpqStationsModel *>(&m);
                return sm ? sm->sortValue(r, c) : std::nan("");
            });
        m_staProxy->setDynamicSortFilter(false);
        m_staView->setModel(m_staProxy);
        m_staView->setFocusPolicy(Qt::NoFocus);
        m_staView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        /* El ancho exacto lo pone fit_columns; la tabla no debe fijar el
         * minimo de la ventana (si el usuario la encoge, aparece scroll). */
        m_staView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        /* -1: conserva el orden natural (distancia) hasta el primer clic. */
        ewguiqt::wire_header_sort(m_staView, m_staProxy, -1);
        m_splitPicking->addWidget(m_staView);

        QWidget *right = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(right);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        QWidget *traceWrap = new QWidget(right);
        QHBoxLayout *th = new QHBoxLayout(traceWrap);
        th->setContentsMargins(0, 0, 0, 0);
        th->setSpacing(0);
        m_waves = new BpqCanvasWidget(BPQ_SCENE_TRACES, traceWrap);
        m_waves->setObjectName("canvas_traces");
        m_tracesScroll = new QScrollBar(Qt::Vertical, traceWrap);
        m_tracesScroll->setObjectName("traces-scroll");
        th->addWidget(m_waves, 1);
        th->addWidget(m_tracesScroll, 0);
        m_axis  = new BpqCanvasWidget(BPQ_SCENE_TRACE_AXIS, right);
        m_axis->setObjectName("canvas_axis");
        m_axis->setFixedHeight(20);
        v->addWidget(traceWrap, 1);
        v->addWidget(m_axis, 0);
        connect(m_tracesScroll, &QScrollBar::valueChanged, this,
                [this](int val) {
            m_tracesCtx.scroll_row = val;
            if (m_waves) m_waves->requestRepaint();
            if (m_axis) m_axis->requestRepaint();
        });
        m_splitPicking->addWidget(right);
        m_splitPicking->setStretchFactor(0, 0);
        m_splitPicking->setStretchFactor(1, 1);
        m_splitPicking->setSizes(QList<int>() << 240 << 640);

        QVBoxLayout *lay = new QVBoxLayout(pk);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->addWidget(m_splitPicking, 1);
        buildPickingBar(pk, lay);

        m_subtabs->addTab(pk, QStringLiteral("picking"));
    }

    /* --- sub-pestaña magnitudes: tabla + traza procesada + eje --- */
    {
        QWidget *mg = new QWidget;
        m_splitMag = new QSplitter(Qt::Horizontal, mg);
        m_splitMag->setObjectName("split_mag");

        m_magView = make_table(kMagHeaders, 8, "mag-table");
        m_magModel = new BpqMagModel(this);
        m_magProxy = new ewguiqt::SortProxy(this);
        m_magProxy->setSourceModel(m_magModel);
        m_magProxy->setColumnKinds({ewguiqt::SortKind::Text,
                                    ewguiqt::SortKind::Number,
                                    ewguiqt::SortKind::Text,
                                    ewguiqt::SortKind::Number,
                                    ewguiqt::SortKind::Number,
                                    ewguiqt::SortKind::Number,
                                    ewguiqt::SortKind::Number,
                                    ewguiqt::SortKind::Text}, 0);
        m_magProxy->setCellNumberFn(
            [](const QAbstractItemModel &m, int r, int c) {
                const auto *mm = qobject_cast<const BpqMagModel *>(&m);
                return mm ? mm->sortValue(r, c) : std::nan("");
            });
        m_magProxy->setDynamicSortFilter(false);
        m_magView->setModel(m_magProxy);
        m_magView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        ewguiqt::wire_header_sort(m_magView, m_magProxy, -1);
        m_splitMag->addWidget(m_magView);

        /* Pila `traza` | `Mww` (como el stack de la GTK): el panel Mww convive
         * con la traza procesada en el mismo tercio derecho. */
        QWidget *tracePage = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(tracePage);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        m_magCanvas = new BpqCanvasWidget(BPQ_SCENE_MAG, tracePage);
        m_magCanvas->setObjectName("canvas_mag");
        m_magAxis = new BpqCanvasWidget(BPQ_SCENE_MAG_AXIS, tracePage);
        m_magAxis->setObjectName("canvas_mag_axis");
        m_magAxis->setFixedHeight(20);
        v->addWidget(m_magCanvas, 1);
        v->addWidget(m_magAxis, 0);

        QTabWidget *rstack = new QTabWidget;
        rstack->setObjectName("mag_stack");
        /* El panel Mww es una pestana alternativa y no debe fijar el ancho
         * minimo de la ventana: al mostrar magnitudes el reparto se ajusta a
         * la tabla y el canvas se queda con el resto. */
        rstack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        rstack->addTab(tracePage, QStringLiteral("traza"));
        m_mwwPanel = new BpqMwwPanel(rstack);
        m_mwwPanel->setObjectName("mww_panel");
        connect(m_mwwPanel, &BpqMwwPanel::calculateRequested, this,
                &BpqMainWindow::startMww);
        connect(m_mwwPanel, &BpqMwwPanel::cancelRequested, this,
                &BpqMainWindow::cancelMww);
        rstack->addTab(m_mwwPanel, QStringLiteral("Mww"));
        m_splitMag->addWidget(rstack);
        m_splitMag->setStretchFactor(0, 0);   /* tabla: ancho ajustado */
        m_splitMag->setStretchFactor(1, 1);   /* traza/Mww: el resto */
        m_splitMag->setSizes(QList<int>() << 240 << 640);

        /* La franja se arrastra desde su handle: pulsacion (agarre), dragged
         * (nueva ventana) y release (fin). El resto del canvas queda para el
         * zoom por clics, como en la GTK. */
        connect(m_magCanvas, &BpqCanvasWidget::pressed, this,
                &BpqMainWindow::magPress);
        connect(m_magCanvas, &BpqCanvasWidget::dragged, this,
                &BpqMainWindow::magDragMove);
        connect(m_magCanvas, &BpqCanvasWidget::released, this,
                &BpqMainWindow::magRelease);

        QVBoxLayout *lay = new QVBoxLayout(mg);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->addWidget(m_splitMag, 1);

        /* Barra: magnitud activa + puerta de distancia + acciones. */
        QWidget *bar = new QWidget(mg);
        bar->setObjectName("mag_bar_row");
        QHBoxLayout *bl = new QHBoxLayout(bar);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(4);
        bl->addWidget(new QLabel(QStringLiteral("mag:"), bar));
        for (int k = 0; k < BPQ_MAG_KINDS; k++) {
            QRadioButton *rb = new QRadioButton(
                QString::fromUtf8(csnhypodbp_magcfg_name(k)), bar);
            rb->setObjectName(QStringLiteral("mag_kind_%1").arg(k));
            rb->setChecked(k == 0);
            connect(rb, &QRadioButton::toggled, this, [this, k](bool on) {
                if (on) setMagKind(k);
            });
            m_magRadios.append(rb);
            bl->addWidget(rb);
        }
        bl->addSpacing(8);
        bl->addWidget(new QLabel(QStringLiteral("dist[deg]:"), bar));
        m_magGateLo = new QLineEdit(bar);
        m_magGateLo->setObjectName("mag_gate_lo");
        m_magGateLo->setMaxLength(6);
        m_magGateLo->setFixedWidth(52);
        m_magGateHi = new QLineEdit(bar);
        m_magGateHi->setObjectName("mag_gate_hi");
        m_magGateHi->setMaxLength(6);
        m_magGateHi->setFixedWidth(52);
        connect(m_magGateLo, &QLineEdit::editingFinished, this,
                &BpqMainWindow::applyMagGate);
        connect(m_magGateHi, &QLineEdit::editingFinished, this,
                &BpqMainWindow::applyMagGate);
        bl->addWidget(m_magGateLo);
        bl->addWidget(m_magGateHi);
        {
            QPushButton *rst = new QPushButton(QStringLiteral("reset default"), bar);
            QPushButton *calc = new QPushButton(QStringLiteral("calculate"), bar);
            QPushButton *all = new QPushButton(QStringLiteral("calculate all"), bar);

            rst->setObjectName("mag_reset");
            calc->setObjectName("mag_calculate");
            all->setObjectName("mag_calculate_all");
            connect(rst, &QPushButton::clicked, this, [this]() {
                if (!m_magPanel) return;
                bpq_magpanel_gate_reset(m_magPanel);
                applyMagGate();
            });
            connect(calc, &QPushButton::clicked, this, [this]() {
                calculateMagnitudes(false);
            });
            connect(all, &QPushButton::clicked, this, [this]() {
                calculateMagnitudes(true);
            });
            m_magCalc = calc;
            bl->addSpacing(6);
            bl->addWidget(rst);
            bl->addWidget(calc);
            bl->addWidget(all);
        }
        m_magBar = new QLabel(bar);
        m_magBar->setObjectName("mag_bar");
        /* El texto de estado es largo: no debe fijar el ancho minimo de la
         * ventana (Ignored) ni dejar sin sitio a la tabla. Ocupa el sobrante. */
        m_magBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_magBar->setMinimumWidth(0);
        bl->addWidget(m_magBar, 1);
        lay->addWidget(bar, 0);

        m_subtabs->addTab(mg, QStringLiteral("magnitudes"));
    }

    QVBoxLayout *outer = new QVBoxLayout(page);
    outer->setContentsMargins(2, 2, 2, 2);
    /* Preview compartido por picking y magnitudes (paridad GTK): un solo render
     * para estado, valores, color y botones. */
    m_preview = new BpqPreviewPanel(page);
    m_preview->setObjectName(QStringLiteral("preview_panel"));
    connect(m_preview, &BpqPreviewPanel::confirmRequested,
            this, &BpqMainWindow::confirmCandidate);
    connect(m_preview, &BpqPreviewPanel::discardRequested,
            this, &BpqMainWindow::discardCandidate);
    connect(m_preview, &BpqPreviewPanel::prefMagChanged, this, [this](int kind) {
        m_prefMag = kind;
        m_prefMagUser = true;
    });
    outer->addWidget(m_preview, 0);
    outer->addWidget(m_subtabs, 1);

    m_tabs->addTab(page, QStringLiteral("Procesar"));
}

void BpqMainWindow::buildMapsColumn()
{
    m_splitV = new QSplitter(Qt::Vertical, this);
    m_splitV->setObjectName("split_maps");

    m_mapPlan = new BpqCanvasWidget(BPQ_SCENE_MAP_PLAN, m_splitV);
    m_mapPlan->setObjectName("canvas_map_plan");
    m_mapProf = new BpqCanvasWidget(BPQ_SCENE_MAP_PROF, m_splitV);
    m_mapProf->setObjectName("canvas_map_prof");

    m_splitV->addWidget(m_mapPlan);
    m_splitV->addWidget(m_mapProf);
    /* MapSplit: 65/35 (el .d trae 7/3 = 0.70; se aplica una sola vez) */
    const int total = 520;
    const double frac = (m_cfg && m_cfg->map_split_frac > 0.0) ? m_cfg->map_split_frac : 0.7;
    m_splitV->setSizes(QList<int>() << int(total * frac)
                                    << int(total * (1.0 - frac)));
    m_splitH->addWidget(m_splitV);
}

void BpqMainWindow::buildMenuBar()
{
    QMenuBar *mb = menuBar();

    QMenu *file = mb->addMenu(QStringLiteral("&File"));
    QAction *exp = file->addAction(QStringLiteral("Export view..."));
    connect(exp, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export view"), QString(),
            QStringLiteral("PNG (*.png);;SVG (*.svg);;PDF (*.pdf)"));
        if (path.isEmpty()) return;
        BpqFmt fmt = BPQ_FMT_PNG;
        if (path.endsWith(".svg", Qt::CaseInsensitive)) fmt = BPQ_FMT_SVG;
        else if (path.endsWith(".pdf", Qt::CaseInsensitive)) fmt = BPQ_FMT_PDF;

        /* Se exporta el lienzo con el foco; si no hay, el principal de la
         * pestana visible. El CONTEXTO es obligatorio: sin el, la escena se
         * dibuja vacia (mismo error que tenia este menu al principio). */
        BpqCanvasWidget *cv = qobject_cast<BpqCanvasWidget *>(QApplication::focusWidget());
        if (!cv) {
            if (m_tabs && m_tabs->currentIndex() == 0) cv = m_mapPlan;
            else if (m_subtabs && m_subtabs->currentIndex() == 0) cv = m_waves;
            else cv = m_magCanvas;
        }
        if (!cv) return;
        const int w = cv->width() > 0 ? cv->width() : 640;
        const int h = cv->height() > 0 ? cv->height() : 480;
        const int rc = bpq_scene_export(cv->scene(), cv->context(),
                                        path.toUtf8().constData(), fmt, w, h, 1.0);
        if (rc != 0)
            QMessageBox::warning(this, QStringLiteral("Export view"),
                                 QStringLiteral("No se pudo exportar a %1").arg(path));
        else
            statusBar()->showMessage(
                QStringLiteral("exportado %1 (%2x%3)")
                    .arg(path).arg(w).arg(h), 4000);
    });
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("Quit"));
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *events = mb->addMenu(QStringLiteral("&Events"));
    QAction *search = events->addAction(QStringLiteral("Search events..."));
    connect(search, &QAction::triggered, this, &BpqMainWindow::searchEvents);
    QAction *clr = events->addAction(QStringLiteral("Clear search"));
    connect(clr, &QAction::triggered, this, &BpqMainWindow::clearSearch);

    /* Filtros de VISTA: los presets del .d, con su atajo. Solo afectan el dibujo;
     * `locate` manda la traza cruda. Los atajos NO van como
     * QAction::setShortcut: asi no se disparan con el foco en una caja de texto
     * ni fuera de la sub-pestana `picking` (los resuelve keyPressEvent con
     * csnhypodbp_filtercfg_key_action). */
    QMenu *fmen = mb->addMenu(QStringLiteral("Fi&lter"));
    QAction *fdlg = fmen->addAction(QStringLiteral("Filters..."));
    connect(fdlg, &QAction::triggered, this, &BpqMainWindow::openFilters);
    fmen->addSeparator();
    QAction *raw = fmen->addAction(QStringLiteral("Raw"));
    connect(raw, &QAction::triggered, this, [this]() { applyFilter(-1); });
    fmen->addSeparator();
    for (int i = 0; i < m_cfg->filtercfg.nf; i++) {
        const CsnhypodbpFilterPreset &f = m_cfg->filtercfg.f[i];
        QString label = QString::fromUtf8(f.name);

        if (f.key) label += QStringLiteral("   [%1]").arg(QLatin1Char(f.key));
        QAction *a = fmen->addAction(label);
        connect(a, &QAction::triggered, this, [this, i]() { applyFilter(i); });
    }
    fmen->addSeparator();
    QAction *stalta = fmen->addAction(QStringLiteral("STA/LTA (vista)"));
    stalta->setCheckable(true);
    m_staltaOn = (m_cfg->filtercfg.stalta_on != 0);
    stalta->setChecked(m_staltaOn);
    if (m_cfg->filtercfg.stalta_key)
        stalta->setToolTip(QStringLiteral("tecla %1 (solo en picking)")
                               .arg(QLatin1Char(m_cfg->filtercfg.stalta_key)));
    connect(stalta, &QAction::toggled, this, [this](bool on) {
        m_staltaOn = on;
        rebuildStalta();
        updateTracesContext();
        if (m_waves) m_waves->requestRepaint();
    });

    QMenu *help = mb->addMenu(QStringLiteral("&Help"));
    QAction *about = help->addAction(QStringLiteral("About csnhypodbpQT"));
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::information(
            this, QStringLiteral("csnhypodbpQT"),
            QStringLiteral("Prototipo Qt6 de csnhypodbp.\n"
                           "Reutiliza los nucleos puros del modulo GTK in situ;\n"
                           "no enlaza GTK, Adwaita, glib ni gio."));
    });
}

/* --- ganchos de test (BPQ_MODE) ------------------------------------------- */

static void dumpWidget(QString &out, const QObject *obj, int depth)
{
    const QWidget *w = qobject_cast<const QWidget *>(obj);
    if (w) {
        out += QString(depth * 2, QLatin1Char(' '));
        out += QStringLiteral("%1 name=%2 visible=%3 size=%4x%5\n")
                   .arg(QString::fromLatin1(w->metaObject()->className()))
                   .arg(w->objectName().isEmpty() ? QStringLiteral("-") : w->objectName())
                   .arg(w->isVisible() ? 1 : 0)
                   .arg(w->width()).arg(w->height());
    }
    const QObjectList kids = obj->children();
    for (const QObject *k : kids) {
        if (qobject_cast<const QWidget *>(k) || !k->objectName().isEmpty())
            dumpWidget(out, k, depth + 1);
    }
}

QString BpqMainWindow::dumpTree() const
{
    QString out;
    dumpWidget(out, this, 0);

    /* Resumen de datos: hace de `--dump` un test real del camino
     * anillos -> store -> modelo (no solo de la geometria). */
    out += QStringLiteral("data events=%1\n").arg(m_eventsModel ? m_eventsModel->rowCount() : 0);
    out += QStringLiteral("pin key=%1 event=%2 tab=%3/%4 fetches=%5\n")
               .arg(bpq_pin_active(&m_pin)
                        ? QString::fromLatin1(bpq_pin_key(&m_pin))
                        : QStringLiteral("-"))
               .arg(m_selEventKey.isEmpty() ? QStringLiteral("-") : m_selEventKey)
               .arg(m_tabs ? m_tabs->currentIndex() : -1)
               .arg(m_subtabs ? m_subtabs->currentIndex() : -1)
               .arg(m_waveJob);
    if (m_eventsModel) {
        const int n = qMin(3, m_eventsModel->rowCount());
        for (int r = 0; r < n; r++) {
            QStringList cells;
            for (int c = 0; c < m_eventsModel->columnCount(); c++)
                cells << m_eventsModel->data(m_eventsModel->index(r, c), Qt::DisplayRole).toString();
            out += QStringLiteral("row %1: %2\n").arg(r).arg(cells.join(QLatin1Char('|')));
        }
    }
    out += QStringLiteral("data origins=%1\n")
               .arg(m_originsModel ? m_originsModel->rowCount() : 0);
    if (m_originsModel && m_originsModel->rowCount() > 0) {
        QStringList created, keys;
        const int n = qMin(3, m_originsModel->rowCount());

        for (int r = 0; r < n; r++) {
            created << m_originsModel->data(m_originsModel->index(r, 3),
                                            Qt::DisplayRole).toString();
            keys << m_originsModel->keyAt(r);
        }
        out += QStringLiteral("origins created=%1 keys=%2\n")
                   .arg(created.join(QLatin1Char(',')))
                   .arg(keys.join(QLatin1Char(',')));
    }
    {
        QString sel = QStringLiteral("-");

        if (m_originsView && m_originsProxy && m_originsView->currentIndex().isValid()) {
            const int src = sourceRowForView(m_originsProxy,
                                             m_originsView->currentIndex());

            if (src >= 0) sel = m_originsModel->keyAt(src);
        }
        out += QStringLiteral("origins sel=%1\n").arg(sel);
    }
    /* Estado de la columna derecha: deja verificar el encuadre, el perfil
     * elegido y que el mapa base se decodifico (bg=0x0 = lienzo virtual). */
    out += QStringLiteral("panels pts=%1 pref=%2 sel=%3 prof=%4 zoom=%5 layers=%6 bg=%7x%8\n")
               .arg(m_npts).arg(m_pref).arg(m_mapCtx.selected)
               .arg(m_profIndex)
               .arg(m_mapCtx.zoom, 0, 'f', 6)
               .arg(bpq_map_nlayers(m_map))
               .arg(m_mapImage.isNull() ? 0 : m_mapImage.width())
               .arg(m_mapImage.isNull() ? 0 : m_mapImage.height());

    out += QStringLiteral("stations rows=%1 picked=%2 sel=%3\n")
               .arg(m_staModel ? m_staModel->rowCount() : 0)
               .arg(m_staModel ? m_staModel->pickedCount() : 0)
               .arg(m_selSta.isEmpty() ? QStringLiteral("-") : m_selSta);
    out += QStringLiteral("filter idx=%1 zero=%2 stalta=%3\n")
               .arg(m_filterIdx)
               .arg(m_phaseZero ? 1 : 0)
               .arg(m_staltaOn ? 1 : 0);
    /* Barra del locate: mascaras del envio, localizador activo y candidata. */
    out += QStringLiteral("locate mask=%1%2%3 locator=%4 busy=%5 cand=%6 res=%7 manual=%8 armed=%9 anchor=%10\n")
               .arg((m_chkA && m_chkA->isChecked()) ? 1 : 0)
               .arg((m_chkM && m_chkM->isChecked()) ? 1 : 0)
               .arg((m_chkT && m_chkT->isChecked()) ? 1 : 0)
               .arg(m_locCombo ? m_locCombo->currentText() : QStringLiteral("-"))
               .arg(m_locBusy ? 1 : 0)
               .arg(m_candValid ? 1 : 0)
               .arg(m_staModel ? m_staModel->residCount() : 0)
               .arg(bpq_manual_count())
               .arg(m_pickArmed ? 1 : 0)
               .arg(m_tracesCtx.zoom_anchor_t, 0, 'f', 1);
    /* Preview: es el mismo volcado que mira el usuario en la pantalla. */
    out += QStringLiteral("preview state=%1 cand=%2 pref=%3 title=\"%4\" "
                          "lat=%5 lon=%6 dep=%7 nph=%8 rms=%9 "
                          "mags=%10,%11,%12,%13,%14\n")
               .arg(m_previewVals.state)
               .arg(m_previewVals.candidate)
               .arg(m_prefMag)
               .arg(QString::fromUtf8(m_previewVals.title))
               .arg(QString::fromUtf8(m_previewVals.val[0]))
               .arg(QString::fromUtf8(m_previewVals.val[1]))
               .arg(QString::fromUtf8(m_previewVals.val[2]))
               .arg(QString::fromUtf8(m_previewVals.val[3]))
               .arg(QString::fromUtf8(m_previewVals.val[4]))
               .arg(QString::fromUtf8(m_previewVals.val[5]))
               .arg(QString::fromUtf8(m_previewVals.val[6]))
               .arg(QString::fromUtf8(m_previewVals.val[7]))
               .arg(QString::fromUtf8(m_previewVals.val[8]))
               .arg(QString::fromUtf8(m_previewVals.val[9]));
    out += QStringLiteral("db avail=%1 search=%2\n")
               .arg((m_db && bpq_db_available(m_db)) ? 1 : 0)
               .arg(m_searchMode ? 1 : 0);
    {
        int with = 0, nvis = 1, first = 0;
        const CsnhypodbpSolution *cs = currentSolution();

        for (BpqTrace *t : m_traces)
            if (t) with++;
        if (m_waves) {
            const double d = m_waves->devicePixelRatioF();

            first = bpq_traces_visible(&m_tracesCtx,
                                       int(m_waves->height() * d), &nvis);
        }
        out += QStringLiteral("traces rows=%1 with=%2 first=%3 nvis=%4 "
                              "scroll=%5 per=%6 sta=%7 win=[%8,+%9] sol=%10\n")
                   .arg(m_traceRows.size())
                   .arg(with)
                   .arg(first)
                   .arg(nvis)
                   .arg(m_tracesCtx.scroll_row)
                   .arg(m_tracesCtx.per_screen)
                   .arg(m_selSta.isEmpty() ? QStringLiteral("-") : m_selSta)
                   .arg(m_winT0, 0, 'f', 2)
                   .arg(m_winLen, 0, 'f', 0)
                   .arg(cs ? solKey(cs) : QStringLiteral("-"));
        out += QStringLiteral("pick armed=%1 phase=%2\n")
                   .arg(m_pickArmed ? 1 : 0)
                   .arg(m_pickArmed ? QString(QLatin1Char(m_pickPhase))
                                    : QStringLiteral("-"));
    }

    /* Sub-pestana magnitudes (fase 6.1): filas, cuantas entran al calculo y
     * cuantas ventanas ajusto el usuario. */
    {
        int in_use = 0;
        const CsnhypodbpMagRow *first = m_magRows.isEmpty() ? nullptr
                                                           : &m_magRows.first();

        for (const CsnhypodbpMagRow &r : m_magRows)
            if (r.used) in_use++;
        QString msel = QStringLiteral("-");

        if (m_magView && m_magProxy && m_magView->currentIndex().isValid()) {
            const int src = sourceRowForView(m_magProxy, m_magView->currentIndex());
            const CsnhypodbpMagRow *sr = m_magModel ? m_magModel->row(src) : nullptr;

            if (sr) msel = QString::fromUtf8(sr->sta);
        }
        out += QStringLiteral("mag kind=%1 rows=%2 in_use=%3 touched=%4 "
                              "ntr=%5 anchor=%6 magcfg=%7 sta=%8 sel=%9 "
                              "first=[%10 t0=%11 t1=%12]")
                   .arg(m_magKind)
                   .arg(m_magRows.size())
                   .arg(in_use)
                   .arg(m_magPanel ? bpq_magpanel_touched_count(m_magPanel) : 0)
                   .arg(m_magCtx.ntr)
                   .arg(m_magCtx.anchor, 0, 'f', 1)
                   .arg(m_magCtx.magcfg ? 1 : 0)
                   .arg(m_selSta.isEmpty() ? QStringLiteral("-") : m_selSta)
                   .arg(msel)
                   .arg(first ? QString::fromUtf8(first->sta) : QStringLiteral("-"))
                   .arg(first ? first->t0 : 0.0, 0, 'f', 0)
                   .arg(first ? first->t1 : 0.0, 0, 'f', 0)
               + QLatin1Char('\n');
    }

    /* Anchos: el criterio de "tabla ajustada" es que no haya scroll horizontal
     * con el ancho por defecto de la ventana. */
    {
        static const struct {
            const char *name;
            const QTableView *view;
        } tables[] = {
            { "sta", m_staView },
            { "mag", m_magView },
        };

        for (const auto &t : tables) {
            int total = 0;
            const QHeaderView *hh = t.view ? t.view->horizontalHeader() : nullptr;

            if (!t.view || !hh) continue;
            for (int c = 0; c < (t.view->model() ? t.view->model()->columnCount() : 0); c++)
                total += hh->sectionSize(c);
            out += QStringLiteral("table %1 cols=%2 total=%3 viewport=%4 hscroll=%5\n")
                       .arg(QString::fromLatin1(t.name))
                       .arg(t.view->model() ? t.view->model()->columnCount() : 0)
                       .arg(total)
                       .arg(t.view->viewport()->width())
                       .arg(t.view->horizontalScrollBar()->isVisible() ? 1 : 0);
        }
    }

    /* Diagnostico de la capa de ondas (fase 3):
     *   BPQ_WAVE_PROBE="STA,NET,CHAN,LOC,t0,secs" */
    {
        const QByteArray probe = qgetenv("BPQ_WAVE_PROBE");
        if (!probe.isEmpty() && m_wave && m_cfg) {
            char line[512] = "";
            (void)bpq_wave_probe(m_wave, m_cfg, probe.constData(), line, sizeof(line));
            out += QString::fromUtf8(line) + QLatin1Char('\n');
        }
    }
    return out;
}

QString BpqMainWindow::dumpLayout() const
{
    QString out;
    const QSplitter *spl[5] = { m_splitH, m_splitV, m_splitEvents,
                                m_splitPicking, m_splitMag };
    const char *names[5] = { "split_main", "split_maps", "split_events",
                             "split_picking", "split_mag" };
    for (int i = 0; i < 5; i++) {
        if (!spl[i]) continue;
        const QList<int> sz = spl[i]->sizes();
        QStringList parts;
        for (int v : sz) parts << QString::number(v);
        out += QStringLiteral("%1 sizes=%2\n").arg(QString::fromLatin1(names[i]),
                                                   parts.join(QLatin1Char(',')));
    }
    out += QStringLiteral("window minHint=%1x%2 current=%3x%4\n")
               .arg(minimumSizeHint().width()).arg(minimumSizeHint().height())
               .arg(width()).arg(height());
    return out;
}

bool BpqMainWindow::saveShot(const QString &pngPath) const
{
    /* grab() no es const en QWidget. */
    const QPixmap pm = const_cast<BpqMainWindow *>(this)->grab();
    return pm.save(pngPath, "PNG");
}
