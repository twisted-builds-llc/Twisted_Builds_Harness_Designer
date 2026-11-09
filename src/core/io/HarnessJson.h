// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "io/ZipArchive.h"
#include "model/Harness.h"

#include <optional>

namespace tbh::io {

// Harness <-> container JSON entries (everything except manifest.json).
// Output is indented JSON with sorted keys, so it diffs cleanly.
ZipEntries serializeHarness(const Harness &harness);

// Parses entries at the current schema version and checks referential integrity
// (unique ids, references resolve, pin counts in range). Unknown entries go to
// Harness::otherEntries; unknown keys go to each entity's `extra`.
std::optional<Harness> deserializeHarness(const ZipEntries &entries, QString *error);

} // namespace tbh::io
