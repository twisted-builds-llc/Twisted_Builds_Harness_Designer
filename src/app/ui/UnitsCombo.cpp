// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/UnitsCombo.h"

#include <QComboBox>
#include <QCoreApplication>

namespace tbh::app {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::app::UnitsCombo)
};
} // namespace

void fillUnitsCombo(QComboBox *combo)
{
    combo->addItem(Tr::tr("Inches (in)"), int(Units::Inches));
    combo->addItem(Tr::tr("Millimeters (mm)"), int(Units::Millimeters));
    combo->addItem(Tr::tr("Centimeters (cm)"), int(Units::Centimeters));
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
