#include "crv_overlay_dialog.h"

#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QVBoxLayout>

CrvOverlayDialog::CrvOverlayDialog(CrvMap *map, QWidget *parent,
                                   std::function<void()> onChanged)
    : QDialog(parent), m_map(map), m_changed(std::move(onChanged))
{
    setWindowTitle(QStringLiteral("Overlay layers"));

    QVBoxLayout *v = new QVBoxLayout(this);
    const int n = map ? crv_map_nlayers(map) : 0;
    if (n == 0) {
        v->addWidget(new QLabel(QStringLiteral("(sin capas .lines)"), this));
    }
    for (int i = 0; i < n; i++) {
        CrvLayerInfo li;
        if (crv_map_layer_info(map, i, &li) != 0)
            continue;

        QHBoxLayout *row = new QHBoxLayout();
        QLabel *swatch = new QLabel(this);
        QPixmap pm(14, 14);
        pm.fill(QColor(int((li.rgb >> 16) & 0xff),
                       int((li.rgb >> 8) & 0xff),
                       int(li.rgb & 0xff)));
        swatch->setPixmap(pm);
        row->addWidget(swatch);

        QCheckBox *cb = new QCheckBox(QString::fromUtf8(li.name), this);
        cb->setChecked(li.visible != 0);
        cb->setToolTip(QStringLiteral("polylines=%1 vertices=%2")
                           .arg(li.polylines)
                           .arg(li.vertices));
        connect(cb, &QCheckBox::toggled, this, [this, i](bool on) {
            crv_map_set_visible(m_map, i, on ? 1 : 0);
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
