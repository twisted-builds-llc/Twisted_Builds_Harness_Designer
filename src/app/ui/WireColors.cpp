// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/WireColors.h"

#include <QCoreApplication>

namespace tbh::app {

namespace {

struct StandardColor
{
    const char *code;
    const char *name;
    QRgb rgb;
};

constexpr StandardColor kColors[] = {
    {"BLK", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Black"), 0x1a1a1a},
    {"BRN", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Brown"), 0x7b4a24},
    {"RED", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Red"), 0xd62728},
    {"ORN", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Orange"), 0xff8c1a},
    {"YEL", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Yellow"), 0xf2d21b},
    {"GRN", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Green"), 0x2ca02c},
    {"BLU", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Blue"), 0x1f5fd6},
    {"VIO", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Violet"), 0x8e44ad},
    {"GRY", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Gray"), 0x8c8c8c},
    {"WHT", QT_TRANSLATE_NOOP("tbh::app::WireColors", "White"), 0xf5f5f5},
    {"PNK", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Pink"), 0xf48fb1},
    {"TAN", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Tan"), 0xd2b48c},
    {"LT GRN", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Light green"), 0x90ee90},
    {"DK GRN", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Dark green"), 0x1b5e20},
    {"LT BLU", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Light blue"), 0x81c3f5},
    {"DK BLU", QT_TRANSLATE_NOOP("tbh::app::WireColors", "Dark blue"), 0x0d2b6b},
};

} // namespace

QStringList standardWireColorCodes()
{
    QStringList codes;
    for (const auto &c : kColors)
        codes.append(QString::fromLatin1(c.code));
    return codes;
}

QString wireColorName(const QString &code)
{
    for (const auto &c : kColors) {
        if (code.compare(QLatin1StringView(c.code), Qt::CaseInsensitive) == 0)
            return QCoreApplication::translate("tbh::app::WireColors", c.name);
    }
    return code;
}

QColor wireColor(const QString &value)
{
    for (const auto &c : kColors) {
        if (value.compare(QLatin1StringView(c.code), Qt::CaseInsensitive) == 0)
            return QColor::fromRgb(c.rgb);
    }
    if (value.startsWith(QLatin1Char('#'))) {
        const QColor color = QColor::fromString(value);
        if (color.isValid())
            return color;
    }
    return {};
}

} // namespace tbh::app
