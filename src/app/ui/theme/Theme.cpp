// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace tbh::app {

namespace {

constexpr auto kSettingsKey = "ui/theme";

struct Colors
{
    QColor window, base, alternateBase, button, text, disabledText, light, midlight, mid, dark,
        accent;
};

QPalette makePalette(const Colors &c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.alternateBase);
    p.setColor(QPalette::ToolTipBase, c.base);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.disabledText);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.button);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, c.light);
    p.setColor(QPalette::Midlight, c.midlight);
    p.setColor(QPalette::Mid, c.mid);
    p.setColor(QPalette::Dark, c.dark);
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Accent, c.accent);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.disabledText);
    return p;
}

QPalette darkPalette()
{
    return makePalette({QColor(0x24, 0x26, 0x2b), QColor(0x1b, 0x1d, 0x21), QColor(0x24, 0x26, 0x2b),
                        QColor(0x2e, 0x31, 0x37), QColor(0xe6, 0xe8, 0xeb), QColor(0x7a, 0x7f, 0x87),
                        QColor(0x3a, 0x3e, 0x45), QColor(0x33, 0x36, 0x3c), QColor(0x2a, 0x2c, 0x31),
                        QColor(0x16, 0x17, 0x1a), QColor(0x3d, 0x8b, 0xfd)});
}

QPalette lightPalette()
{
    return makePalette({QColor(0xf3, 0xf4, 0xf6), Qt::white, QColor(0xf7, 0xf8, 0xfa),
                        QColor(0xe9, 0xeb, 0xee), QColor(0x1d, 0x20, 0x24), QColor(0x8a, 0x8f, 0x96),
                        Qt::white, QColor(0xf0, 0xf1, 0xf3), QColor(0xb8, 0xbc, 0xc2),
                        QColor(0x9a, 0x9e, 0xa5), QColor(0x1f, 0x6f, 0xeb)});
}

} // namespace

QPalette lightThemePalette()
{
    return lightPalette();
}

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const int stored = QSettings().value(kSettingsKey, int(Mode::System)).toInt();
    if (stored >= int(Mode::System) && stored <= int(Mode::Dark))
        m_mode = Mode(stored);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_mode == Mode::System)
            apply();
    });
    apply();
}

void Theme::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    QSettings().setValue(kSettingsKey, int(mode));
    apply();
}

void Theme::apply()
{
    bool dark = m_mode == Mode::Dark;
    if (m_mode == Mode::System)
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QApplication::setPalette(dark ? darkPalette() : lightPalette());
}

} // namespace tbh::app
