/*
 * bpq_preview_panel.cxx - Ver bpq_preview_panel.h.
 *
 * Panel compacto: una linea de estado (titulo + Origin time + calculando) y una
 * tabla de DOS filas (encabezados / valores) con los parametros y las cinco
 * magnitudes. Sin relleno de fondo: el estado va en el color del titulo.
 */
#include "bpq_preview_panel.h"

#include <QButtonGroup>
#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <cstring>

BpqPreviewPanel::BpqPreviewPanel(QWidget *parent) : QWidget(parent)
{
    QHBoxLayout *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    QVBoxLayout *content = new QVBoxLayout;
    content->setContentsMargins(2, 2, 2, 2);
    content->setSpacing(3);

    /* --- linea de estado: titulo, Origin time y "calculando ..." ---------- */
    {
        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);

        m_title = new QLabel(this);
        m_title->setObjectName(QStringLiteral("preview_title"));
        QFont tf = m_title->font();
        tf.setBold(true);
        m_title->setFont(tf);

        m_otime = new QLabel(this);
        m_otime->setObjectName(QStringLiteral("preview_otime"));

        m_busy = new QLabel(this);
        m_busy->setObjectName(QStringLiteral("preview_busy"));
        {
            QPalette bp = m_busy->palette();
            bp.setColor(QPalette::WindowText, QColor("#7a3ea8"));
            m_busy->setPalette(bp);
        }

        row->addWidget(m_title, 0);
        row->addWidget(m_otime, 0);
        row->addWidget(m_busy, 1);
        content->addLayout(row);
    }

    /* --- tabla de 2 filas: encabezados + valores -------------------------- */
    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 1, 0, 0);
    m_grid->setHorizontalSpacing(12);
    m_grid->setVerticalSpacing(2);
    {
        QButtonGroup *grp = new QButtonGroup(this);
        grp->setExclusive(true);

        for (int i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++) {
            QLabel *cap = new QLabel(
                QString::fromUtf8(csnhypodbp_preview_headers[i]), this);
            QFont cf = cap->font();
            cf.setBold(true);
            cap->setFont(cf);
            cap->setObjectName(QStringLiteral("preview_head_%1").arg(i));
            m_grid->addWidget(cap, 0, i, Qt::AlignLeft);

            if (i < CSNHYPODBP_PREVIEW_NPARAMS) {
                m_val[i] = new QLabel(QStringLiteral("-"), this);
                m_val[i]->setObjectName(QStringLiteral("preview_val_%1").arg(i));
                /* Preferred (no Ignored): un label Ignored puede quedarse a
                 * ancho cero y recortar el valor ("10" -> "1"). El grid ya
                 * reparte el ancho de la ventana. */
                m_val[i]->setSizePolicy(QSizePolicy::Preferred,
                                        QSizePolicy::Preferred);
                m_val[i]->setMinimumWidth(0);
                m_grid->addWidget(m_val[i], 1, i, Qt::AlignLeft);
            } else {
                const int k = i - CSNHYPODBP_PREVIEW_NPARAMS;

                m_radio[k] = new QRadioButton(QStringLiteral("-"), this);
                m_radio[k]->setObjectName(
                    QStringLiteral("preview_radio_%1").arg(k));
                m_radio[k]->setAutoExclusive(true);
                m_radio[k]->setSizePolicy(QSizePolicy::Preferred,
                                          QSizePolicy::Fixed);
                m_radio[k]->setMinimumWidth(0);
                grp->addButton(m_radio[k], k);
                m_grid->addWidget(m_radio[k], 1, i, Qt::AlignLeft);
            }
            m_grid->setColumnStretch(i, 1);
        }
        connect(grp, &QButtonGroup::idClicked, this, [this](int id) {
            if (!m_updating) emit prefMagChanged(id);
        });
    }
    content->addLayout(m_grid);

    /* --- error real (solo cuando existe) ---------------------------------- */
    m_err = new QLabel(this);
    m_err->setObjectName(QStringLiteral("preview_err"));
    m_err->setWordWrap(true);
    {
        QPalette ep = m_err->palette();
        ep.setColor(QPalette::WindowText, QColor("#a02020"));
        m_err->setPalette(ep);
    }
    m_err->setVisible(false);
    content->addWidget(m_err);
    content->addStretch(1);

    h->addLayout(content, 1);

    /* --- Confirm / Discard (a la derecha) --------------------------------- */
    {
        QVBoxLayout *bv = new QVBoxLayout;
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(4);

        m_confirm = new QPushButton(QStringLiteral("Confirm"), this);
        m_confirm->setObjectName(QStringLiteral("preview_confirm"));
        m_discard = new QPushButton(QStringLiteral("Discard"), this);
        m_discard->setObjectName(QStringLiteral("preview_discard"));

        connect(m_confirm, &QPushButton::clicked, this,
                &BpqPreviewPanel::confirmRequested);
        connect(m_discard, &QPushButton::clicked, this,
                &BpqPreviewPanel::discardRequested);
        bv->addStretch(1);
        bv->addWidget(m_confirm, 0);
        bv->addWidget(m_discard, 0);
        bv->addStretch(1);
        h->addLayout(bv, 0);
    }

    /* Estado inicial: sin seleccion (guiones). */
    CsnhypodbpPreviewVals v;
    std::memset(&v, 0, sizeof(v));
    csnhypodbp_preview_clear(&v);
    render(v, -1);
}

void BpqPreviewPanel::render(const CsnhypodbpPreviewVals &v, int prefMag)
{
    const bool running = (v.state == CSNHYPODBP_PREVIEW_RUNNING);
    const bool error   = (v.state == CSNHYPODBP_PREVIEW_ERROR);
    const bool cand    = v.candidate != 0 &&
                         v.state == CSNHYPODBP_PREVIEW_CANDIDATE;

    m_title->setText(QString::fromUtf8(v.title));
    m_otime->setText(QString::fromUtf8(v.otime));
    m_busy->setText(running ? QString::fromUtf8(v.busy) : QString());
    m_busy->setVisible(running);
    m_err->setText(error ? QString::fromUtf8(v.err) : QString());
    m_err->setVisible(error);

    for (int i = 0; i < CSNHYPODBP_PREVIEW_NPARAMS; i++)
        m_val[i]->setText(QString::fromUtf8(v.val[i]));

    /* Magnitudes: el valor va DENTRO del radio (una sola fila); sin valor el
     * radio queda apagado y con guion. */
    {
        m_updating = true;
        for (int k = 0; k < CSNHYPODBP_PREVIEW_MAGS; k++) {
            const QString txt =
                QString::fromUtf8(v.val[CSNHYPODBP_PREVIEW_NPARAMS + k]);
            const bool has = (txt != QStringLiteral("-"));

            m_radio[k]->setText(txt);
            m_radio[k]->setEnabled(has);
            m_radio[k]->setChecked(prefMag == k && has);
        }
        m_updating = false;
    }

    /* Estado por color del titulo, sin fondo amarillo/rojo. */
    if (cand) m_title->setStyleSheet(QStringLiteral("color: #a02020;"));
    else if (v.state == CSNHYPODBP_PREVIEW_CANDIDATE)
        m_title->setStyleSheet(QStringLiteral("color: #8a5a00;"));
    else
        m_title->setStyleSheet(QString());

    /* Los textos cambian el `sizeHint` de los labels (de "-" a "-20.100"): sin
     * invalidar, el grid conserva el ancho del guion y recorta el valor. */
    if (m_grid) {
        m_grid->invalidate();
        m_grid->activate();   /* recalcula anchos aunque no haya event loop */
    }

    /* Los botones solo tienen sentido con una candidata del `locate`. */
    if (m_confirm) m_confirm->setEnabled(cand);
    if (m_discard) m_discard->setEnabled(cand);
}

int BpqPreviewPanel::prefMag() const
{
    for (int k = 0; k < CSNHYPODBP_PREVIEW_MAGS; k++)
        if (m_radio[k] && m_radio[k]->isChecked()) return k;
    return -1;
}

void BpqPreviewPanel::setPrefMag(int kind)
{
    if (kind < 0 || kind >= CSNHYPODBP_PREVIEW_MAGS || !m_radio[kind]) return;
    if (!m_radio[kind]->isEnabled()) return;   /* sin valor: no se marca */
    m_updating = true;
    m_radio[kind]->setChecked(true);
    m_updating = false;
}

QString BpqPreviewPanel::dump() const
{
    QStringList heads, vals;
    QString color = QStringLiteral("default");

    for (int i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++) {
        heads << QString::fromUtf8(csnhypodbp_preview_headers[i]);
        if (i < CSNHYPODBP_PREVIEW_NPARAMS) {
            vals << (m_val[i] ? m_val[i]->text() : QStringLiteral("-"));
        } else {
            const int k = i - CSNHYPODBP_PREVIEW_NPARAMS;
            const QString t = m_radio[k] ? m_radio[k]->text()
                                         : QStringLiteral("-");

            vals << ((m_radio[k] && m_radio[k]->isEnabled() &&
                      t != QStringLiteral("-"))
                         ? t
                         : QStringLiteral("-"));
        }
    }
    if (m_title) {
        if (m_title->styleSheet().contains(QStringLiteral("#a02020")))
            color = QStringLiteral("#a02020");
        else if (m_title->styleSheet().contains(QStringLiteral("#8a5a00")))
            color = QStringLiteral("#8a5a00");
    }
    return QStringLiteral("preview grid rows=2 cols=%1 headers=%2 values=%3 "
                          "bg=none title_color=%4\n")
        .arg(CSNHYPODBP_PREVIEW_NVALS)
        .arg(heads.join(QLatin1Char(',')))
        .arg(vals.join(QLatin1Char(',')))
        .arg(color);
}
