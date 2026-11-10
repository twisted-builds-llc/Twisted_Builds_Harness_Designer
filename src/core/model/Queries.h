// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

// All connectors, splices and free ends, in model order.
std::vector<LayoutRef> layoutRefs(const Harness &harness);
// Label for whatever a layout node references ("" for none or branch points).
QString layoutRefLabel(const Harness &harness, const QString &refId);

} // namespace tbh
