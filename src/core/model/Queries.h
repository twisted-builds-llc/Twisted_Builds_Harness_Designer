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

// Human-readable end description, e.g. "ECU-A : A1", "S1", "Ring terminal".
QString describeWireEnd(const Harness &harness, const WireEnd &end);
// e.g. "5V REF (ECU-A : A1 - SENSOR : +)".
QString describeWire(const Harness &harness, const Wire &wire);

} // namespace tbh
