// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QStringList>

namespace tbh::app {

// Standard wire color codes (e.g. "BLK", "LT GRN") in display order.
QStringList standardWireColorCodes();
// Translated display name for a code, e.g. "Black".
QString wireColorName(const QString &code);
// Render color for a stored value: a standard code or "#rrggbb". Invalid -> QColor().
QColor wireColor(const QString &value);

} // namespace tbh::app
