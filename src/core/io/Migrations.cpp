// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/Migrations.h"

#include "io/FileFormat.h"

#include <QCoreApplication>

#include <array>

namespace tbh::io {

namespace {

using MigrationStep = bool (*)(ZipEntries &entries, QString *error);

// kMigrations[i] upgrades schema version i+1 to i+2. Append one step (and bump
// kSchemaVersion) for every backward-incompatible schema change.
constexpr std::array<MigrationStep, kSchemaVersion - 1> kMigrations{};

} // namespace

bool migrateToCurrent(int fromVersion, ZipEntries &entries, QString *error)
{
    if (fromVersion < 1) {
        if (error)
            *error = QCoreApplication::translate("tbh::io::Migrations",
                                                 "Invalid schema version %1.")
                         .arg(fromVersion);
        return false;
    }
    if (fromVersion > kSchemaVersion) {
        if (error)
            *error = QCoreApplication::translate(
                         "tbh::io::Migrations",
                         "This file uses schema version %1, but this version of the application "
                         "only supports up to %2. Please update the application.")
                         .arg(fromVersion)
                         .arg(kSchemaVersion);
        return false;
    }
    for (int version = fromVersion; version < kSchemaVersion; ++version) {
        if (!kMigrations[size_t(version - 1)](entries, error))
            return false;
    }
    return true;
}

} // namespace tbh::io
