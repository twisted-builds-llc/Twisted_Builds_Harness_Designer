// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

// Per-user application preferences, persisted in QSettings.
namespace tbh::app::preferences {

// Units given to newly created harnesses. Default: inches.
Units defaultUnits();
void setDefaultUnits(Units units);

} // namespace tbh::app::preferences
