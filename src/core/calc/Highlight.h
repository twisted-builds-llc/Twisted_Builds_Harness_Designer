// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Highlight set (PROJECT_PLAN.md 8.4): everything related to a selection, in both views.

#include "calc/WireLength.h"

#include <QSet>

namespace tbh::calc {

// What the user selected. Pins are "connectorId/pinId"; when a connector has selected
// pins, only those pins seed the set (not the whole connector).
struct HighlightSeed
{
    QSet<QString> connectors;
    QSet<QString> pins;
    QSet<QString> splices;
    QSet<QString> wires;
    QSet<QString> layoutNodes;
    QSet<QString> layoutSegments;
};

struct HighlightSet
{
    QSet<QString> connectors;
    QSet<QString> pins; // "connectorId/pinId" of related wire ends
    QSet<QString> splices;
    QSet<QString> wires;
    QSet<QString> freeEnds; // freeEndPlacementKey()
    QSet<QString> layoutNodes;
    QSet<QString> layoutSegments;
};

QString pinKey(const QString &connectorId, const QString &pinId);

// Related wires: those on seeded connectors/pins/splices, seeded wires, wires routed
// through seeded segments, and wires on what seeded layout nodes stand for (for a branch
// point: wires through its segments). Then everything those wires touch: their end items
// and pins, and the layout segments and nodes of their routes. Seeds are always included.
HighlightSet relatedItems(const Harness &harness, const LayoutGraph &graph, const HighlightSeed &seed);

} // namespace tbh::calc
