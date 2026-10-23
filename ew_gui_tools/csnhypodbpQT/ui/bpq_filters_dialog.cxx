/*
 * bpq_filters_dialog.cxx - Ver bpq_filters_dialog.h.
 */
#include "bpq_filters_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

BpqFiltersDialog::BpqFiltersDialog(const BpqConfig *cfg, int active,
                                   bool phaseZero, bool stalta, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("filters_dialog"));
    setWindowTitle(QStringLiteral("Filters"));

    QVBoxLayout *v = new QVBoxLayout(this);
    QGroupBox *box = new QGroupBox(QStringLiteral("Vista del picking"), this);
    QVBoxLayout *bv = new QVBoxLayout(box);

    m_grp = new QButtonGroup(this);
    m_grp->setExclusive(true);

    /* Raw primero: es el "apagado" del filtro. */
    {
        QRadioButton *r = new QRadioButton(QStringLiteral("Raw (sin filtro)"), box);

        r->setObjectName(QStringLiteral("filter_raw"));
        m_grp->addButton(r, 0);
        m_idx.append(-1);
        bv->addWidget(r);
        if (active < 0) r->setChecked(true);
    }

    if (cfg) {
        for (int i = 0; i < cfg->filtercfg.nf; i++) {
            char lbl[128] = "";

            csnhypodbp_filtercfg_format_preset(&cfg->filtercfg.f[i], lbl,
                                               sizeof(lbl));
            {
                QString txt = QString::fromUtf8(lbl);

                if (cfg->filtercfg.f[i].key)
                    txt += QStringLiteral("   [%1]")
                               .arg(QLatin1Char(cfg->filtercfg.f[i].key));
                QRadioButton *r = new QRadioButton(txt, box);

                r->setObjectName(QStringLiteral("filter_%1").arg(i));
                m_grp->addButton(r, m_idx.size());
                m_idx.append(i);
                bv->addWidget(r);
                if (i == active) r->setChecked(true);
            }
        }
    }
    v->addWidget(box);

    m_zero = new QCheckBox(QStringLiteral("FilterPhase zero (forward+backward)"), this);
    m_zero->setObjectName(QStringLiteral("filter_zero"));
    m_zero->setChecked(phaseZero);
    m_zero->setToolTip(QStringLiteral(
        "Fase cero = |H|^2 (orden efectivo doble). Apagado = causal, que retrasa "
        "el pico y sesga el clic."));
    v->addWidget(m_zero);

    m_stalta = new QCheckBox(QStringLiteral("STA/LTA (solo vista)"), this);
    m_stalta->setObjectName(QStringLiteral("filter_stalta"));
    m_stalta->setChecked(stalta);
    m_stalta->setToolTip(QStringLiteral(
        "Dibuja la curva CF y el disparo estimado; no crea picadas."));
    v->addWidget(m_stalta);

    {
        QLabel *hint = new QLabel(QStringLiteral(
            "El filtro es solo de la vista: `locate` manda la traza cruda."), this);

        hint->setWordWrap(true);
        v->addWidget(hint);
    }

    QDialogButtonBox *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
}

int BpqFiltersDialog::selected() const
{
    const int id = m_grp ? m_grp->checkedId() : -1;

    if (id < 0 || id >= m_idx.size()) return -1;
    return m_idx.at(id);
}

bool BpqFiltersDialog::phaseZero() const
{
    return m_zero && m_zero->isChecked();
}

bool BpqFiltersDialog::stalta() const
{
    return m_stalta && m_stalta->isChecked();
}
