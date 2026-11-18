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
    QString circuitId;
    QString name;
    QString endA; // describeWireEnd()
    QString endB;
    QString baseColor;
    QString stripeColor;
    Gauge gauge;
    QString wireTypeName;
    std::optional<double> cutMm; // nullopt: not routed and no override
    bool overridden = false;
    QStringList cables; // labels of the cables it is in
    bool shield = false; // it is a cable's shield
};

// A bought cable, or the braid / foil of a built shielded cable, by part number. Each cable
// is one piece as long as the run its conductors share (calc::cableRunLengthMm), rounded up
// like wires.
struct CableLine
{
    enum class Kind { Cable, ShieldBraid };
    Kind kind = Kind::Cable;
    QString partNumber; // empty = "(no part number)"
    QStringList references; // cable labels
    int pieces = 0;
    int piecesWithoutLength = 0; // a conductor is not routed: not in lengthMm
    double lengthMm = 0;
    double lengthWithWasteMm = 0; // lengthMm * (1 + wasteFactorWirePercent / 100)
};

struct SleevingLine
{
    QString sleevingId;
    QString family;
    QString sizeLabel;
    bool known = true;   // false: id not in the library
    int pieces = 0; // segments covered, plus label covers
    double lengthMm = 0;
    double lengthWithWasteMm = 0; // lengthMm * (1 + wasteFactorSleevingPercent / 100)
};

enum class PartCategory { Connector, Backshell, Terminal, Seal, CavityPlug, Splice, Label, Tie };

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
    std::vector<CableLine> cables;
    std::vector<SleevingLine> sleeving;
    std::vector<PartLine> parts;
    std::vector<BomExtra> extras;

    // Lines whose part number is missing (parts, cables) or whose length is unknown (wires,
    // cables).
    int flaggedCount() const;
};

// Counting rules:
// - Wire: every wire is one cut piece; its cut length is calc::wireLength() (override wins).
// - Cables: the conductors and shield of a bought cable stay in the cut list (each breakout
//   is cut to length) but not in the wire lines; the cable is one CableLine piece. A built
//   cable's conductors are wires as usual (twisted ones are longer by the twist take-up);
//   its shield wire is in the cut list but counted as a ShieldBraid piece, not as wire.
//   Cables whose conductors are all factory wires are not counted.
// - Sleeving: each segment with sleeving adds its full length (v1 coverage is full).
// - Connectors: one per connector, grouped by part number.
// - Terminals: one per pin that has at least one (non-factory) wire (pin terminal P/N), plus one per
//   eyelet or ring-terminal free end, however many wires it takes (its terminal P/N; flying
//   leads and bare ends with a terminal P/N count too).
// - Seals: one per wired pin with a seal P/N.
// - Cavity plugs: one per pin marked as a cavity plug; its part number is taken from the
//   pin's seal P/N field.
// - Splices: one per splice, grouped by part number.
// - Factory items (Wire::factory, Connector::factory) already exist and are not counted:
//   factory wires are left out of the wire lines and cut list; a factory connector counts
//   only the terminals and seals of pins that have a non-factory wire. Splices and free
//   ends whose wires are all factory wires are not counted either; cut ends never are.
Bom buildBom(const Harness &harness, const Library &library);

} // namespace tbh::bom
