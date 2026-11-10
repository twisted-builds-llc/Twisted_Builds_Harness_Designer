// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Harness.h"

#include <QUuid>

namespace tbh {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString unitsSymbol(Units units)
{
    switch (units) {
    case Units::Inches:
        return QStringLiteral("in");
    case Units::Millimeters:
        return QStringLiteral("mm");
    case Units::Centimeters:
        return QStringLiteral("cm");
    }
    Q_UNREACHABLE_RETURN(QString());
}

double mmToUnits(double mm, Units units)
{
    switch (units) {
    case Units::Inches:
        return mm / 25.4;
    case Units::Millimeters:
        return mm;
    case Units::Centimeters:
        return mm / 10.0;
    }
    Q_UNREACHABLE_RETURN(mm);
}

double unitsToMm(double value, Units units)
{
    switch (units) {
    case Units::Inches:
        return value * 25.4;
    case Units::Millimeters:
        return value;
    case Units::Centimeters:
        return value * 10.0;
    }
    Q_UNREACHABLE_RETURN(value);
}

std::optional<Units> unitsFromSymbol(const QString &symbol)
{
    for (Units units : {Units::Inches, Units::Millimeters, Units::Centimeters}) {
        if (symbol == unitsSymbol(units))
            return units;
    }
    return std::nullopt;
}

std::vector<std::pair<QString, QPointF>> SchematicPlacement::sheetPositions(const QString &resolvedHome) const
{
    std::vector<std::pair<QString, QPointF>> result{{resolvedHome, position}};
    for (const auto &[sheet, pos] : alsoOn) {
        if (sheet != resolvedHome)
            result.emplace_back(sheet, pos);
    }
    return result;
}

void SchematicPlacement::setSheetPositions(const std::vector<std::pair<QString, QPointF>> &positions)
{
    Q_ASSERT(!positions.empty());
    if (positions.empty())
        return;
    sheetId = positions.front().first;
    position = positions.front().second;
    alsoOn.clear();
    for (size_t i = 1; i < positions.size(); ++i) {
        if (positions[i].first != sheetId)
            alsoOn.emplace(positions[i].first, positions[i].second);
    }
}

std::optional<QPointF> SchematicPlacement::positionOn(const QString &resolvedHome, const QString &sheet) const
{
    if (sheet == resolvedHome)
        return position;
    const auto it = alsoOn.find(sheet);
    return it == alsoOn.end() ? std::nullopt : std::optional(it->second);
}

void SchematicPlacement::setPositionOn(const QString &resolvedHome, const QString &sheet, const QPointF &pos)
{
    if (sheet == resolvedHome)
        position = pos;
    else
        alsoOn[sheet] = pos;
}

int Schematic::sheetIndex(const QString &sheetId) const
{
    for (size_t i = 0; i < sheets.size(); ++i) {
        if (sheets[i].id == sheetId)
            return int(i);
    }
    return 0;
}

QString Schematic::sheetOf(const QString &placementKey) const
{
    const auto it = placements.find(placementKey);
    return resolvedSheetId(it == placements.end() ? QString() : it->second.sheetId);
}

bool Schematic::isOnSheet(const QString &placementKey, const QString &sheetId) const
{
    const auto it = placements.find(placementKey);
    if (it == placements.end())
        return sheetId == sheets.front().id;
    return it->second.positionOn(resolvedSheetId(it->second.sheetId), sheetId).has_value();
}

std::vector<QString> Schematic::sheetsOf(const QString &placementKey) const
{
    std::vector<QString> result;
    for (const SchematicSheet &sheet : sheets) {
        if (isOnSheet(placementKey, sheet.id))
            result.push_back(sheet.id);
    }
    return result;
}

std::optional<QPointF> Schematic::positionOn(const QString &placementKey, const QString &sheetId) const
{
    const auto it = placements.find(placementKey);
    if (it == placements.end())
        return std::nullopt;
    return it->second.positionOn(resolvedSheetId(it->second.sheetId), sheetId);
}

} // namespace tbh
