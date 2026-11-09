// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

class QComboBox;

namespace tbh::app {

// Shared helpers so every units picker lists the same choices in the same order.
void fillUnitsCombo(QComboBox *combo);
Units comboUnits(const QComboBox *combo);
void setComboUnits(QComboBox *combo, Units units);

} // namespace tbh::app
