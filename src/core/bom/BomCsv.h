// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "bom/Bom.h"

namespace tbh::bom {

enum class CsvSection { Wires, CutList, Cables, Sleeving, Parts, Extras };
// Every section, in the order bomToCsv() writes them.
inline constexpr CsvSection kCsvSections[] = {CsvSection::Wires,    CsvSection::CutList, CsvSection::Cables,
                                              CsvSection::Sleeving, CsvSection::Parts,   CsvSection::Extras};

// RFC 4180 CSV (comma separated, CRLF line ends, fields quoted when needed). Numbers use
// '.' as the decimal point so spreadsheets import them the same everywhere; lengths are in
// `units` with the unit in the column header.
QString sectionToCsv(const Bom &bom, CsvSection section, Units units);
// All sections in one file, each preceded by a title row and separated by a blank row.
QString bomToCsv(const Bom &bom, Units units);
// "Cable" or "Shield braid".
QString cableKindName(CableLine::Kind kind);
// The cut list's Cable column: the cables the wire is in, e.g. "TP1, CAN", or "CAN shield".
QString cutLineCableText(const CutLine &line);
// File-name friendly section name ("wires", "cut-list"...), for one file per section.
QString sectionFileName(CsvSection section);

} // namespace tbh::bom
