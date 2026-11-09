// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "io/ZipArchive.h"

namespace tbh::io {

// Upgrades raw container entries from `fromVersion` to kSchemaVersion by running
// migrate_vN_to_vN+1 steps in order. Rejects versions newer than this build supports.
bool migrateToCurrent(int fromVersion, ZipEntries &entries, QString *error);

} // namespace tbh::io
