// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"

#include <QSettings>

namespace tbh::app::preferences {

namespace {
constexpr auto kDefaultUnitsKey = "preferences/defaultUnits";
}

Units defaultUnits()
{
    const QString symbol = QSettings().value(kDefaultUnitsKey).toString();
    return unitsFromSymbol(symbol).value_or(Units::Inches);
}

void setDefaultUnits(Units units)
{
    QSettings().setValue(kDefaultUnitsKey, unitsSymbol(units));
}

} // namespace tbh::app::preferences
