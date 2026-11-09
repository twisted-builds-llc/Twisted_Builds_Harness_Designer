// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <optional>

namespace tbh::io {

// Reads a .tbh container: zip -> manifest/schema check -> migrations -> model.
std::optional<Harness> readTbhFile(const QString &path, QString *error);

// Writes atomically (temporary file, then rename). A harness without a valid
// createdAt gets the current time in the manifest.
bool writeTbhFile(const QString &path, const Harness &harness, QString *error);

} // namespace tbh::io
