#include "sev_overlay_dialog.h"

#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QVBoxLayout>

SevOverlayDialog::SevOverlayDialog(SevMap *map, QWidget *parent,
                                   std::function<void()> onChanged)
    : QDialog(parent), m_map(map), m_changed(std::move(onChanged))
{
    setWindowTitle(QStringLiteral("Overlay layers"));

    QVBoxLayout *v = new QVBoxLayout(this);
    const int n = map ? sev_map_nlayers(map) : 0;
    if (n == 0) {
        v->addWidget(new QLabel(QStringLiteral("(sin capas .lines)"), this));
    }
    for (int i = 0; i < n; i++) {
        SevLayerInfo li;
        if (sev_map_layer_info(map, i, &li) != 0)
            continue;

        QHBoxLayout *row = new QHBoxLayout();
        QLabel *swatch = new QLabel(this);
        QPixmap pm(14, 14);
        pm.fill(QColor::fromRgbF(qBound(0.0, li.rgb[0], 1.0),
                                 qBound(0.0, li.rgb[1], 1.0),
                                 qBound(0.0, li.rgb[2], 1.0)));
        swatch->setPixmap(pm);
        row->addWidget(swatch);

        QCheckBox *cb = new QCheckBox(QString::fromUtf8(li.name), this);
        cb->setChecked(li.visible != 0);
        cb->setToolTip(QStringLiteral("polylines=%1 vertices=%2")
                           .arg(li.polylines)
                           .arg(li.vertices));
        connect(cb, &QCheckBox::toggled, this, [this, i](bool on) {
            sev_map_set_visible(m_map, i, on ? 1 : 0);
            if (m_changed)
                m_changed();
        });
        row->addWidget(cb, 1);
        v->addLayout(row);
    }

    QDialogButtonBox *bb =
        new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    v->addWidget(bb);

    resize(320, 240);
}
