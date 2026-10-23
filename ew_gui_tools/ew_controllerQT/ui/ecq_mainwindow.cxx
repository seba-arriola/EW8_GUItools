/* Ventana principal de ew_controllerQT. Ver ui/ecq_mainwindow.h.
 *
 * Toda la logica (estado, sensibilidad, protocolo, editor del .d, resolucion de
 * logs) vive en c/ecq_*.c: aqui solo hay widgets. Las unicas decisiones que se
 * toman aqui son de presentacion (que se pinta y cuando se congela).
 */
#include "ecq_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

#include "ewguiqt/confirm.h"
#include "ewguiqt/list_select.h"
#include "ewguiqt/log_view.h"
#include "ewguiqt/table.h"
#include "ewguiqt/theme.h"

#include "ecq_capi.h"

namespace {

const int kPollMs = 500;      /* lectura del anillo (GTK: on_read cada 500 ms) */
const int kTickMs = 1000;     /* latido + peticion de estado */
const int kAttachMs = 2000;   /* reintento de attach (GTK: on_try_attach) */
const int kLogTail = 32768;   /* LOG_TAIL del modulo GTK */

QTableView *make_table_view(const QString &name)
{
    auto *v = new QTableView;
    v->setObjectName(name);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setSortingEnabled(false);           /* el orden lo lleva el SortProxy */
    v->setShowGrid(true);
    v->setAlternatingRowColors(false);
    v->setWordWrap(false);
    v->setTextElideMode(Qt::ElideRight);
    v->verticalHeader()->setVisible(false);
    v->horizontalHeader()->setSectionsClickable(true);
    v->horizontalHeader()->setStretchLastSection(true);
    v->horizontalHeader()->setMinimumSectionSize(40);
    /* Acotado: una tabla que pida mucho ancho infla el minimo de la ventana
     * (trampa T6 de WSLg, AGENTS §7). */
    v->setMinimumWidth(180);
    return v;
}

void set_cell(QStandardItemModel *m, int row, int col, const QString &text)
{
    QStandardItem *it = m->item(row, col);
    if (!it) {
        it = new QStandardItem;
        it->setEditable(false);
        m->setItem(row, col, it);
    }
    it->setText(text);
}

} /* namespace */

EcqMainWindow::EcqMainWindow(const EcqConfig *cfg, QWidget *parent)
    : QMainWindow(parent)
{
    if (cfg)
        m_cfg = *cfg;
    m_core = ecq_core_new(&m_cfg);

    buildUi();

    applyHeader();
    applyTable(true);
    applyButtons();
    applyStatusText(tr("Waiting for the CONTROL_RING ring (start startstop)..."));

    m_readTimer = new QTimer(this);
    m_readTimer->setInterval(kPollMs);
    connect(m_readTimer, &QTimer::timeout, this, &EcqMainWindow::onRead);
    m_readTimer->start();

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(kTickMs);
    connect(m_tickTimer, &QTimer::timeout, this, &EcqMainWindow::onTick);
    m_tickTimer->start();

    m_attachTimer = new QTimer(this);
    m_attachTimer->setInterval(kAttachMs);
    connect(m_attachTimer, &QTimer::timeout, this, &EcqMainWindow::onTryAttach);
    m_attachTimer->start();

    QTimer::singleShot(0, this, &EcqMainWindow::onTryAttach);
}

EcqMainWindow::~EcqMainWindow()
{
    if (m_cfgFile)
        ecq_cfg_free(m_cfgFile);
    ecq_core_free(m_core);
}

/* ------------------------------------------------------------------------- */
/* Construccion de la UI                                                     */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("ew_controllerQT"));
    resize(980, 720);

    /* Menu nativo (QMenuBar): el modulo GTK usa una ventana modal para evitar
     * el bug de popups de WSLg; Qt no lo necesita (misma decision que
     * csnhypodbpQT, docs/testLibQT.md). */
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *dark = viewMenu->addAction(tr("Dark mode"));
    dark->setCheckable(true);
    dark->setChecked(ewguiqt::dark_mode(*qApp));
    connect(dark, &QAction::toggled, this, [](bool on) {
        ewguiqt::set_dark_mode(*qApp, on);
    });
    viewMenu->addSeparator();
    QAction *refresh = viewMenu->addAction(tr("Refresh"));
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refresh, &QAction::triggered, this, &EcqMainWindow::onRefresh);

    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(6, 6, 6, 0);

    m_header = new QLabel(central);
    m_header->setTextFormat(Qt::PlainText);
    m_header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    /* Sin wrap, el minimumSizeHint del QLabel es el texto COMPLETO: con el
     * hostname/version reales la ventana no podia encoger (trampa WSLg). */
    m_header->setWordWrap(true);
    outer->addWidget(m_header);

    auto *split = new QSplitter(Qt::Horizontal, central);

    /* --- columna izquierda: pestanas Modules | Configuration ------------- */
    auto *left = new QWidget(split);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(left);

    /* Modules */
    auto *modsPage = new QWidget;
    auto *mv = new QVBoxLayout(modsPage);
    m_modView = make_table_view(QStringLiteral("modules"));
    m_modModel = new QStandardItemModel(0, 4, this);
    m_modModel->setHorizontalHeaderLabels({tr("Module"), tr("PID"),
                                           tr("Status"), tr("Details")});
    m_modProxy = new ewguiqt::SortProxy(this);
    m_modProxy->setSourceModel(m_modModel);
    m_modProxy->setColumnKinds({ewguiqt::SortKind::Text, ewguiqt::SortKind::Number,
                                ewguiqt::SortKind::Text, ewguiqt::SortKind::Text},
                               0 /* desempate por Module */);
    m_modView->setModel(m_modProxy);
    ewguiqt::wire_header_sort(m_modView, m_modProxy, 0);
    connect(m_modView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &EcqMainWindow::onSelectionChanged);
    mv->addWidget(m_modView, 1);

    auto *btns = new QHBoxLayout;
    m_btnStop = new QPushButton(tr("Stop"));
    m_btnRestart = new QPushButton(tr("Restart"));
    m_btnStart = new QPushButton(tr("Start"));
    m_btnReconfig = new QPushButton(tr("Reconfig"));
    m_btnRefresh = new QPushButton(tr("Refresh"));
    m_btnStop->setObjectName(QStringLiteral("btn_stop"));
    m_btnRestart->setObjectName(QStringLiteral("btn_restart"));
    m_btnStart->setObjectName(QStringLiteral("btn_start"));
    m_btnReconfig->setObjectName(QStringLiteral("btn_reconfig"));
    m_btnRefresh->setObjectName(QStringLiteral("btn_refresh"));
    connect(m_btnStop, &QPushButton::clicked, this, &EcqMainWindow::onStop);
    connect(m_btnRestart, &QPushButton::clicked, this, &EcqMainWindow::onRestart);
    connect(m_btnStart, &QPushButton::clicked, this, &EcqMainWindow::onStart);
    connect(m_btnReconfig, &QPushButton::clicked, this, &EcqMainWindow::onReconfig);
    connect(m_btnRefresh, &QPushButton::clicked, this, &EcqMainWindow::onRefresh);
    btns->addWidget(m_btnStop);
    btns->addWidget(m_btnRestart);
    btns->addWidget(m_btnStart);
    btns->addStretch(1);
    btns->addWidget(m_btnReconfig);
    btns->addWidget(m_btnRefresh);
    mv->addLayout(btns);
    m_tabs->addTab(modsPage, tr("Modules"));

    /* Configuration */
    auto *cfgPage = new QWidget;
    auto *cv = new QVBoxLayout(cfgPage);
    auto *cfgTop = new QHBoxLayout;
    m_cfgPick = new QPushButton(tr("Choose module..."));
    m_cfgPick->setObjectName(QStringLiteral("cfg_pick"));
    connect(m_cfgPick, &QPushButton::clicked, this, &EcqMainWindow::onPickCfg);
    cfgTop->addWidget(new QLabel(tr("Module:")));
    cfgTop->addWidget(m_cfgPick, 1);
    cv->addLayout(cfgTop);
    m_cfgPath = new QLabel;
    m_cfgPath->setTextFormat(Qt::PlainText);
    m_cfgPath->setWordWrap(true);
    cv->addWidget(m_cfgPath);
    m_cfgMsg = new QLabel;
    m_cfgMsg->setTextFormat(Qt::PlainText);
    m_cfgMsg->setWordWrap(true);
    cv->addWidget(m_cfgMsg);

    auto *cfgBody = new QWidget;
    m_cfgGrid = new QGridLayout(cfgBody);
    m_cfgScroll = new QScrollArea;
    m_cfgScroll->setWidgetResizable(true);
    m_cfgScroll->setWidget(cfgBody);
    cv->addWidget(m_cfgScroll, 1);

    auto *cfgBtns = new QHBoxLayout;
    auto *save = new QPushButton(tr("Save"));
    auto *reconf = new QPushButton(tr("Reconfigure"));
    auto *reload = new QPushButton(tr("Reload"));
    save->setObjectName(QStringLiteral("cfg_save"));
    reconf->setObjectName(QStringLiteral("cfg_reconfig"));
    reload->setObjectName(QStringLiteral("cfg_reload"));
    connect(save, &QPushButton::clicked, this, &EcqMainWindow::onCfgSave);
    connect(reconf, &QPushButton::clicked, this, &EcqMainWindow::onCfgReconfig);
    connect(reload, &QPushButton::clicked, this, &EcqMainWindow::onCfgReload);
    cfgBtns->addWidget(save);
    cfgBtns->addWidget(reconf);
    cfgBtns->addStretch(1);
    cfgBtns->addWidget(reload);
    cv->addLayout(cfgBtns);
    m_tabs->addTab(cfgPage, tr("Configuration"));

    connect(m_tabs, &QTabWidget::currentChanged, this, &EcqMainWindow::onTabChanged);
    lv->addWidget(m_tabs);
    split->addWidget(left);

    /* --- columna derecha: Rings arriba, Logs abajo ----------------------- */
    auto *right = new QSplitter(Qt::Vertical, split);

    auto *ringsPage = new QWidget(right);
    auto *rv = new QVBoxLayout(ringsPage);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->addWidget(new QLabel(tr("Rings")));
    m_ringView = make_table_view(QStringLiteral("rings"));
    m_ringView->setSelectionMode(QAbstractItemView::NoSelection);
    m_ringModel = new QStandardItemModel(0, 3, this);
    m_ringModel->setHorizontalHeaderLabels({tr("Ring"), tr("Key"), tr("Size KB")});
    m_ringProxy = new ewguiqt::SortProxy(this);
    m_ringProxy->setSourceModel(m_ringModel);
    m_ringProxy->setColumnKinds({ewguiqt::SortKind::Text, ewguiqt::SortKind::Number,
                                 ewguiqt::SortKind::Number});
    m_ringView->setModel(m_ringProxy);
    ewguiqt::wire_header_sort(m_ringView, m_ringProxy, 0);
    rv->addWidget(m_ringView, 1);
    right->addWidget(ringsPage);

    auto *logPage = new QWidget(right);
    auto *vv = new QVBoxLayout(logPage);
    vv->setContentsMargins(0, 0, 0, 0);
    auto *logTop = new QHBoxLayout;
    m_logPick = new QPushButton(tr("Choose module..."));
    m_logPick->setObjectName(QStringLiteral("log_pick"));
    connect(m_logPick, &QPushButton::clicked, this, &EcqMainWindow::onPickLog);
    logTop->addWidget(new QLabel(tr("Log:")));
    logTop->addWidget(m_logPick, 1);
    vv->addLayout(logTop);
    m_logView = new ewguiqt::LogView;
    m_logView->setObjectName(QStringLiteral("log"));
    m_logView->setTailBytes(kLogTail);
    vv->addWidget(m_logView, 1);
    right->addWidget(logPage);

    right->setStretchFactor(0, 0);
    right->setStretchFactor(1, 1);
    right->setSizes({320, 360});
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({560, 400});

    outer->addWidget(split, 1);
    setCentralWidget(central);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    /* Ignored: el texto de la statusbar no puede fijar el ancho minimo de la
     * ventana (se recorta, no se pierde: es seleccionable). */
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(m_status, 1);

    setMinimumSize(420, 320);
}

/* ------------------------------------------------------------------------- */
/* Pintado                                                                   */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::applyHeader()
{
    const EcqTable *t = ecq_table(m_core);
    if (!t)
        return;
    m_header->setText(tr("EARTHWORM SYSTEM STATUS\n%1   Start: %2   Current: %3   Disk: %4   Version: %5")
                          .arg(QString::fromUtf8(t->hostname),
                               QString::fromUtf8(t->start),
                               QString::fromUtf8(t->current),
                               QString::fromUtf8(t->disk),
                               QString::fromUtf8(t->version)));
}

void EcqMainWindow::applyTable(bool force)
{
    const EcqTable *t = ecq_table(m_core);
    if (!t)
        return;
    if (!force && t->seq == m_appliedSeq)
        return;
    m_appliedSeq = t->seq;

    const int selPid = selectedPid();

    {
        QSignalBlocker block(m_modView->selectionModel());
        m_modModel->setRowCount(t->nmods);
        for (int i = 0; i < t->nmods; ++i) {
            set_cell(m_modModel, i, 0, QString::fromUtf8(t->mods[i].name));
            set_cell(m_modModel, i, 1, QString::number(t->mods[i].pid));
            set_cell(m_modModel, i, 2, QString::fromUtf8(t->mods[i].status));
            set_cell(m_modModel, i, 3, QString::fromUtf8(t->mods[i].details));
        }
    }

    m_ringModel->setRowCount(t->nrings);
    for (int i = 0; i < t->nrings; ++i) {
        set_cell(m_ringModel, i, 0, QString::fromUtf8(t->rings[i].name));
        set_cell(m_ringModel, i, 1, QString::number(t->rings[i].key));
        set_cell(m_ringModel, i, 2, QString::number(t->rings[i].size));
    }

    selectPid(selPid);
}

void EcqMainWindow::applyButtons()
{
    const int pid = selectedPid();
    m_btnStop->setEnabled(ecq_can(m_core, ECQ_ACT_STOP, pid) != 0);
    m_btnRestart->setEnabled(ecq_can(m_core, ECQ_ACT_RESTART, pid) != 0);
    m_btnStart->setEnabled(ecq_can(m_core, ECQ_ACT_START, pid) != 0);
    m_btnReconfig->setEnabled(ecq_can(m_core, ECQ_ACT_RECONFIG, 0) != 0);
    m_btnRefresh->setEnabled(true);
}

void EcqMainWindow::applyStatusText(const QString &text)
{
    if (m_status)
        m_status->setText(text);
}

void EcqMainWindow::applySelection()
{
    const int pid = selectedPid();
    if (pid > 0)
        ecq_select_pid(m_core, pid);
    applyButtons();
}

/* ------------------------------------------------------------------------- */
/* Timers                                                                    */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::onTryAttach()
{
    if (!m_core)
        return;
    if (ecq_ring_attached(m_core)) {
        m_attachTimer->stop();
        return;
    }
    if (ecq_ring_attach(m_core) == 0) {
        m_attachTimer->stop();
        m_attachedOnce = true;
        applyStatusText(tr("Connected to the control ring."));
        ecq_request_status(m_core);
        return;
    }
    if (!m_attachedOnce)
        applyStatusText(tr("Waiting for the CONTROL_RING ring (start startstop)..."));
}

void EcqMainWindow::onRead()
{
    if (!m_core || !ecq_ring_attached(m_core))
        return;

    const int applied = ecq_read(m_core, 0);
    if (applied <= 0)
        return;

    const EcqTable *t = ecq_table(m_core);
    if (m_frozen)
        return;   /* Configuration activa: se parsea, no se repinta */

    applyTable(false);
    applyHeader();
    applyButtons();
    refreshLog();

    QString line = tr("Updated %1 UTC - %2 modules")
                       .arg(QString::fromUtf8(t->current))
                       .arg(t->nmods);
    const QString target = QString::fromUtf8(ecq_target_label(m_core));
    if (!target.isEmpty())
        line += tr(" - target: %1").arg(target);
    applyStatusText(line);
}

void EcqMainWindow::onTick()
{
    if (!m_core)
        return;

    const time_t now = time(nullptr);
    if (ecq_ring_attached(m_core))
        (void)ecq_tick(m_core, now);

    const int nr = ecq_noreply(m_core, now);
    if (nr == m_lastNoreply)
        return;
    m_lastNoreply = nr;

    if (nr) {
        applyStatusText(tr("NO RESPONSE from startstop"));
        applyButtons();
        if (!m_frozen)
            applyTable(true);
        return;
    }
    if (m_attachedOnce) {
        applyButtons();
        if (!m_frozen)
            applyTable(true);
    }
}

/* ------------------------------------------------------------------------- */
/* Tabla de modulos                                                          */
/* ------------------------------------------------------------------------- */

int EcqMainWindow::selectedPid() const
{
    if (!m_modView || !m_modView->selectionModel())
        return 0;
    const QModelIndex idx = m_modView->selectionModel()->currentIndex();
    if (!idx.isValid())
        return 0;
    return m_modProxy->index(idx.row(), 1).data().toInt();
}

void EcqMainWindow::selectPid(int pid)
{
    if (!m_modView)
        return;
    {
        QSignalBlocker block(m_modView->selectionModel());
        bool found = false;
        if (pid > 0) {
            for (int r = 0; r < m_modProxy->rowCount(); ++r) {
                if (m_modProxy->index(r, 1).data().toInt() == pid) {
                    m_modView->selectRow(r);
                    m_modView->scrollTo(m_modProxy->index(r, 0));
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            m_modView->clearSelection();
    }
    /* La seleccion programatica no emite selectionChanged (va bloqueada para no
     * reentrar en el rebuild): el estado del core y los botones se sincronizan
     * aqui, o `ECQ_SELECT_PID` dejaria los botones del estado anterior. */
    (void)ecq_select_pid(m_core, pid);
    applyButtons();
}

void EcqMainWindow::onSelectionChanged(const QItemSelection &, const QItemSelection &)
{
    applySelection();
}

const EcqRow *EcqMainWindow::rowOfPid(int pid) const
{
    const EcqTable *t = ecq_table(m_core);
    if (!t || pid <= 0)
        return nullptr;
    for (int i = 0; i < t->nmods; ++i)
        if (t->mods[i].pid == pid)
            return &t->mods[i];
    return nullptr;
}

/* ------------------------------------------------------------------------- */
/* Acciones                                                                  */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::confirmAction(EcqAction action)
{
    const int pid = selectedPid();
    const EcqRow *r = rowOfPid(pid);
    if (!r)
        return;

    const QString where = tr("pid %1, %2").arg(pid).arg(QString::fromUtf8(r->cfgfile));
    const char *verb = action == ECQ_ACT_STOP ? "Stop"
                     : action == ECQ_ACT_RESTART ? "Restart" : "Start";
    const QString msg = tr("%1 the module '%2' (%3)?")
                            .arg(QString::fromLatin1(verb),
                                 QString::fromUtf8(r->name), where);
    if (!ewguiqt::confirm(this, msg))
        return;

    char detail[128] = "";
    if (ecq_action(m_core, action, pid, detail, sizeof(detail)) != 0) {
        applyStatusText(tr("Not connected to the control ring."));
        return;
    }
    applyStatusText(QString::fromUtf8(detail));
}

void EcqMainWindow::onStop()    { confirmAction(ECQ_ACT_STOP); }
void EcqMainWindow::onRestart() { confirmAction(ECQ_ACT_RESTART); }
void EcqMainWindow::onStart()   { confirmAction(ECQ_ACT_START); }

void EcqMainWindow::onReconfig()
{
    char detail[128] = "";
    if (ecq_action(m_core, ECQ_ACT_RECONFIG, 0, detail, sizeof(detail)) != 0) {
        applyStatusText(tr("Not connected to the control ring."));
        return;
    }
    applyStatusText(QString::fromUtf8(detail));
}

void EcqMainWindow::onRefresh()
{
    applyStatusText(tr("Updating..."));
    (void)ecq_request_status(m_core);
}

/* ------------------------------------------------------------------------- */
/* Pestana Configuration                                                     */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::onTabChanged(int index)
{
    const bool nowCfg = (index == 1);
    if (m_frozen && !nowCfg && m_cfgFile && ecq_cfg_dirty(m_cfgFile)) {
        if (!ewguiqt::confirm(this,
                tr("There are unsaved changes in the configuration. Discard them?"))) {
            QSignalBlocker block(m_tabs);
            m_tabs->setCurrentIndex(1);
            return;
        }
    }
    m_frozen = nowCfg;
    if (nowCfg)
        return;

    /* Al volver: se repinta lo que cambio mientras estaba congelado. */
    applyTable(true);
    applyHeader();
    applyButtons();
    refreshLog();
}

void EcqMainWindow::onPickCfg()
{
    const EcqTable *t = ecq_table(m_core);
    if (!t || t->nmods == 0) {
        m_cfgMsg->setText(tr("No module status yet."));
        return;
    }

    QStringList labels;
    QVector<int> index;
    for (int i = 0; i < t->nmods; ++i) {
        char lab[200];
        ecq_module_label(m_core, i, lab, sizeof(lab));
        labels << QString::fromUtf8(lab);
        index << i;
    }

    int cur = 0;
    for (int i = 0; i < index.size(); ++i)
        if (t->mods[index.at(i)].pid == m_cfgPid)
            cur = i;

    const int chosen = ewguiqt::ListSelectDialog::pick(this, tr("Choose module"), labels, cur);
    if (chosen < 0 || chosen >= index.size())
        return;

    const EcqRow *r = &t->mods[index.at(chosen)];
    if (m_cfgFile) {
        ecq_cfg_free(m_cfgFile);
        m_cfgFile = nullptr;
    }
    m_cfgName = QString::fromUtf8(r->cfgfile);
    m_cfgPid = r->pid;
    m_cfgPick->setText(QString::fromUtf8(r->name));

    char err[512] = "";
    m_cfgFile = ecq_cfg_open(r->cfgfile, err, sizeof(err));
    rebuildCfgGrid();
    if (!m_cfgFile) {
        m_cfgPath->setText(QString::fromUtf8(ecq_cfg_path(nullptr)));
        m_cfgMsg->setText(QString::fromUtf8(err));
        return;
    }
    m_cfgPath->setText(QString::fromUtf8(ecq_cfg_path(m_cfgFile)));
    m_cfgMsg->setText(tr("Loaded: %1 (%2 editable variables)")
                          .arg(QString::fromUtf8(ecq_cfg_path(m_cfgFile)))
                          .arg(ecq_cfg_nvars(m_cfgFile)));
}

void EcqMainWindow::rebuildCfgGrid()
{
    while (QLayoutItem *item = m_cfgGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_cfgEdits.clear();
    if (!m_cfgFile)
        return;

    for (int v = 0; v < ecq_cfg_nvars(m_cfgFile); ++v) {
        auto *key = new QLabel(QStringLiteral("<b>%1</b>")
                                   .arg(QString::fromUtf8(ecq_cfg_key(m_cfgFile, v))));
        key->setTextFormat(Qt::RichText);
        auto *edit = new QLineEdit(QString::fromUtf8(ecq_cfg_value(m_cfgFile, v)));
        edit->setMaxLength(512);
        connect(edit, &QLineEdit::textEdited, this, &EcqMainWindow::onCfgEdited);
        m_cfgGrid->addWidget(key, v, 0);
        m_cfgGrid->addWidget(edit, v, 1);
        m_cfgEdits.append(edit);
    }
    m_cfgGrid->setColumnStretch(1, 1);
    m_cfgGrid->setRowStretch(ecq_cfg_nvars(m_cfgFile), 1);
}

void EcqMainWindow::onCfgEdited()
{
    if (!m_cfgFile)
        return;
    for (int v = 0; v < m_cfgEdits.size() && v < ecq_cfg_nvars(m_cfgFile); ++v) {
        const QString text = m_cfgEdits.at(v)->text();
        if (text != QString::fromUtf8(ecq_cfg_value(m_cfgFile, v)))
            (void)ecq_cfg_set(m_cfgFile, v, text.toUtf8().constData());
    }
    if (ecq_cfg_dirty(m_cfgFile))
        m_cfgMsg->setText(tr("There are unsaved changes..."));
}

void EcqMainWindow::onCfgSave()
{
    if (!m_cfgFile)
        return;
    char msg[512] = "";
    (void)ecq_cfg_save(m_cfgFile, msg, sizeof(msg));
    m_cfgMsg->setText(QString::fromUtf8(msg));
}

void EcqMainWindow::onCfgReconfig()
{
    if (!m_cfgFile)
        return;
    onCfgSave();
    if (ecq_cfg_dirty(m_cfgFile))
        return;

    const EcqRow *r = rowOfPid(m_cfgPid);
    if (!r)
        return;
    const QString where = tr("pid %1, %2").arg(m_cfgPid).arg(QString::fromUtf8(r->cfgfile));
    if (!ewguiqt::confirm(this, tr("Restart '%1' (%2) to apply the saved config?")
                                    .arg(QString::fromUtf8(r->name), where)))
        return;

    char detail[128] = "";
    if (std::strcmp(r->name, "startstop") == 0) {
        /* startstop no se reinicia: relee su config (TYPE_RECONFIG). */
        if (ecq_action(m_core, ECQ_ACT_RECONFIG, 0, detail, sizeof(detail)) != 0) {
            m_cfgMsg->setText(tr("Not connected to the control ring."));
            return;
        }
        m_cfgMsg->setText(tr("Reconfig (startstop) sent... startstop re-reads its config."));
        return;
    }
    if (ecq_action(m_core, ECQ_ACT_RESTART, m_cfgPid, detail, sizeof(detail)) != 0) {
        m_cfgMsg->setText(tr("Not connected to the control ring."));
        return;
    }
    m_cfgMsg->setText(tr("Restart sent... Config saved. The module re-reads its config on restart."));
}

void EcqMainWindow::onCfgReload()
{
    if (m_cfgName.isEmpty())
        return;
    if (m_cfgFile) {
        ecq_cfg_free(m_cfgFile);
        m_cfgFile = nullptr;
    }
    char err[512] = "";
    m_cfgFile = ecq_cfg_open(m_cfgName.toLocal8Bit().constData(), err, sizeof(err));
    rebuildCfgGrid();
    if (!m_cfgFile) {
        m_cfgMsg->setText(QString::fromUtf8(err));
        return;
    }
    m_cfgMsg->setText(tr("Loaded: %1 (%2 editable variables)")
                          .arg(QString::fromUtf8(ecq_cfg_path(m_cfgFile)))
                          .arg(ecq_cfg_nvars(m_cfgFile)));
}

/* ------------------------------------------------------------------------- */
/* Pestana Logs                                                              */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::onPickLog()
{
    const EcqTable *t = ecq_table(m_core);
    if (!t || t->nmods == 0) {
        m_logView->setStatus(tr("(no module status yet)"));
        return;
    }

    QStringList labels;
    QVector<int> pids;
    for (int i = 0; i < t->nmods; ++i) {
        char lab[200];
        ecq_module_label(m_core, i, lab, sizeof(lab));
        labels << QString::fromUtf8(lab);
        pids << t->mods[i].pid;
    }

    int cur = 0;
    for (int i = 0; i < pids.size(); ++i)
        if (pids.at(i) == m_logPid)
            cur = i;

    const int chosen = ewguiqt::ListSelectDialog::pick(this, tr("Choose module"), labels, cur);
    if (chosen < 0 || chosen >= pids.size())
        return;
    m_logPid = pids.at(chosen);
    refreshLog();
}

void EcqMainWindow::refreshLog()
{
    if (m_logPid <= 0 || !m_logView)
        return;
    const EcqRow *r = rowOfPid(m_logPid);
    if (!r)
        return;

    char path[600] = "";
    char err[512] = "";
    if (ecq_log_path(m_cfg.log_dir, r->name, r->config, path, sizeof(path),
                     err, sizeof(err)) != 0) {
        m_logView->setStatus(QString::fromUtf8(err));
        return;
    }
    (void)m_logView->loadPath(QString::fromUtf8(path));
}

/* ------------------------------------------------------------------------- */
/* Cierre                                                                    */
/* ------------------------------------------------------------------------- */

void EcqMainWindow::closeEvent(QCloseEvent *ev)
{
    if (m_cfgFile && ecq_cfg_dirty(m_cfgFile)) {
        if (!ewguiqt::confirm(this,
                tr("There are unsaved changes in the configuration. Discard them?"))) {
            ev->ignore();
            return;
        }
    }
    ev->accept();
}

/* ------------------------------------------------------------------------- */
/* Ganchos de test                                                           */
/* ------------------------------------------------------------------------- */

bool EcqMainWindow::waitForSeq(unsigned seq, int timeout_ms)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    for (;;) {
        const EcqTable *t = ecq_table(m_core);
        if (t && t->seq >= seq)
            return true;
        if (QDateTime::currentMSecsSinceEpoch() >= deadline)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

QString EcqMainWindow::dumpTree() const
{
    QString out;
    const EcqTable *t = ecq_table(m_core);

    out += QStringLiteral("panel \"window|0|0|%1|%2\"\n").arg(width()).arg(height());
    if (!t)
        return out;

    out += QStringLiteral("header \"%1|%2|%3|%4|%5\"\n")
               .arg(QString::fromUtf8(t->hostname), QString::fromUtf8(t->start),
                    QString::fromUtf8(t->current), QString::fromUtf8(t->disk),
                    QString::fromUtf8(t->version));
    for (int i = 0; i < t->nmods; ++i)
        out += QStringLiteral("row \"%1|%2|%3|%4|%5\"\n")
                   .arg(QString::fromUtf8(t->mods[i].name))
                   .arg(t->mods[i].pid)
                   .arg(QString::fromUtf8(t->mods[i].status))
                   .arg(QString::fromUtf8(t->mods[i].details))
                   .arg(QString::fromUtf8(t->mods[i].cfgfile));
    for (int i = 0; i < t->nrings; ++i)
        out += QStringLiteral("ring \"%1|%2|%3\"\n")
                   .arg(QString::fromUtf8(t->rings[i].name))
                   .arg(t->rings[i].key)
                   .arg(t->rings[i].size);

    const int pid = selectedPid();
    const EcqRow *r = rowOfPid(pid);
    out += QStringLiteral("target \"%1\"\n")
               .arg(r ? QStringLiteral("%1|%2").arg(QString::fromUtf8(r->name)).arg(pid)
                      : QString());

    if (m_modView)
        out += QStringLiteral("panel \"modules|%1|%2|%3|%4\"\n")
                   .arg(m_modView->mapTo(this, QPoint(0, 0)).x())
                   .arg(m_modView->mapTo(this, QPoint(0, 0)).y())
                   .arg(m_modView->width()).arg(m_modView->height());
    if (m_ringView)
        out += QStringLiteral("panel \"rings|%1|%2|%3|%4\"\n")
                   .arg(m_ringView->mapTo(this, QPoint(0, 0)).x())
                   .arg(m_ringView->mapTo(this, QPoint(0, 0)).y())
                   .arg(m_ringView->width()).arg(m_ringView->height());
    if (m_logView)
        out += QStringLiteral("panel \"log|%1|%2|%3|%4\"\n")
                   .arg(m_logView->mapTo(this, QPoint(0, 0)).x())
                   .arg(m_logView->mapTo(this, QPoint(0, 0)).y())
                   .arg(m_logView->width()).arg(m_logView->height());

    /* Sensibilidad: la MISMA tabla que el arnés del modulo GTK comprueba. */
    if (m_btnStop)
        out += QStringLiteral("button \"stop|%1\"\n").arg(m_btnStop->isEnabled() ? 1 : 0);
    if (m_btnRestart)
        out += QStringLiteral("button \"restart|%1\"\n").arg(m_btnRestart->isEnabled() ? 1 : 0);
    if (m_btnStart)
        out += QStringLiteral("button \"start|%1\"\n").arg(m_btnStart->isEnabled() ? 1 : 0);
    if (m_btnReconfig)
        out += QStringLiteral("button \"reconfig|%1\"\n").arg(m_btnReconfig->isEnabled() ? 1 : 0);
    return out;
}

QString EcqMainWindow::dumpLayout() const
{
    QString out = dumpTree();

    /* Mismo invariante que el arnes GTK: el hueco entre la tabla de modulos y
     * la de anillos no debe comerse la ventana, y los anillos no deben quedar
     * aplastados. */
    if (m_modView && m_ringView) {
        const QPoint modPos = m_modView->mapTo(this, QPoint(0, 0));
        const QPoint ringPos = m_ringView->mapTo(this, QPoint(0, 0));
        const int gap = ringPos.x() - (modPos.x() + m_modView->width());
        out += QStringLiteral("metric gap_details_ring %1\n").arg(gap);
        out += QStringLiteral("metric rings_height %1\n").arg(m_ringView->height());
    }
    out += QStringLiteral("metric window_min_width %1\n").arg(minimumSizeHint().width());
    out += QStringLiteral("metric window_min_height %1\n").arg(minimumSizeHint().height());
    return out;
}

bool EcqMainWindow::saveShot(const QString &path)
{
    const QPixmap pm = grab();
    return pm.save(path);
}
