// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include "model/Harness.h"

#include <QSet>

#include <algorithm>

namespace tbh {

// Ids of wires with at least one end on the connector.
std::vector<QString> wiresOnConnector(const Harness &harness, const QString &connectorId);
// Ids of wires with at least one end on one of the given pins of the connector.
std::vector<QString> wiresOnPins(const Harness &harness, const QString &connectorId,
                                 const QSet<QString> &pinIds);
// Ids of wires with at least one end on the splice.
std::vector<QString> wiresOnSplice(const Harness &harness, const QString &spliceId);
// Ids of wires with at least one end on the free end.
std::vector<QString> wiresOnFreeEnd(const Harness &harness, const QString &freeEndId);
// The free end with this id, or null.
const FreeEnd *findFreeEnd(const Harness &harness, const QString &id);
// "Flying lead", "Eyelet", "Ring terminal", "Bare end".
QString freeEndKindName(FreeEndKind kind);
// Kind name plus label, e.g. "Eyelet GND".
QString describeFreeEnd(const FreeEnd &end);
// Layout node type that stands for a free end of this kind (ring terminals draw as eyelets).
LayoutNodeType layoutNodeTypeFor(FreeEndKind kind);
// A point that sits inside a loom rather than ending one: a splice, or a cut (Cut free end).
// Such points can be placed along a bundle and have a stub moved into it.
bool isLoomPoint(const Harness &harness, const LayoutNode &node);
// Ids of wires with an end on any splice or free end the layout node holds (refId and
// sharedRefIds), in model order.
std::vector<QString> wiresAtLayoutNode(const Harness &harness, const LayoutNode &node);

// Circuit text copied across wires, only into empty fields. Each listed wire is taken
// with its net: every wire reachable through splices (connector pins end the net).
// The net's text is the first non-empty pin text, the listed wire's own pins first.
// - every empty pin in the net gets that text;
// - every unnamed wire in the net takes its own pin's text, else the net's text.
// Existing text is never overwritten, so the copy happens once and later edits on either
// end stay independent. Unknown wire ids are skipped.
struct CircuitTextFill
{
    QSet<QString> connectorIds; // connectors whose pins changed
    QSet<QString> wireIds;      // wires whose name changed
};
CircuitTextFill fillEmptyCircuitText(Harness &harness, const std::vector<QString> &wireIds);
// A renamed circuit text follows across the nets of the listed wires (same nets as
// fillEmptyCircuitText): every pin whose text is exactly `oldText` and every wire named
// exactly `oldText` gets `newText`. Pins and wires named something else are left alone.
// Does nothing if either text is empty or they are equal.
CircuitTextFill renameCircuitText(Harness &harness, const std::vector<QString> &wireIds,
                                  const QString &oldText, const QString &newText);

// Cables a wire is in, as a conductor or as the shield, in model order.
std::vector<const Cable *> cablesOfWire(const Harness &harness, const QString &wireId);
// Keeps cables valid after wires went away: missing conductors are dropped, a missing shield
// is cleared, and cables left with fewer than two conductors are removed.
void pruneCables(Harness &harness);

// Human-readable end description, e.g. "ECU-A : A1", "S1", "Ring terminal".
QString describeWireEnd(const Harness &harness, const WireEnd &end);
// e.g. "5V REF (ECU-A : A1 - SENSOR : +)".
QString describeWire(const Harness &harness, const Wire &wire);

// The sheet's name, or "Sheet N" (N = position, from 1) when it has none.
QString sheetDisplayName(const Schematic &schematic, int index);
// Placement key of the item a wire end is on: connector, splice or free end id.
QString wireEndItemId(const WireEnd &end);

// Where a wire appears on the schematic. A wire can be shown on several sheets (like a
// connector shared by sheets); it stays one wire. It is drawn in full (`sheets`) on each of
// its own sheets that shows both its ends; without any, on the first sheet in order showing
// both. When the ends share no sheet, each end gets a lead to an off-sheet tag on
// `tagSheetA` / `tagSheetB`: the wire's first own sheet if that end is shown there, else the
// end's home sheet.
struct WireSheets
{
    std::vector<QString> sheets; // in sheet order; empty when the ends share no sheet
    QString tagSheetA;
    QString tagSheetB;

    bool drawnOn(const QString &sheetId) const
    {
        return std::find(sheets.begin(), sheets.end(), sheetId) != sheets.end();
    }
};
WireSheets wireSheets(const Harness &harness, const Wire &wire);

// Something a layout node can stand for: a connector, a splice or a free end.
struct LayoutRef
{
    QString refId; // LayoutNode::refId value
    LayoutNodeType nodeType;
    QString label;
};

// "18 AWG", "0.5 mm²".
QString gaugeText(const Gauge &gauge);
// Reads a wire size as gaugeText() shows it: "18 AWG", "18", "0.5 mm²", "0.5 mm2", "0.5mm"
// (locale or C decimal point). No unit means AWG. Empty or unreadable text gives nullopt.
std::optional<Gauge> parseGauge(const QString &text);

// Circuit ids: "C" + a number of at least three digits. The next one is one more than the
// highest such id in use (ids typed in another form are ignored), e.g. C001 for none.
QString circuitIdForNumber(int number);
int nextCircuitNumber(const Harness &harness);
QString nextCircuitId(const Harness &harness);

// Wire ends a wire can be connected to, by their describeWireEnd() text: every connector
// pin ("X1 : 3"), splice and free end, in model order.
struct WireEndChoice
{
    QString text;
    WireEnd end;
};
std::vector<WireEndChoice> wireEndChoices(const Harness &harness);
// The choice whose text matches `text`, ignoring case and spaces ("x1:3" finds "X1 : 3").
// The first match wins when labels repeat.
std::optional<WireEnd> findWireEnd(const Harness &harness, const QString &text);

// Connecting a pin by naming the ends it is wired to: names are describeWireEnd() texts,
// comma separated (e.g. "ECU : 3, S1"; findWireEnd() matching).
// The current text for a pin: the far ends of its wires, in wire order.
QString pinConnectionsText(const Harness &harness, const PinRef &pin);
// What it takes to make `pin` wired to exactly the ends `text` names. Wires to ends no
// longer named are moved to newly named ends, pairing them in order, so they keep their
// color, gauge and name; named ends left over get new wires, wires left over are removed.
// Names that match nothing, or name the pin itself, go to `unknown` (the plan is otherwise
// empty then). Repeated names count once.
struct PinConnectionPlan
{
    struct Move
    {
        QString wireId;
        bool endB = false; // which end of the wire moves
        WireEnd end;
    };
    QStringList unknown;
    std::vector<Move> moves;
    std::vector<WireEnd> adds;
    std::vector<QString> removals;

    bool isEmpty() const { return moves.empty() && adds.empty() && removals.empty(); }
};
PinConnectionPlan planPinConnections(const Harness &harness, const PinRef &pin, const QString &text);

// All connectors, splices and free ends, in model order.
std::vector<LayoutRef> layoutRefs(const Harness &harness);
// Label for whatever a layout node references ("" for none or branch points).
QString layoutRefLabel(const Harness &harness, const QString &refId);

} // namespace tbh
