// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QSet>

namespace tbh {

// Copy/paste support. A fragment is a Harness holding only the given connectors and
// splices, their schematic placements, and every wire whose non-free ends all lie on
// those items (plus the free-end placements of those wires). Layout data is not copied.
Harness extractFragment(const Harness &harness, const QSet<QString> &connectorIds,
                        const QSet<QString> &spliceIds);

// Gives every connector, pin, splice and wire a fresh id and rewrites all references
// and placement keys, so the fragment can be inserted next to its original.
void assignNewIds(Harness &fragment);

} // namespace tbh
