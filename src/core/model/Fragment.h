// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QSet>

namespace tbh {

// Copy/paste support. A fragment is a Harness holding only the given connectors, splices
// and free ends (plus free ends wired to those connectors and splices), their schematic
// placements, and every wire whose ends both lie on those items (with its route
// placement), and the cables of those wires (a cable keeps the conductors and shield that
// came along; one left with fewer than two conductors is not copied). Layout data is not
// copied.
Harness extractFragment(const Harness &harness, const QSet<QString> &connectorIds,
                        const QSet<QString> &spliceIds, const QSet<QString> &freeEndIds = {});

// Gives every connector, pin, splice, free end, wire and cable a fresh id and rewrites all references
// and placement keys, so the fragment can be inserted next to its original.
void assignNewIds(Harness &fragment);

} // namespace tbh
