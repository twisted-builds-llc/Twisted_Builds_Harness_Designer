// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>

#include <optional>

namespace tbh {

// Label for a pin appended after `previous`: a trailing number is incremented, keeping
// zero padding ("A8" -> "A9", "07" -> "08", "9" -> "10"). No trailing number -> "".
QString nextPinLabel(const QString &previous);

// Expands a pin label pattern such as "A1-A8, B1-B8, C1-C12" or "1-120".
// - Tokens are comma separated. "X3-X7" / "X3-7" is a range (descending allowed),
//   anything else is a literal label.
// - Zero padding of the start number is kept ("01-12" -> 01..12).
// - `step` takes every step-th number of each range (2 -> 1, 3, 5...).
// - `reverse` reverses the final list.
// Fails if the result is empty, has more than kMaxPins labels, or a range mixes prefixes.
std::optional<QStringList> expandPinPattern(const QString &pattern, int step, bool reverse,
                                            QString *error);

} // namespace tbh
