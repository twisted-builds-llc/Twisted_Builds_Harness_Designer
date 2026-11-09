// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/UnitsCombo.h"

#include <QComboBox>
#include <QCoreApplication>

namespace tbh::app {

void fillUnitsCombo(QComboBox *combo)
{
    const auto tr = [](const char *text) {
        return QCoreApplication::translate("tbh::app::UnitsCombo", text);
    };
    combo->addItem(tr("Inches (in)"), int(Units::Inches));
    combo->addItem(tr("Millimeters (mm)"), int(Units::Millimeters));
    combo->addItem(tr("Centimeters (cm)"), int(Units::Centimeters));
}

Units comboUnits(const QComboBox *combo)
{
    return Units(combo->currentData().toInt());
}

void setComboUnits(QComboBox *combo, Units units)
{
    combo->setCurrentIndex(combo->findData(int(units)));
}

} // namespace tbh::app
