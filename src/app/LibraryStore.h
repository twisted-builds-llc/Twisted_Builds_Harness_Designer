// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

// The user's parts library file (per user, outside any harness).
namespace tbh::app::libraryStore {

QString userLibraryPath();
// The approximate seed library shipped with the app.
Library defaultLibrary();
// The user's library; the seed library if there is none yet. If the file exists but cannot
// be read, returns the seed library and explains why in `warning` (the file is left alone).
// A library saved before connector parts existed (v0.1.0) gets the seed connector parts it
// lacks, once: they are saved to the file, and not added again after the user deletes them.
Library loadUserLibrary(QString *warning);
bool saveUserLibrary(const Library &library, QString *error);

} // namespace tbh::app::libraryStore
