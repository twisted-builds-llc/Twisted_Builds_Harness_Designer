// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/TbhFile.h"

#include "io/FileFormat.h"
#include "io/HarnessJson.h"
#include "io/Migrations.h"
#include "io/ZipArchive.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace tbh::io {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::io::TbhFile)
};
} // namespace

using namespace Qt::StringLiterals;

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

std::optional<Harness> readTbhFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, Tr::tr("Could not open \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    auto entries = readZip(file.readAll(), error);
    if (!entries)
        return std::nullopt;

    const auto manifestIt = entries->find(QString(kManifestEntry));
    if (manifestIt == entries->end()) {
        setError(error, Tr::tr("The file has no manifest; it is not a harness file."));
        return std::nullopt;
    }
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestIt->second);
    const QJsonValue version = manifestDoc.object().value(u"schemaVersion");
    if (!manifestDoc.isObject() || !version.isDouble()) {
        setError(error, Tr::tr("The file manifest is damaged (no schema version)."));
        return std::nullopt;
    }
    if (!migrateToCurrent(version.toInt(), *entries, error))
        return std::nullopt;

    auto harness = deserializeHarness(*entries, error);
    if (harness) {
        harness->createdAt = QDateTime::fromString(manifestDoc.object().value(u"createdAt").toString(),
                                                   Qt::ISODateWithMs);
    }
    return harness;
}

bool writeTbhFile(const QString &path, const Harness &harness, QString *error)
{
    ZipEntries entries = serializeHarness(harness);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime created = harness.createdAt.isValid() ? harness.createdAt.toUTC() : now;
    const QJsonObject manifest{
        {u"schemaVersion"_s, kSchemaVersion},
        {u"appVersion"_s, QStringLiteral(TBH_APP_VERSION)},
        {u"createdAt"_s, created.toString(Qt::ISODateWithMs)},
        {u"modifiedAt"_s, now.toString(Qt::ISODateWithMs)},
    };
    entries[QString(kManifestEntry)] = QJsonDocument(manifest).toJson(QJsonDocument::Indented);

    const auto archive = writeZip(entries, error);
    if (!archive)
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, Tr::tr("Could not write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    if (file.write(*archive) != archive->size() || !file.commit()) {
        setError(error, Tr::tr("Could not write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

} // namespace tbh::io
