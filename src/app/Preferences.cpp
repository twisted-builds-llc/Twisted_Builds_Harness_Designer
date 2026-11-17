// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

namespace tbh::app::preferences {

namespace {
constexpr auto kDefaultUnitsKey = "preferences/defaultUnits";
constexpr auto kAutosaveKey = "preferences/autosaveMinutes";
constexpr auto kPanInputKey = "preferences/panInput";
constexpr auto kWheelPansKey = "preferences/wheelPans";
constexpr auto kCircuitSuggestionsKey = "preferences/circuitSuggestions";
constexpr auto kCircuitSuggestionDefaultsKey = "preferences/circuitSuggestionDefaults";

class Tr
{
    Q_DECLARE_TR_FUNCTIONS(Preferences)
};
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

bool circuitSuggestions()
{
    return QSettings().value(kCircuitSuggestionsKey, false).toBool();
}

void setCircuitSuggestions(bool on)
{
    QSettings().setValue(kCircuitSuggestionsKey, on);
}

QStringList circuitSuggestionDefaults()
{
    const QSettings settings;
    if (settings.contains(kCircuitSuggestionDefaultsKey))
        return settings.value(kCircuitSuggestionDefaultsKey).toStringList();
    return {QStringLiteral("12V+"), QStringLiteral("5V+"), QStringLiteral("GND"), Tr::tr("Sensor GND"),
            Tr::tr("Unused")};
}

void setCircuitSuggestionDefaults(const QStringList &texts)
{
    QSettings().setValue(kCircuitSuggestionDefaultsKey, texts);
}

QStringList circuitSuggestionsFor(const Harness &harness)
{
    if (!circuitSuggestions())
        return {};
    QStringList used;
    for (const Connector &c : harness.connectors) {
        for (const Pin &p : c.pins)
            used << p.circuitText;
    }
    for (const Wire &w : harness.wires)
        used << w.name;
    used.sort(Qt::CaseInsensitive);
    QStringList result;
    QSet<QString> seen;
    for (const QStringList &list : {circuitSuggestionDefaults(), used}) {
        for (const QString &raw : list) {
            const QString text = raw.trimmed();
            if (text.isEmpty() || seen.contains(text.toCaseFolded()))
                continue;
            seen.insert(text.toCaseFolded());
            result << text;
        }
    }
    return result;
}

} // namespace tbh::app::preferences
