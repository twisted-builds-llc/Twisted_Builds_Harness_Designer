// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bill of materials generated from the model (PROJECT_PLAN.md 7.3). Never stored: only
// the manual extra rows (Harness::bomExtras) are saved.

#include "model/Harness.h"

namespace tbh::bom {

// Wire grouped by base color + stripe + gauge + wire type.
struct WireLine
{
    QString baseColor;
    QString stripeColor;
    Gauge gauge;
    QString wireTypeId;
    QString wireTypeName;      // from the library; empty if none/unknown
    int pieces = 0;            // cut pieces in the group
    int piecesWithoutLength = 0; // unrouted and not overridden: not in lengthMm
    double lengthMm = 0;       // sum of cut lengths
    double lengthWithWasteMm = 0; // lengthMm * (1 + wasteFactorWirePercent / 100)
};

// One row per wire.
struct CutLine
{
    QString wireId;
    QString name;
    QString endA; // describeWireEnd()
    QString endB;
    QString baseColor;
    QString stripeColor;
    Gauge gauge;
    QString wireTypeName;
    std::optional<double> cutMm; // nullopt: not routed and no override
    bool overridden = false;
};

struct SleevingLine
{
    QString sleevingId;
    QString family;
    QString sizeLabel;
    bool known = true;   // false: id not in the library
    int segments = 0;
    double lengthMm = 0;
    double lengthWithWasteMm = 0; // lengthMm * (1 + wasteFactorSleevingPercent / 100)
};

enum class PartCategory { Connector, Terminal, Seal, CavityPlug, Splice };

// Parts counted from connectors, pins and wire ends. An empty part number groups the
// items that lack one; those lines are flagged in the UI.
struct PartLine
{
    PartCategory category;
    QString partNumber; // empty = "(no part number)"
    QString description;
    int quantity = 0;
    QStringList references; // e.g. "ECU-A", "ECU-A : A1", "S1", "Ring terminal GND"
};

struct Bom
{
    std::vector<WireLine> wires;
    std::vector<CutLine> cutList;
    std::vector<SleevingLine> sleeving;
    std::vector<PartLine> parts;
    std::vector<BomExtra> extras;

    // Lines whose part number is missing (parts) or whose length is unknown (wires).
    int flaggedCount() const;
};

// Counting rules:
// - Wire: every wire is one cut piece; its cut length is calc::wireLength() (override wins).
// - Sleeving: each segment with sleeving adds its full length (v1 coverage is full).
// - Connectors: one per connector, grouped by part number.
// - Terminals: one per pin that has at least one wire (pin terminal P/N), plus one per
//   eyelet or ring-terminal free end, however many wires it takes (its terminal P/N; flying
//   leads and bare ends with a terminal P/N count too).
// - Seals: one per wired pin with a seal P/N.
// - Cavity plugs: one per pin marked as a cavity plug; its part number is taken from the
//   pin's seal P/N field.
// - Splices: one per splice, grouped by part number.
Bom buildBom(const Harness &harness, const Library &library);

} // namespace tbh::bom
