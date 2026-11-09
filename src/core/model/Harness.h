// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Value types for the harness data model (PROJECT_PLAN.md section 5).
// Entities reference each other by stable string ids, never by pointer or index.
// All lengths are stored in millimeters regardless of the display unit.
// Each entity keeps `extra`: JSON keys this version does not understand, preserved on save.

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QPointF>
#include <QString>

#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace tbh {

inline constexpr int kMinPins = 1;
inline constexpr int kMaxPins = 999;

// New random UUID without braces.
QString newId();

// Display unit for lengths. Lengths are always stored in millimeters.
enum class Units { Inches, Millimeters, Centimeters };
// "in" | "mm" | "cm"; used in files and settings, and as the display symbol.
QString unitsSymbol(Units units);
std::optional<Units> unitsFromSymbol(const QString &symbol);
// Length conversion between millimeters (storage) and a display unit.
double mmToUnits(double mm, Units units);
double unitsToMm(double value, Units units);
enum class Gender { None, Male, Female };
enum class GaugeSystem { Awg, Mm2 };
enum class FreeEndKind { FlyingLead, Eyelet, RingTerminal, Bare };
// End types used to look up strip/terminal allowances.
enum class EndType { Pin, Splice, FlyingLead, Eyelet, RingTerminal, Bare };
// Only Single in v1; twisted pair / shielded / multi-conductor come later (section 5.6).
enum class ConductorKind { Single };
enum class LayoutNodeType { Connector, Branch, Splice, FlyingLead, Eyelet };
// Only Full in v1 (sleeving covers the whole segment).
enum class SleevingCoverage { Full };

struct Settings {
    double packingFactor = 0.70;
    double serviceLoopPercent = 0.0;
    // Missing entries mean 0 mm.
    std::map<EndType, double> stripAllowanceMm;
    double wasteFactorWirePercent = 5.0;
    double wasteFactorSleevingPercent = 5.0;
    double lengthRoundingIncrementMm = 1.0;
    double sleevingDiameterMarginPercent = 10.0;
    QJsonObject extra;

    bool operator==(const Settings &) const = default;
};

struct HarnessInfo {
    QString name;
    QString description;
    QString revision;
    QString author;
    Units units = Units::Inches;

    bool operator==(const HarnessInfo &) const = default;
};

struct Pin {
    QString id;
    QString label; // free-form: "1", "A1", "+", "GND", ...
    QString circuitText;
    QString terminalPartNumber;
    QString sealPartNumber;
    bool isCavityPlug = false;
    QString notes;
    QJsonObject extra;

    bool operator==(const Pin &) const = default;
};

struct Connector {
    QString id;
    QString label;
    QString partNumber;
    QString manufacturer;
    QString description;
    Gender gender = Gender::None;
    QString notes;
    std::vector<Pin> pins; // ordered, kMinPins..kMaxPins
    QJsonObject extra;

    bool operator==(const Connector &) const = default;
};

struct PinRef {
    QString connectorId;
    QString pinId;
    bool operator==(const PinRef &) const = default;
};

struct SpliceRef {
    QString spliceId;
    bool operator==(const SpliceRef &) const = default;
};

struct FreeEnd {
    FreeEndKind kind = FreeEndKind::FlyingLead;
    QString terminalPartNumber; // empty = none
    QString label;              // empty = none
    bool operator==(const FreeEnd &) const = default;
};

using WireEnd = std::variant<PinRef, SpliceRef, FreeEnd>;

struct Gauge {
    double value = 0.0;
    GaugeSystem system = GaugeSystem::Awg;
    bool operator==(const Gauge &) const = default;
};

struct Wire {
    QString id;
    QString name; // circuit name / text
    WireEnd endA = FreeEnd{};
    WireEnd endB = FreeEnd{};
    QString baseColor;   // standard color name or #rrggbb
    QString stripeColor; // empty = no stripe
    Gauge gauge;
    QString wireTypeId; // library reference, empty = unassigned
    ConductorKind conductorKind = ConductorKind::Single;
    std::optional<double> lengthOverrideMm;
    std::vector<QString> routeSegmentIds; // manual route override, empty = automatic
    QJsonObject extra;

    bool operator==(const Wire &) const = default;
};

struct Splice {
    QString id;
    QString label;
    QString partNumber;
    QJsonObject extra;

    bool operator==(const Splice &) const = default;
};

struct LayoutNode {
    QString id;
    LayoutNodeType type = LayoutNodeType::Branch;
    QString refId; // connector/splice id for those node types, else empty
    QPointF position;
    QJsonObject extra;

    bool operator==(const LayoutNode &) const = default;
};

struct LayoutSegment {
    QString id;
    QString nodeA;
    QString nodeB;
    double lengthMm = 0.0; // entered value, not derived from drawing
    QString label;
    QString sleevingId; // library reference, empty = none
    SleevingCoverage sleevingCoverage = SleevingCoverage::Full;
    QJsonObject extra;

    bool operator==(const LayoutSegment &) const = default;
};

// Where an item sits on the schematic canvas. Presentation only.
struct SchematicPlacement {
    QPointF position;
    bool collapsed = false;
    int maxVisibleRows = 0; // connectors: rows shown before scrolling; 0 = show all
    QJsonObject extra;

    bool operator==(const SchematicPlacement &) const = default;
};

struct Schematic {
    // Key: connector or splice id, or freeEndPlacementKey() for a wire's free end.
    std::map<QString, SchematicPlacement> placements;
    QJsonObject extra;

    bool operator==(const Schematic &) const = default;
};

// Placement key of a wire's free end: "<wireId>/a" or "<wireId>/b".
QString freeEndPlacementKey(const QString &wireId, bool endB);

// A BOM line the user adds by hand (tape, zip ties...), saved in bom.json.
struct BomExtra {
    QString id;
    QString partNumber;
    QString description;
    double quantity = 1.0;
    QString unit; // free text, e.g. "ea", "roll", "ft"
    QString notes;
    QJsonObject extra;

    bool operator==(const BomExtra &) const = default;
};

// ---- Parts libraries (5.7) ----------------------------------------------------------
// Wire types with outer diameters, and sleeving. Each user keeps a library; a harness file
// embeds the entries it uses so it opens the same way on another machine. Shipped seed
// values are approximate.

struct WireOuterDiameter {
    Gauge gauge;
    double odMm = 0.0; // insulated outer diameter
    bool operator==(const WireOuterDiameter &) const = default;
};

struct WireType {
    QString id;
    QString name; // e.g. "GXL"
    std::vector<WireOuterDiameter> outerDiameters;
    // Shipped seed data not verified by the user; the UI must label it approximate.
    bool approximate = false;
    QJsonObject extra;

    // OD for an exact gauge match, or nullopt.
    std::optional<double> outerDiameterMm(const Gauge &gauge) const;
    bool operator==(const WireType &) const = default;
};

enum class SleevingKind { Braided, SplitLoom, HeatShrink, Other };

struct Sleeving {
    QString id;
    QString family;    // the "type" the user picks, e.g. "PET braided"
    SleevingKind kind = SleevingKind::Other;
    QString sizeLabel; // e.g. "1/4 in"
    double innerDiameterMm = 0.0; // nominal / as-supplied inner diameter
    std::optional<double> recoveredDiameterMm; // heat shrink: fully recovered diameter
    bool approximate = false; // see WireType::approximate
    QJsonObject extra;

    bool operator==(const Sleeving &) const = default;
};

struct Library {
    std::vector<WireType> wireTypes;
    std::vector<Sleeving> sleevings;

    const WireType *wireType(const QString &id) const;
    const Sleeving *sleeving(const QString &id) const;
    // Distinct sleeving families in catalog order.
    QStringList sleevingFamilies() const;
    bool isEmpty() const { return wireTypes.empty() && sleevings.empty(); }
    bool operator==(const Library &) const = default;
};

struct Harness {
    HarnessInfo info;
    Settings settings;
    QDateTime createdAt; // UTC; invalid = set on first save
    std::vector<Connector> connectors;
    std::vector<Wire> wires;
    std::vector<Splice> splices;
    std::vector<LayoutNode> layoutNodes;
    std::vector<LayoutSegment> layoutSegments;
    Schematic schematic;
    Library library; // embedded library entries (library.json)
    std::vector<BomExtra> bomExtras; // bom.json
    QJsonObject extra; // unknown keys of harness.json
    // Container entries this version does not parse (from newer versions), kept
    // byte-for-byte.
    std::map<QString, QByteArray> otherEntries;

    bool operator==(const Harness &) const = default;
};

} // namespace tbh
