// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Library helpers. The Library types live in Harness.h because a harness embeds one.

#include "model/Harness.h"

namespace tbh {

// The user's entries plus embedded entries whose ids the user library lacks. The user's
// own (verified) data wins; files from other machines still resolve their parts.
Library mergeLibraries(const Library &user, const Library &embedded);

// Entries of `library` referenced by the harness (wire types of its wires, sleeving of its
// segments), for embedding on save.
Library usedLibraryEntries(const Harness &harness, const Library &library);

} // namespace tbh
