// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Printable build package: one PDF with the layout, the schematic, the wire cut list and
// the connector pinouts, each sheet framed with a title block. Always black on white,
// whatever the app theme.

#include "model/HarnessModel.h"

#include <QPageSize>

namespace tbh::app {

struct PdfExportOptions
{
    QPageSize pageSize{QPageSize::Letter}; // printed in landscape
    bool showBundleDiameters = true;
};

// Writes the package for `model`'s harness to `path`. Returns the number of sheets written,
// or 0 with `*error` set if the file could not be written.
int exportBuildPackage(const HarnessModel &model, const QString &path, const PdfExportOptions &options,
                       QString *error);

} // namespace tbh::app
