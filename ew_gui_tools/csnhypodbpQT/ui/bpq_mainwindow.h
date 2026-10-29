/*
 * bpq_mainwindow.h - Ventana principal del prototipo csnhypodbpQT.
 *
 * Paridad de estructura con csnhypodbp_main.c:1559-2479 (ver docs/testLibQT.md
 * §4.4): QSplitter[H] { QTabWidget [Eventos | Procesar] , QSplitter[V]
 * {planta, perfil} }, con "Procesar" = preview + QTabWidget [picking |
 * magnitudes].
 *
 * Fase 0: estructura y lienzos; las tablas llevan sus cabeceras reales pero sin
 * modelo de datos todavia (fases 1-3 enchufan BpqEventsModel/BpqOriginsModel/
 * BpqStaModel/BpqMagModel).
 */
#ifndef BPQ_MAINWINDOW_H
#define BPQ_MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QImage>
#include <QElapsedTimer>
#include <QPointF>

#include <atomic>

#include "bpq_capi.h"
#include "bpq_pin.h"
#include "csnhypodbp_magsta.h"

class BpqEventsModel;
class BpqOriginsModel;
class BpqStationsModel;
class BpqWaveWorker;
class BpqLocateWorker;
class QThread;
class QComboBox;
class QCheckBox;
class QPushButton;
class QTimer;
class QLabel;
class QLineEdit;
class QRadioButton;
class QScrollBar;
class QKeyEvent;
class QSortFilterProxyModel;
class BpqMagModel;
class BpqMwwPanel;
class BpqMwwWorker;
class BpqPreviewPanel;
class QSplitter;
class QTabWidget;
class QTableView;
class BpqCanvasWidget;
class BpqEventsModel;
class BpqSortProxy;
class BpqOriginsModel;

namespace ewguiqt { class SortProxy; }
namespace ewguiqt { class ColumnFitter; }
class BpqRingWorker;

class BpqMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit BpqMainWindow(const BpqConfig *cfg, QWidget *parent = nullptr);
    ~BpqMainWindow() override;

    /* Ganchos de test (BPQ_MODE=--dump|--layout|--shot|--select). */
    QString dumpTree() const;
    QString dumpLayout() const;
    bool    saveShot(const QString &pngPath) const;
    /* Espera a que termine el fetch de ondas vigente (modos headless). */
    bool    waitForWaves(int ms);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;

private:
    void buildUi();
    void buildMenuBar();
    void buildEventsTab();
    void buildProcesarTab();
    void buildMapsColumn();
    void openData();          /* anillos + store + modelos + worker */
    void refreshFromStore();  /* reset de la maestra conservando la seleccion */
    void loadTestArc();       /* BPQ_TEST_ARC=<archivo ARC> (tests/demos) */
    void buildPoints(const BpqEvent *ev, const QString &keepKey = QString());
                                            /* soluciones -> CsnhypodbpMapPt[] */    void refreshPanels();                   /* mapa/perfil: contexto + auto-encuadre */
    void refreshStations(const BpqEvent *ev, bool forceFetch = false); /* tabla + ondas */
    void fetchTraces(const CsnhypodbpSolution *sol); /* ondas de las picadas */
    void buildTraceRows(const CsnhypodbpStaRow *base); /* canales de UNA estacion */
    QString pickDefaultStation() const;      /* la mas cercana con picada */
    const CsnhypodbpSolution *currentSolution() const;
    /* --- origen fijado (pin) ------------------------------------------- */
    void onOriginDoubleClicked(const QModelIndex &idx);
    void pinOrigin(int row, bool forceReload, bool switchTab = true);
    int  pinnedSolutionIndex() const;        /* punto de m_pts[] del pin (-1) */
    int  solutionIndexForKey(const BpqEvent *ev, const QString &key) const;
    /* Puente fila-vista <-> fila-fuente cuando la tabla tiene SortProxy. */
    int  viewRowForSource(const QSortFilterProxyModel *p, int srcRow) const;
    int  sourceRowForView(const QSortFilterProxyModel *p, const QModelIndex &idx) const;
    void reapplySort(ewguiqt::SortProxy *p); /* tras un reset, conserva el orden */
    /* Ajusta las columnas y ensancha el panel de la tabla lo justo para que no
     * haga falta scroll horizontal. */
    void fitProcesarTable(QTableView *view, QSplitter *split);
    void fitEventTables();
    /* Estacion actual unica para picking y magnitudes. */
    void selectStation(const QString &sta);
    void syncStationViews();
    void loadPinFixture();                   /* BPQ_TEST_FIXTURE=pins */
    void feedConfirmedAfter();               /* BPQ_TEST_FEED_AFTER=1 */
    void updateTracesContext();              /* punteros de los canvas de trazas */
    void onWavesHovered(double x, double y); /* guia vertical del puntero */
    void onWavesLeft();                      /* el puntero salio del canvas */
    void setTracesPerScreen(int per);        /* 1..3 (Shift+rueda) */
    void searchEvents();                     /* dialogo + bpq_db_search */
    void clearSearch();                      /* vuelve a vivo + historial */
    void emitManualPick(int row, double t);  /* clic en la traza -> overlay local */
    void refreshAfterManual();               /* tabla + trazas + persistencia */
    void updatePickCursor();                 /* circulo de la fase bajo el cursor */
    void zoomClickAt(double x, double y);    /* dos clics: acerca / resetea */
    void resetTimeView();                    /* vuelve a m_defT0/m_defWin */
    void axisPress(double x);                /* arrastre sobre el eje: inicio */
    void axisDrag(double dx);                /* ... y avance (dx incremental) */
    void axisRelease();                      /* ... y fin: zoom o reset */
    void applyTracesWindow();                /* ctx -> ventana + repaint */
    void applyFilter(int idx);               /* filtro de vista (idx < 0 = raw) */
    void openFilters();                      /* ventana Filters… (tecla del .d) */
    bool applyFilterKey(int key);            /* atajo del .d; true = consumida */
    bool handlePickingKey(QKeyEvent *ev);    /* teclas del picking (cualquier foco) */
    void syncTracesScroll();                 /* barra vertical <- contexto */
    bool focusIsTextInput() const;           /* no consumir teclas al escribir */
    void rebuildStalta();                    /* curva CF por traza */
    void filterOne(BpqTrace *t);             /* demean + filtro del .d */
    void startPrefetch();                    /* llena la cache de vecinas */
    void selectProfileFor(double lat, double lon);

    /* --- locate (fase 4) --- */
    void buildPickingBar(QWidget *parent, class QVBoxLayout *lay);
    void locateNow();                        /* manda las picadas efectivas */
    void cancelLocate();                     /* aborta la corrida en curso */
    void tickLocate();                       /* "calculando <loc>... N s" */
    void locateFinished(quint64 job, bool ok, BpqLocateOut *out,
                        const QString &err, double secs);
    void rebuildStaTable(const CsnhypodbpSolution *sol);  /* sin refetch de ondas */
    /* --- preview + Confirm/Discard (fase 5) --- */
    void renderPreview();                    /* UNICO camino de pintado */
    void confirmCandidate();                 /* ARC -> ConfRing + store local */
    void discardCandidate();                 /* vuelve a mostrar la elegida */
    double fixDepthKm() const;               /* Fix de la barra; < 0 = libre */
    void saveManualPicks();                  /* overlay -> ManualStateFile.picks */
    void syncTraceRows();                    /* fila nueva de la tabla -> traza */
    /* --- magnitudes (fase 6.1) --- */
    void renderMagnitudes();                 /* filas + canvas (UNICO camino) */
    void fillMagCtx();                       /* estacion actual -> bandas del canvas */
    void calculateMagnitudes(bool allKinds); /* csnmags con las ventanas */
    void magFinished(quint64 job, bool ok, CsnhypodbpMagOut *out,
                     const QString &err, double secs);
    void magPress(double x, double y);       /* agarre de la franja */
    void magDragMove(double dx, double dy);
    void magRelease();
    void magDragCancel();                    /* desarma sin tocar las ventanas */
    void magHovered(double x, double y);     /* cursor: borde/handle de la franja */
    void magLeft();
    void applyMagGate();                     /* cajas de la puerta -> panel */
    void setMagKind(int kind);               /* radio ML/Mwp/Mb/Ms */
    void onMagReceived(const BpqMagMsg &m);  /* TYPE_MAGNITUDE con estaciones */
    /* --- panel Mww (fase 6.3) --- */
    void seedMww();                          /* solucion+magnitud -> semilla */
    void startMww();                         /* Calculate (hilo propio) */
    void cancelMww();                        /* descarta el resultado en vuelo */
    void mwwFinished(quint64 job, bool ok, const QString &err, double secs);

public:
    bool runLocateForTest();                 /* hook headless BPQ_MODE=--locate */
    bool runConfirmForTest();                /* hook headless BPQ_MODE=--confirm */
    bool runMagCalcForTest();                /* hook headless BPQ_MODE=--magcalc */
    bool runMwwForTest();                    /* hook headless BPQ_MODE=--mww */
    bool runMouseForTest();                  /* hook headless BPQ_MODE=--mouse */
    bool runMagDragForTest();                /* hook headless BPQ_MODE=--magdrag */
    bool runInputForTest();                  /* hook headless BPQ_MODE=--input */
    bool runSortForTest();                   /* hook headless BPQ_MODE=--sort */
    /* Hooks de la fase 7 (BPQ_TEST_FIXTURE/PIN/CLICK/...); sin efecto en
     * produccion salvo que las variables de entorno esten puestas. */
    void runTestHooks();
    bool runPinForTest(const QString &key);
    bool runClickForTest(const QString &key);
    bool runEventForTest(const QString &key);
    bool runCandForTest(bool discard);
private:

    const BpqConfig *m_cfg = nullptr;
    BpqRings        *m_rings = nullptr;
    BpqEvents       *m_events = nullptr;
    BpqRingWorker   *m_worker = nullptr;
    BpqEventsModel  *m_eventsModel = nullptr;
    BpqSortProxy    *m_eventsProxy = nullptr;
    BpqOriginsModel *m_originsModel = nullptr;
    BpqStationsModel *m_staModel = nullptr;
    ewguiqt::SortProxy *m_originsProxy = nullptr;
    ewguiqt::SortProxy *m_staProxy = nullptr;
    ewguiqt::SortProxy *m_magProxy = nullptr;
    QString          m_selKey;
    QString          m_selEventKey;   /* identidad estable del evento mostrado */
    /* Origen fijado (fase 7): solo cambia fijando otro; manda en Procesar. */
    BpqPin           m_pin{};
    const BpqEvent  *m_pinEvent = nullptr;
    QString          m_curSolKey;     /* solucion cuyas ondas ya se pidieron */
    bool             m_syncOrigins = false; /* rebuild: no refrescar dos veces */

    /* Columna derecha: mapa y perfil reales (los contextos viven aqui). */
    BpqMap          *m_map = nullptr;
    BpqMapCtx        m_mapCtx{};
    BpqProfileCtx    m_profCtx{};
    CsnhypodbpMapPt  m_pts[64];
    int              m_ptSol[64];      /* solucion del store de cada punto */
    int              m_npts = 0;
    int              m_pref = -1;
    /* Una vez que el usuario hace zoom/paneo se respeta su vista hasta que
     * cambie de evento (si no, cada resize la devolveria al auto-encuadre). */
    bool             m_mapUserView = false;
    bool             m_profUserView = false;
    BpqProfileData  *m_profData = nullptr;
    int              m_profIndex = -1;
    QImage           m_mapImage;
    BpqWave         *m_wave = nullptr;   /* store/SDS (fase 3) */
    BpqStations     *m_staCat = nullptr; /* catalogo de estaciones (StaFile) */
    BpqDb           *m_db = nullptr;     /* base hipocentral (historial/busqueda) */
    double           m_searchFrom = 0.0;
    double           m_searchTo = 0.0;
    bool             m_searchMode = false;
    char             m_pickPhase = 'P';  /* fase que se pica con el clic */
    bool             m_pickArmed = false; /* P/S: modo picada activo (circulo) */
    /* Zoom por dos clics en las trazas: ancla en px de dispositivo. El gesto
     * inverso restituye m_defT0/m_defWin (sin pila de undo). */
    double           m_zoomAnchorX = -1.0;
    double           m_zoomAnchorT = 0.0;
    bool             m_axisDragging = false;
    double           m_axisDragX0 = 0.0;  /* px de dispositivo */
    double           m_axisDragX1 = 0.0;
    int              m_filterIdx = -1;   /* preset activo (-1 = Raw) */
    bool             m_phaseZero = true; /* FilterPhase del .d (editable en Filters…) */
    bool             m_staltaOn = false;
    QVector<BpqStaLta *> m_stalta;       /* alineado con m_traces */

    /* Panel de picking: filas de la tabla, filas con traza y sus trazas. */
    QVector<CsnhypodbpStaRow> m_staRows;
    QVector<CsnhypodbpStaRow> m_traceRows;
    QVector<BpqTrace *>       m_traces;
    QVector<QByteArray>       m_chanLabels;   /* canal de cada fila (HHZ...) */
    QVector<const char *>     m_chanPtr;      /* lo que lee el canvas */
    QString                   m_selSta;       /* estacion actual (sus canales se ven) */
    double                    m_defT0 = 0.0;  /* ventana default del evento */
    double                    m_defWin = 120.0;
    BpqTracesCtx              m_tracesCtx{};
    int                       m_tracesPerScreen = 0;   /* 1..3; inicial del .d */
    int                       m_perScreenWheelAccum = 0;
    double                    m_winT0 = 0.0;
    double                    m_winLen = 120.0;
    /* Fetch de ondas en su propio hilo (no congela la ventana). */
    QThread                   *m_waveThread = nullptr;
    BpqWaveWorker             *m_waveWorker = nullptr;
    quint64                    m_waveJob = 0;
    quint64                    m_waveDone = 0;
    int                        m_waveWith = 0;
    /* Cache LRU de trazas: la consulta/llena el worker; aqui solo se crea, se
     * libera al cerrar y se leen sus stats atomicas para --dump. */
    BpqWaveCache              *m_waveCache = nullptr;
    std::atomic<quint64>       m_waveGen{0};
    QTimer                    *m_prefetchTimer = nullptr;
    QStringList                m_traceOrder;     /* entrega del pedido del usuario */
    QStringList                m_prefetchOrder;  /* estaciones precargadas */
    int                        m_prefetchOk = 0;
    int                        m_prefetchFail = 0;
    int                        m_prefetchN = 0;  /* copia de PrefetchNeighbors */
    bool                       m_refetched = false;  /* hook BPQ_WAVE_REFETCH */

    /* `locate` en su propio hilo: csnloc + refinador tardan segundos. */
    QThread          *m_locThread = nullptr;
    BpqLocateWorker  *m_locWorker = nullptr;
    quint64           m_locJob = 0;
    bool              m_locBusy = false;
    QElapsedTimer     m_locTimer;
    QTimer           *m_locTick = nullptr;
    QPushButton      *m_locBtn = nullptr;
    QPushButton      *m_locCancel = nullptr;
    QComboBox        *m_locCombo = nullptr;
    QCheckBox        *m_chkA = nullptr;
    QCheckBox        *m_chkM = nullptr;
    QCheckBox        *m_chkT = nullptr;
    /* Candidata del ultimo locate: sus residuales son los que muestra la tabla
     * (csnhypodbp_resid_source) hasta que el usuario confirme o descarte. */
    CsnhypodbpSolution m_cand{};
    CsnhypodbpMagOut   m_candMag{};
    bool               m_candValid = false;
    QString            m_candKey;          /* clave de la solucion de ORIGEN */
    double             m_candSecs = 0.0;
    QString            m_candErr;
    QString            m_lastLocCounts;    /* P/S y M/A/T del ultimo locate */
    const BpqEvent    *m_curEv = nullptr;   /* evento mostrado (para el locate) */
    BpqPreviewPanel   *m_preview = nullptr;
    CsnhypodbpPreviewVals m_previewVals{};  /* ultimo render (para --dump) */
    int                m_prefMag = -1;      /* radio marcado (0=ML..4=Mww) */
    bool               m_prefMagUser = false;
    QCheckBox         *m_chkFix = nullptr;  /* Fix: profundidad fija del refinador */
    QLineEdit         *m_editFix = nullptr;
    QString            m_manualBase;        /* archivo legado del overlay */
    QString            m_manualPath;        /* overlay del evento actual */
    QString            m_manKey;            /* EVENTO al que pertenece el overlay */
    int                m_lastPubPicks = 0;  /* picadas publicadas en el ultimo Confirm */

    QTabWidget      *m_tabs = nullptr;      /* Eventos | Procesar */
    QTabWidget      *m_subtabs = nullptr;   /* picking | magnitudes */
    QSplitter       *m_splitH = nullptr;    /* izquierda | columna de mapas */
    QSplitter       *m_splitV = nullptr;    /* planta | perfil */
    QSplitter       *m_splitEvents = nullptr;
    QSplitter       *m_splitPicking = nullptr;  /* tabla estaciones | trazas */
    QSplitter       *m_splitMag = nullptr;      /* tabla magnitudes | traza */
    QTableView      *m_eventsView = nullptr;
    QTableView      *m_originsView = nullptr;
    ewguiqt::ColumnFitter *m_eventsFit = nullptr;   /* llena el ancho del panel */
    ewguiqt::ColumnFitter *m_originsFit = nullptr;
    QTableView      *m_staView = nullptr;
    QTableView      *m_magView = nullptr;
    BpqCanvasWidget *m_mapPlan = nullptr;
    BpqCanvasWidget *m_mapProf = nullptr;
    BpqCanvasWidget *m_waves = nullptr;
    BpqCanvasWidget *m_axis = nullptr;
    QScrollBar      *m_tracesScroll = nullptr;   /* canal visible (per_screen) */
    BpqCanvasWidget *m_magCanvas = nullptr;
    BpqCanvasWidget *m_magAxis = nullptr;
    /* --- magnitudes (fase 6.1) --- */
    BpqMagModel              *m_magModel = nullptr;
    BpqMagPanel              *m_magPanel = nullptr;
    QVector<CsnhypodbpMagRow> m_magRows;       /* todas: canvas, estado, ancla */
    QVector<CsnhypodbpMagRow> m_magTableRows;  /* solo in_range: la tabla */
    QVector<BpqMagTrace>      m_magTrs;      /* una banda por canal */
    QByteArray                m_magStaLabel; /* `sta` estable para el canvas */
    BpqMagCtx                 m_magCtx{};
    int                       m_magKind = 0;
    CsnhypodbpMagStaStore     m_magSta;        /* TYPE_MAGNITUDE por estacion */
    int                       m_magDragGrab = 0;
    double                    m_magDragX = 0.0;      /* px de dispositivo */
    double                    m_magDragPressT = 0.0;
    double                    m_magDragT0 = 0.0, m_magDragT1 = 0.0;
    /* Dueno del arrastre: solo el (estacion, magnitud) que lo inicio recibe
     * los movimientos. Cambiar de contexto lo cancela (ver magDragCancel). */
    QString                   m_magDragSta;
    int                       m_magDragKind = -1;
    quint64                   m_magJob = 0;
    bool                      m_magBusy = false;
    unsigned                  m_magMask = 0;   /* mascara del calculate en vuelo */
    double                    m_magViewT0 = 0.0;      /* encuadre congelado del arrastre */
    double                    m_magViewSpan = 0.0;
    QLineEdit                *m_magGateLo = nullptr;
    QLineEdit                *m_magGateHi = nullptr;
    /* Origen de la P que entra al calculo de magnitudes (independiente de los
     * checks A/M/Teo del picking, que solo filtran el localizador). */
    QCheckBox                *m_magChkA = nullptr;
    QCheckBox                *m_magChkM = nullptr;
    QCheckBox                *m_magChkT = nullptr;
    QLabel                   *m_magBar = nullptr;
    QPushButton              *m_magCalc = nullptr;
    /* --- panel Mww (fase 6.3) --- */
    BpqMww                   *m_mww = nullptr;
    BpqMwwPanel              *m_mwwPanel = nullptr;
    QThread                  *m_mwwThread = nullptr;
    BpqMwwWorker             *m_mwwWorker = nullptr;
    quint64                   m_mwwJob = 0;
    bool                      m_mwwBusy = false;
    QString                   m_mwwKey;      /* identidad de la semilla vigente */
    QString                   m_mwwFor;      /* solucion del ultimo resultado */
    int                       m_mwwSaved = 0;     /* 1 = Mww+tensor en la BD */
    QString                   m_mwwSaveMsg;
    QVector<QRadioButton *>   m_magRadios;
    QLabel          *m_status = nullptr;
};

#endif /* BPQ_MAINWINDOW_H */
