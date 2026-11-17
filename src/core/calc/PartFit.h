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

struct Problem;

// Problems with library parts, using the library's part data (parts it lacks are not checked):
// - a terminal whose wire size range does not take the conductor on it (several wires in one
//   terminal count by their total cross-section);
// - a terminal or seal whose insulation range does not take a wire's outer diameter (from
//   the wire's wire type; single-wire cavities only);
// - a terminal, seal or plug the connector's library housing does not list;
// - a free end terminal (ring, eyelet) that does not take its wire's size.
// Kind PartMismatch, one per finding, appended to `problems`.
void addPartProblems(const Harness &harness, const Library &library, std::vector<Problem> &problems);

} // namespace tbh::calc
