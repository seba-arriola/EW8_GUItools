/*
 * bpq_preview_panel.cxx - Ver bpq_preview_panel.h.
 */
#include "bpq_preview_panel.h"

#include <QColor>
#include <QFont>
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cstring>

namespace {

/* Fondo del cuadro segun el estado (mismo criterio que el CSS del GTK:
 * `.preview-box.candidate` rojo muy leve, elegida ambar suave). */
const char *kCandidateBg = "#fdeceb";
const char *kSelectedBg  = "#fdf3dc";

}  // namespace

BpqPreviewPanel::BpqPreviewPanel(QWidget *parent) : QWidget(parent)
{
    QHBoxLayout *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);

    /* --- mitad izquierda: titulo, tiempo de origen y parametros ---------- */
    QFrame *lf = new QFrame(this);
    QVBoxLayout *lv = new QVBoxLayout(lf);
    lv->setContentsMargins(6, 4, 6, 4);
    lv->setSpacing(2);
    lf->setObjectName(QStringLiteral("preview_left"));
    m_left = lf;

    m_title = new QLabel(lf);
    m_title->setObjectName(QStringLiteral("preview_title"));
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    lv->addWidget(m_title);

    {
        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        m_otime = new QLabel(lf);
        m_otime->setObjectName(QStringLiteral("preview_otime"));
        m_busy = new QLabel(lf);
        m_busy->setObjectName(QStringLiteral("preview_busy"));
        QPalette bp = m_busy->palette();
        bp.setColor(QPalette::WindowText, QColor("#7a3ea8"));
        m_busy->setPalette(bp);
        row->addWidget(m_otime, 0);
        row->addWidget(m_busy, 1);
        lv->addLayout(row);
    }

    m_err = new QLabel(lf);
    m_err->setObjectName(QStringLiteral("preview_err"));
    m_err->setWordWrap(true);
    {
        QPalette ep = m_err->palette();
        ep.setColor(QPalette::WindowText, QColor("#a02020"));
        m_err->setPalette(ep);
    }
    lv->addWidget(m_err);

    {
        QGridLayout *g = new QGridLayout;
        g->setContentsMargins(0, 2, 0, 0);
        g->setHorizontalSpacing(8);
        g->setVerticalSpacing(1);
        for (int i = 0; i < CSNHYPODBP_PREVIEW_NPARAMS; i++) {
            QLabel *cap = new QLabel(QString::fromUtf8(csnhypodbp_preview_headers[i]), lf);
            m_val[i] = new QLabel(QStringLiteral("-"), lf);
            m_val[i]->setObjectName(QStringLiteral("preview_val_%1").arg(i));
            g->addWidget(cap, i, 0, Qt::AlignRight);
            g->addWidget(m_val[i], i, 1, Qt::AlignLeft);
        }
        lv->addLayout(g);
    }
    lv->addStretch(1);

    /* --- mitad derecha: magnitudes con sus radios ------------------------ */
    QFrame *rf = new QFrame(this);
    QVBoxLayout *rv = new QVBoxLayout(rf);
    rv->setContentsMargins(6, 4, 6, 4);
    rv->setSpacing(2);
    rf->setObjectName(QStringLiteral("preview_right"));
    m_right = rf;

    {
        QLabel *cap = new QLabel(QStringLiteral("Magnitude"), rf);
        QFont cf = cap->font();
        cf.setBold(true);
        cap->setFont(cf);
        rv->addWidget(cap);
    }

    QButtonGroup *grp = new QButtonGroup(this);
    grp->setExclusive(true);
    {
        QGridLayout *g = new QGridLayout;
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(6);
        g->setVerticalSpacing(1);
        for (int k = 0; k < CSNHYPODBP_PREVIEW_MAGS; k++) {
            const int i = CSNHYPODBP_PREVIEW_NPARAMS + k;
            m_radio[k] = new QRadioButton(
                QString::fromUtf8(csnhypodbp_preview_headers[i]), rf);
            m_radio[k]->setObjectName(QStringLiteral("preview_radio_%1").arg(k));
            m_radio[k]->setAutoExclusive(true);
            grp->addButton(m_radio[k], k);
            m_val[i] = new QLabel(QStringLiteral("-"), rf);
            m_val[i]->setObjectName(QStringLiteral("preview_val_%1").arg(i));
            g->addWidget(m_radio[k], k, 0);
            g->addWidget(m_val[i], k, 1, Qt::AlignLeft);
        }
        rv->addLayout(g);
    }
    connect(grp, &QButtonGroup::idClicked, this, [this](int id) {
        if (!m_updating) emit prefMagChanged(id);
    });
    rv->addStretch(1);

    h->addWidget(lf, 1);

    QFrame *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    h->addWidget(sep, 0);

    h->addWidget(rf, 1);

    /* --- Confirm / Discard (a la derecha del cuadro) --------------------- */
    {
        QVBoxLayout *bv = new QVBoxLayout;
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(4);
        m_confirm = new QPushButton(QStringLiteral("Confirm"), this);
        m_confirm->setObjectName(QStringLiteral("preview_confirm"));
        m_discard = new QPushButton(QStringLiteral("Discard"), this);
        m_discard->setObjectName(QStringLiteral("preview_discard"));
        connect(m_confirm, &QPushButton::clicked, this, &BpqPreviewPanel::confirmRequested);
        connect(m_discard, &QPushButton::clicked, this, &BpqPreviewPanel::discardRequested);
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

void BpqPreviewPanel::setBg(QWidget *w, const char *hexOrNull)
{
    if (!w) return;
    if (!hexOrNull) {
        w->setAutoFillBackground(false);
        w->setStyleSheet(QString());
        return;
    }
    /* background-color solo no basta en algunos estilos: se usa la paleta. */
    QPalette p = w->palette();
    p.setColor(QPalette::Window, QColor(QString::fromLatin1(hexOrNull)));
    w->setAutoFillBackground(true);
    w->setPalette(p);
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

    for (int i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
        m_val[i]->setText(QString::fromUtf8(v.val[i]));

    /* Radios: solo se pueden marcar las magnitudes que traen valor. */
    {
        const int has[CSNHYPODBP_PREVIEW_MAGS] = {
            std::strcmp(v.val[CSNHYPODBP_PREVIEW_NPARAMS + 0], "-") != 0,
            std::strcmp(v.val[CSNHYPODBP_PREVIEW_NPARAMS + 1], "-") != 0,
            std::strcmp(v.val[CSNHYPODBP_PREVIEW_NPARAMS + 2], "-") != 0,
            std::strcmp(v.val[CSNHYPODBP_PREVIEW_NPARAMS + 3], "-") != 0,
            std::strcmp(v.val[CSNHYPODBP_PREVIEW_NPARAMS + 4], "-") != 0,
        };

        m_updating = true;
        for (int k = 0; k < CSNHYPODBP_PREVIEW_MAGS; k++) {
            m_radio[k]->setEnabled(has[k] != 0);
            m_radio[k]->setChecked(prefMag == k && has[k]);
        }
        m_updating = false;
    }

    setBg(m_left, cand ? kCandidateBg
                       : (v.state == CSNHYPODBP_PREVIEW_CANDIDATE ? kSelectedBg
                                                                  : nullptr));
    setBg(m_right, cand ? kCandidateBg
                        : (v.state == CSNHYPODBP_PREVIEW_CANDIDATE ? kSelectedBg
                                                                   : nullptr));

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
    if (kind < 0 || kind >= CSNHYPODBP_PREVIEW_MAGS) return;
    m_updating = true;
    m_radio[kind]->setChecked(true);
    m_updating = false;
}
