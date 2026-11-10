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
    QString partNumber;
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

struct FreeEndRef {
    QString freeEndId;
    bool operator==(const FreeEndRef &) const = default;
};

using WireEnd = std::variant<PinRef, SpliceRef, FreeEndRef>;

struct Gauge {
    double value = 0.0;
    GaugeSystem system = GaugeSystem::Awg;
    bool operator==(const Gauge &) const = default;
};

struct Wire {
    QString id;
    QString name; // circuit name / text
    WireEnd endA;
    WireEnd endB;
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

// A wire end that is not on a connector or splice: flying lead, eyelet, ring terminal or
// bare end. Placed on the schematic like a splice; one or more wires end on it.
struct FreeEnd {
    QString id;
    FreeEndKind kind = FreeEndKind::FlyingLead;
    QString label;              // empty = none
    QString terminalPartNumber; // empty = none
    QJsonObject extra;

    bool operator==(const FreeEnd &) const = default;
};

struct LayoutNode {
    QString id;
    LayoutNodeType type = LayoutNodeType::Branch;
    QString refId; // connector/splice/free end id for those node types, else empty
    QPointF position;
    QJsonObject extra;
    // Splice nodes only: further splices at the same point in the loom, side by side with
    // `refId` (splices the same distance from a connector).
    std::vector<QString> sharedSpliceIds;

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
    // Drawing only: the control point of a curved segment (quadratic Bezier) in the frame of
    // its ends, so the curve follows when they move: control = A + x * (B - A) + y * n, with
    // n = (B - A) turned 90 degrees, (-dy, dx). nullopt = straight.
    std::optional<QPointF> bend;
    QJsonObject extra;

    bool operator==(const LayoutSegment &) const = default;
};

// Which canvas a note is drawn on.
enum class NoteCanvas { Schematic, Layout };

// Free text on a canvas. Drawing only.
struct Note {
    QString id;
    NoteCanvas canvas = NoteCanvas::Layout;
    QString text;
    QPointF position; // top left of the text
    QString sheetId;  // schematic notes: the sheet it is on; empty = first sheet
    QJsonObject extra;

    bool operator==(const Note &) const = default;
};

// Where an item sits on the schematic canvas. Presentation only.
struct SchematicPlacement {
    QPointF position;       // on the home sheet `sheetId`
    bool collapsed = false;
    int maxVisibleRows = 0; // connectors: rows shown before scrolling; 0 = show all
    // Connectors, splices, free ends: their home sheet; empty = first sheet.
    // Wire sheet entries (see Schematic::wireSheetKey): the sheet the wire is drawn on.
    QString sheetId;
    // Connectors, splices, free ends shown on further sheets: sheet id -> position there.
    // The item is the same on every sheet (one connector, one splice in the layout and BOM).
    std::map<QString, QPointF> alsoOn;
    QJsonObject extra;

    // Every sheet the item is shown on with its position there, home sheet first.
    // `resolvedHome` is Schematic::resolvedSheetId(sheetId).
    std::vector<std::pair<QString, QPointF>> sheetPositions(const QString &resolvedHome) const;
    // Replaces the sheets the item is shown on; the first entry becomes the home sheet.
    // Must not be empty.
    void setSheetPositions(const std::vector<std::pair<QString, QPointF>> &positions);
    // Position on `sheet`, or nullopt when not shown there.
    std::optional<QPointF> positionOn(const QString &resolvedHome, const QString &sheet) const;
    // Sets the position on `sheet`, adding the sheet if the item is not shown there yet.
    void setPositionOn(const QString &resolvedHome, const QString &sheet, const QPointF &position);

    bool operator==(const SchematicPlacement &) const = default;
};

// One page of the schematic. Each sheet has its own canvas coordinates; a wire whose ends
// are on different sheets is drawn on each as a lead to an off-sheet tag.
struct SchematicSheet {
    QString id;
    QString name; // empty = "Sheet N" by position
    QJsonObject extra;

    bool operator==(const SchematicSheet &) const = default;
};

inline const QString kDefaultSheetId = QStringLiteral("sheet1");

struct Schematic {
    // Key: connector, splice or free end id, or a wire id
    // for a wire whose route was dragged (position.x is the x of its vertical run).
    std::map<QString, SchematicPlacement> placements;
    // Never empty. Items whose sheetId is empty or unknown are on the first sheet.
    std::vector<SchematicSheet> sheets{SchematicSheet{kDefaultSheetId, {}, {}}};
    QJsonObject extra;

    // Index of the sheet `sheetId` resolves to (0 for empty or unknown ids).
    int sheetIndex(const QString &sheetId) const;
    // Resolved sheet id of an item placement or a note's sheetId.
    QString resolvedSheetId(const QString &sheetId) const { return sheets[size_t(sheetIndex(sheetId))].id; }
    // Home sheet of the item with this placement key (first sheet without a placement).
    QString sheetOf(const QString &placementKey) const;
    // True if the item is shown on `sheetId` (an item without a placement is on the first sheet).
    bool isOnSheet(const QString &placementKey, const QString &sheetId) const;
    // Sheets the item is shown on, in sheet order.
    std::vector<QString> sheetsOf(const QString &placementKey) const;
    // Position of the item on `sheetId`; nullopt without a placement or when not shown there.
    std::optional<QPointF> positionOn(const QString &placementKey, const QString &sheetId) const;
    // Key of the placement entry holding the sheet a wire was drawn on (its sheetId only).
    static QString wireSheetKey(const QString &wireId) { return wireId + QStringLiteral("#sheet"); }

    bool operator==(const Schematic &) const = default;
};

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
    std::vector<FreeEnd> freeEnds;
    std::vector<LayoutNode> layoutNodes;
    std::vector<LayoutSegment> layoutSegments;
    std::vector<Note> notes; // both canvases; layout.json and schematic.json hold their own
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
