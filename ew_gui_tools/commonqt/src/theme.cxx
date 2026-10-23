/* Ver include/ewguiqt/theme.h. */
#include "ewguiqt/theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyle>

namespace ewguiqt {

namespace {
const char *kDarkProperty = "ewguiqt_dark";
}

bool dark_mode(const QApplication &app)
{
    return app.property(kDarkProperty).toBool();
}

void set_dark_mode(QApplication &app, bool on)
{
    if (dark_mode(app) == on)
        return;

    if (!on) {
        app.setPalette(app.style()->standardPalette());
        app.setProperty(kDarkProperty, false);
        return;
    }

    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
    p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::Button, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::ButtonText, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0x64, 0xb5, 0xf6));
    p.setColor(QPalette::Highlight, QColor(0x2d, 0x6c, 0xdf));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9a, 0x9a));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x8a, 0x8a, 0x8a));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x8a, 0x8a, 0x8a));
    app.setPalette(p);
    app.setProperty(kDarkProperty, true);
}

} /* namespace ewguiqt */
