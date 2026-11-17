// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Connector parts from a spreadsheet: one CSV file with a row per housing, terminal, seal
// or cavity plug. Columns are found by their header, in any order; unknown columns are
// ignored. Comma or semicolon separated (whichever the header line uses), optional quotes,
// UTF-8 with or without a byte order mark.
//
// Columns (header text, case-insensitive):
//   Type            housing | terminal | seal | plug          (required)
//   Part number                                                (required)
//   Manufacturer, Description
//   Family          housings
//   Contacts        housings: male | female | none (also pins / sockets)
//   Cavities        housings: label pattern, e.g. "1-12" or "A1-A8, B1-B8" (required)
//   Terminals, Seals, Plugs   housings: compatible part numbers, separated by ";" or ","
//   Wire from, Wire to        terminals: e.g. "20 AWG", "0.5 mm2" (no unit = AWG)
//   Insulation min, Insulation max   terminals and seals: outer diameter in mm

#include "model/Harness.h"

#include <QByteArray>
#include <QStringList>

namespace tbh::io {

struct PartsCsv
{
    std::vector<ConnectorPart> connectorParts;
    std::vector<PinPart> pinParts;
    // "Line 4: ..." for each row that was skipped, or why the file was not read at all.
    QStringList problems;
};

// Reads the rows. Imported parts are the user's own data: not approximate. A part number
// repeated in the file keeps its last row.
PartsCsv readPartsCsv(const QByteArray &data);

// A template: the header and one example row per type.
QByteArray partsCsvTemplate();

// Adds `imported` to `library`: parts with a part number already there (same kind for pin
// parts) replace that entry, others are appended. Counts go to `added` and `replaced`.
void mergeImportedParts(Library &library, const PartsCsv &imported, int *added, int *replaced);

} // namespace tbh::io
