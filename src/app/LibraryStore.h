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
Library loadUserLibrary(QString *warning);
bool saveUserLibrary(const Library &library, QString *error);

} // namespace tbh::app::libraryStore
