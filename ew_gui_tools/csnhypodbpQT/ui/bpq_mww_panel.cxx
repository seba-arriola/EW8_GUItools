/*
 * bpq_mww_panel.cxx - Panel `Mww` (fase 6.3). Ver bpq_mww_panel.h.
 */
#include "bpq_mww_panel.h"

#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "bpq_canvas_widget.h"

BpqMwwPanel::BpqMwwPanel(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    v->setSpacing(3);

    m_status = new QLabel(QStringLiteral("Selecciona una solucion para calcular Mww"),
                          this);
    m_status->setObjectName("mww_status");
    v->addWidget(m_status);

    /* Dos cuadros lado a lado con la MISMA altura: [playera + resumen] y
     * [salida completa del motor]. */
    QSplitter *sh = new QSplitter(Qt::Horizontal, this);
    sh->setObjectName("split_mww");

    {
        QWidget *left = new QWidget(sh);
        QVBoxLayout *lv = new QVBoxLayout(left);
        lv->setContentsMargins(0, 0, 0, 0);

        m_ball = new BpqCanvasWidget(BPQ_SCENE_MWW_BALL, left);
        m_ball->setObjectName("canvas_mww_ball");
        m_ball->setMinimumHeight(BPQ_MWW_ROW_H / 2);
        m_summary = new QLabel(QStringLiteral("sin resultado: pulsa `Calculate`"),
                               left);
        m_summary->setObjectName("mww_summary");
        m_summary->setWordWrap(true);
        m_summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lv->addWidget(m_ball, 2);
        lv->addWidget(m_summary, 1);
        lv->setStretch(1, 1);
        left->setMinimumHeight(BPQ_MWW_ROW_H);
        sh->addWidget(left);
    }
    {
        m_output = new QLabel(this);
        m_output->setObjectName("mww_output");
        m_output->setWordWrap(false);
        m_output->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_output->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_output->setMinimumHeight(BPQ_MWW_ROW_H);
        m_output->setStyleSheet(QStringLiteral("font-family: monospace;"));
        sh->addWidget(m_output);
    }
    sh->setSizes(QList<int>() << 320 << 640);
    v->addWidget(sh, 1);

    /* Tabla de parametros: solo las celdas con campo propio son editables. */
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("mww_table");
    m_table->setHorizontalHeaderLabels(QStringList() << QStringLiteral("parametro")
                                                    << QStringLiteral("valor")
                                                    << QStringLiteral("origen"));
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setMinimumHeight(150);
    connect(m_table, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *it) {
        if (it) onCellEdited(it->row(), it->column());
    });
    v->addWidget(m_table, 1);

    {
        QHBoxLayout *bh = new QHBoxLayout;
        bh->setContentsMargins(0, 0, 0, 0);
        m_calc = new QPushButton(QStringLiteral("Calculate"), this);
        m_cancel = new QPushButton(QStringLiteral("Cancel"), this);
        m_calc->setObjectName("mww_calculate");
        m_cancel->setObjectName("mww_cancel");
        m_cancel->setEnabled(false);
        connect(m_calc, &QPushButton::clicked, this,
                &BpqMwwPanel::calculateRequested);
        connect(m_cancel, &QPushButton::clicked, this,
                &BpqMwwPanel::cancelRequested);
        bh->addStretch(1);
        bh->addWidget(m_calc);
        bh->addWidget(m_cancel);
        v->addLayout(bh, 0);
    }
}

void BpqMwwPanel::setMww(BpqMww *m)
{
    m_mww = m;
    refresh();
}

void BpqMwwPanel::setStatus(const QString &txt, bool error)
{
    if (!m_status) return;
    m_status->setText(txt);
    m_status->setStyleSheet(error ? QStringLiteral("color: #b00020;")
                                  : QStringLiteral("color: #1b5e20;"));
}

void BpqMwwPanel::setBusy(bool on)
{
    m_busy = on;
    if (m_calc) m_calc->setEnabled(!on && m_mww && bpq_mww_seeded(m_mww));
    if (m_cancel) m_cancel->setEnabled(on);
    /* Mientras el motor corre, su hilo escribe sobre `m_mww`: editar la tabla a
     * la vez seria una carrera, asi que se deshabilita. */
    if (m_table) m_table->setEnabled(!on);
}

void BpqMwwPanel::rebuildTable()
{
    int i, n = 0;

    if (!m_table) return;
    m_rows.resize(32);
    if (m_mww) n = bpq_mww_rows(m_mww, m_rows.data(), m_rows.size());
    m_rows.resize(n);

    const QSignalBlocker block(m_table);   /* llenar no es editar */
    m_table->setRowCount(n);
    for (i = 0; i < n; i++) {
        QTableWidgetItem *nm = new QTableWidgetItem(QString::fromUtf8(m_rows[i].name));
        QTableWidgetItem *vl = new QTableWidgetItem(QString::fromUtf8(m_rows[i].value));
        QTableWidgetItem *sr = new QTableWidgetItem(QString::fromUtf8(m_rows[i].source));

        nm->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        sr->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (m_rows[i].field == 0)
            vl->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, 0, nm);
        m_table->setItem(i, 1, vl);
        m_table->setItem(i, 2, sr);
    }
    m_table->resizeColumnsToContents();
}

void BpqMwwPanel::refresh()
{
    const bool seeded = m_mww && bpq_mww_seeded(m_mww);

    rebuildTable();
    if (m_ball) {
        m_ball->setContext(m_mww);
        m_ball->requestRepaint();
    }
    if (m_summary)
        m_summary->setText(QString::fromUtf8(m_mww ? bpq_mww_summary(m_mww)
                                                   : "sin configuracion Mww"));
    if (m_output)
        m_output->setText(QString::fromUtf8(m_mww ? bpq_mww_log(m_mww) : ""));
    if (m_calc) m_calc->setEnabled(!m_busy && seeded);
    if (!seeded && !m_busy)
        setStatus(QStringLiteral("Selecciona una solucion con magnitud para calcular Mww"),
                  false);
}

/* Una celda editada: el texto va al nucleo, que valida rango y formato. Si no
 * vale se descarta el campo (vuelve a la semilla) y se repone el texto. */
void BpqMwwPanel::onCellEdited(int row, int col)
{
    if (!m_mww || col != 1 || row < 0 || row >= m_rows.size()) return;
    {
        const int field = m_rows[row].field;
        const QString txt = m_table->item(row, col) ? m_table->item(row, col)->text()
                                                    : QString();
        const bool ok = bpq_mww_edit(m_mww, field, txt.toUtf8().constData());

        if (!ok)
            setStatus(QStringLiteral("valor invalido para %1 (se descarta y vuelve "
                                     "al de la solucion/'.d')")
                          .arg(QString::fromUtf8(m_rows[row].name)),
                      true);
        else
            setStatus(QStringLiteral("parametros listos"), false);
    }
    /* La tabla se rearma SIEMPRE: si el texto no valia, el valor vigente es el
     * de la semilla y la celda debe mostrarlo (nunca un prefijo a medias). */
    rebuildTable();
    if (m_calc) m_calc->setEnabled(bpq_mww_seeded(m_mww) && !m_busy);
}

/* ------------------------------------------------------------------------- */

BpqMwwWorker::BpqMwwWorker(QObject *parent) : QObject(parent) {}

void BpqMwwWorker::runJob(quint64 id, BpqMww *m, int dryRun)
{
    QElapsedTimer t;
    char err[512] = "";
    BpqMwwResult *res;
    bool ok;

    t.start();
    res = bpq_mww_run(m, dryRun, err, sizeof(err));
    ok = (res != NULL) && bpq_mww_result_ok(res);
    emit finished(id, ok, QString::fromUtf8(err), t.elapsed() / 1000.0);
}
