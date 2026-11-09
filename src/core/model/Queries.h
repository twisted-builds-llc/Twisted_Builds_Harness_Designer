// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QSet>

namespace tbh {

// Ids of wires with at least one end on the connector.
std::vector<QString> wiresOnConnector(const Harness &harness, const QString &connectorId);
// Ids of wires with at least one end on one of the given pins of the connector.
std::vector<QString> wiresOnPins(const Harness &harness, const QString &connectorId,
                                 const QSet<QString> &pinIds);
// Ids of wires with at least one end on the splice.
std::vector<QString> wiresOnSplice(const Harness &harness, const QString &spliceId);

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

// Something a layout node can stand for: a connector, a splice or a wire's free end.
struct LayoutRef
{
    QString refId; // LayoutNode::refId value
    LayoutNodeType nodeType;
    QString label;
};

// All connectors, splices and wire free ends, in model order.
std::vector<LayoutRef> layoutRefs(const Harness &harness);
// Label for whatever a layout node references ("" for none or branch points).
QString layoutRefLabel(const Harness &harness, const QString &refId);

} // namespace tbh
