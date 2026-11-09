// SPDX-License-Identifier: GPL-3.0-or-later
#include "LibraryStore.h"

#include "io/HarnessJson.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace tbh::app::libraryStore {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("tbh::app::LibraryStore", text);
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
    if (!file.exists())
        return defaultLibrary();
    QString error;
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
    } else if (auto library = io::deserializeLibrary(file.readAll(), &error)) {
        return *library;
    }
    if (warning)
        *warning = tr("Your parts library (%1) could not be read: %2. Using the default library for now; "
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
            *error = tr("Could not save the parts library to %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace tbh::app::libraryStore
