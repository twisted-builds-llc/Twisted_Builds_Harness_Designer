// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Library helpers. The Library types live in Harness.h because a harness embeds one.

#include "model/Harness.h"

namespace tbh {

// The user's entries plus embedded entries whose ids (part numbers for connector parts) the
// user library lacks. The user's
// own (verified) data wins; files from other machines still resolve their parts.
Library mergeLibraries(const Library &user, const Library &embedded);

// Entries of `library` referenced by the harness (wire types of its wires, sleeving of its
// segments, connector parts by the part numbers on its connectors, pins and free ends), for
// embedding on save.
Library usedLibraryEntries(const Harness &harness, const Library &library);

// Part numbers to offer for a cavity of housing `housingPartNumber`: first the parts of
// `kind` the housing lists (in its order, also ones missing from the library), then the
// library's other parts of that kind in library order. `part` is null for listed numbers the
// library lacks.
struct PartChoice
{
    QString partNumber;
    const PinPart *part = nullptr;
    bool madeForHousing = false;
};
std::vector<PartChoice> pinPartChoices(const Library &library, const QString &housingPartNumber,
                                       PinPartKind kind);

} // namespace tbh
