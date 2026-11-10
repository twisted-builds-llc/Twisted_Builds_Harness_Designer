// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "calc/WireLength.h"
#include "model/Harness.h"

#include <optional>

namespace tbh::app {

// "810 mm", "31.89 in", "81.0 cm" (locale-aware).
QString formatLength(double mm, Units units, bool withSymbol = true);
// Like formatLength() with the unit spelled out: "10 inches", "1 inch", "254 millimeters".
QString formatLengthWords(double mm, Units units);
// Parses a number in `units` (locale or C decimal point). Returns millimeters.
std::optional<double> parseLength(const QString &text, Units units);

struct WireLengthText
{
    QString headline; // "810 mm" or "Not routed"
    QString detail;   // how the number was reached, or why there is none
};

// Explains a calculated length in the harness units, step by step (plan 8.1).
WireLengthText describeWireLength(const Harness &harness, const Wire &wire, const calc::WireLength &length);

} // namespace tbh::app
