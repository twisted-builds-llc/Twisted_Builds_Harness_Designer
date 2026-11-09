// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"

#include <QSettings>

namespace tbh::app::preferences {

namespace {
constexpr auto kDefaultUnitsKey = "preferences/defaultUnits";
constexpr auto kAutosaveKey = "preferences/autosaveMinutes";
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

int autosaveMinutes()
{
    bool ok = false;
    const int minutes = QSettings().value(kAutosaveKey, 2).toInt(&ok);
    return ok && minutes >= 0 && minutes <= 60 ? minutes : 2;
}

void setAutosaveMinutes(int minutes)
{
    QSettings().setValue(kAutosaveKey, minutes);
}

} // namespace tbh::app::preferences
