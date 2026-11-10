// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Rows for the printed build package: the cut list in the order wires are cut, and one
// pinout table per connector and splice.

#include "bom/Bom.h"

namespace tbh::bom {

// The cut list sorted for cutting, so wire from one spool is cut in one go:
//   1. gauge: AWG before mm2, thickest first (AWG low to high, mm2 high to low);
//   2. base color, then stripe (no stripe first), ignoring case;
//   3. wire name in natural order ("W2" before "W10"); unnamed wires last.
// Ties keep their original order.
std::vector<CutLine> cutListInCuttingOrder(std::vector<CutLine> lines);

// One wire end at a connector pin or splice. A pin without wires has one row with an
// empty wireId.
struct PinoutRow
{
    QString pin;     // pin label; empty for splices
    QString circuit; // pin circuit text
    bool cavityPlug = false;
    QString wireId;  // empty: nothing on this pin
    QString wireName;
    QString baseColor;
    QString stripeColor;
    Gauge gauge;
    QString otherEnd; // describeWireEnd() of the wire's other end
};

struct PinoutTable
{
    QString title;       // connector or splice label
    QString partNumber;
    QString description; // connector description, or "Splice"
    std::vector<PinoutRow> rows;
};

// Connectors (every pin, in pin order; each pin's wires in model order), then splices that
// have wires. A wire with both ends on the same item has a row for each end.
std::vector<PinoutTable> pinoutTables(const Harness &harness);

} // namespace tbh::bom
