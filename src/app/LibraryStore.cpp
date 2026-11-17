// SPDX-License-Identifier: GPL-3.0-or-later
#include "LibraryStore.h"

#include "io/HarnessJson.h"
#include "model/Library.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace tbh::app::libraryStore {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::app::LibraryStore)
};
} // namespace

namespace {

// Set once the seed connector parts have been added to an existing user library.
constexpr auto kConnectorPartsSeededKey = "library/connectorPartsSeeded";

// Adds the seed connector parts to a library from before they existed.
Library withSeedConnectorParts(const Library &library)
{
    if (QSettings().value(QLatin1StringView(kConnectorPartsSeededKey)).toBool())
        return library;
    const Library seed = defaultLibrary();
    Library parts;
    parts.connectorParts = seed.connectorParts;
    parts.pinParts = seed.pinParts;
    const Library merged = mergeLibraries(library, parts);
    if (merged == library || saveUserLibrary(merged, nullptr)) {
        QSettings().setValue(QLatin1StringView(kConnectorPartsSeededKey), true);
        return merged;
    }
    return library; // not saved: try again next time
}

} // namespace

QString userLibraryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library.json");
}

Library defaultLibrary()
{
    QFile file(QStringLiteral(":/default-library/library.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return io::deserializeLibrary(file.readAll(), nullptr).value_or(Library{});
}

Library loadUserLibrary(QString *warning)
{
    QFile file(userLibraryPath());
    if (!file.exists()) {
        QSettings().setValue(QLatin1StringView(kConnectorPartsSeededKey), true); // the seed has them
        return defaultLibrary();
    }
    QString error;
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
    } else if (auto library = io::deserializeLibrary(file.readAll(), &error)) {
        file.close();
        return withSeedConnectorParts(*library);
    }
    if (warning)
        *warning = Tr::tr("Your parts library (%1) could not be read: %2. Using the default library for now; "
                      "the file was not changed.")
                       .arg(QDir::toNativeSeparators(userLibraryPath()), error);
    return defaultLibrary();
}

bool saveUserLibrary(const Library &library, QString *error)
{
    const QString path = userLibraryPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray data = io::serializeLibrary(library);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = Tr::tr("Could not save the parts library to %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace tbh::app::libraryStore
