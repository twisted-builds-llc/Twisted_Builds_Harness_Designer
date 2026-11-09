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

std::optional<Units> unitsFromSymbol(const QString &symbol)
{
    for (Units units : {Units::Inches, Units::Millimeters, Units::Centimeters}) {
        if (symbol == unitsSymbol(units))
            return units;
    }
    return std::nullopt;
}

} // namespace tbh
