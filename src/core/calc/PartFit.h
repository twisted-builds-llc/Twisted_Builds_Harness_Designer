// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Whether a wire suits a terminal or seal from the connector parts library.

#include "model/Harness.h"

namespace tbh::calc {

// Conductor cross-section in mm2. AWG n: diameter 0.127 mm * 92^((36 - n) / 39), area
// pi * d^2 / 4. mm2 is the value itself.
double gaugeAreaMm2(const Gauge &gauge);

// Unknown: the part gives no range. TooSmall / TooLarge: the wire is below / above it.
// Bounds allow 2 % for rounding in datasheets (e.g. 0.5 mm2 against 20 AWG = 0.518 mm2).
enum class Fit { Unknown, Fits, TooSmall, TooLarge };

// Conductor size against the part's wireMin / wireMax, compared by cross-section. A missing
// bound does not limit that side.
Fit wireSizeFit(const PinPart &part, const Gauge &gauge);
// Insulation outer diameter against insulationMinMm / insulationMaxMm.
Fit insulationFit(const PinPart &part, double outerDiameterMm);

} // namespace tbh::calc
