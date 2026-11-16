// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"

#include <QSettings>

namespace tbh::app::preferences {

namespace {
constexpr auto kDefaultUnitsKey = "preferences/defaultUnits";
constexpr auto kAutosaveKey = "preferences/autosaveMinutes";
constexpr auto kPanInputKey = "preferences/panInput";
constexpr auto kWheelPansKey = "preferences/wheelPans";
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

PanInput panInput()
{
    const QString value = QSettings().value(kPanInputKey).toString();
    if (value == QLatin1String("right"))
        return PanInput::RightDrag;
    if (value == QLatin1String("space"))
        return PanInput::SpaceDrag;
    return PanInput::MiddleDrag;
}

void setPanInput(PanInput input)
{
    const char *value = input == PanInput::RightDrag ? "right" : input == PanInput::SpaceDrag ? "space" : "middle";
    QSettings().setValue(kPanInputKey, QString::fromLatin1(value));
}

bool wheelPans()
{
    return QSettings().value(kWheelPansKey, false).toBool();
}

void setWheelPans(bool pans)
{
    QSettings().setValue(kWheelPansKey, pans);
}

} // namespace tbh::app::preferences
