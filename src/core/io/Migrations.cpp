// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/Migrations.h"

#include "io/FileFormat.h"
#include "model/Harness.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>

namespace tbh::io {

using namespace Qt::StringLiterals;

namespace {

using MigrationStep = bool (*)(ZipEntries &entries, QString *error);

std::optional<QJsonDocument> parsed(const ZipEntries &entries, QLatin1StringView name)
{
    const auto it = entries.find(QString(name));
    if (it == entries.end())
        return std::nullopt;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(it->second, &error);
    if (error.error != QJsonParseError::NoError)
        return std::nullopt; // deserializeHarness reports it
    return doc;
}

// v1 kept a free end inside its wire end: {"type": "free", "kind", "label",
// "terminalPartNumber"}, placed on the schematic and referenced from the layout by
// "<wireId>/a" or "<wireId>/b". v2 makes each one an entity in freeEnds.json that the wire
// end references by "freeEndId", keyed by that id in the schematic and layout.
bool migrateV1ToV2(ZipEntries &entries, QString *)
{
    const auto wiresDoc = parsed(entries, kWiresEntry);
    if (!wiresDoc || !wiresDoc->isArray())
        return true;
    QHash<QString, QString> idByOldKey;
    QJsonArray freeEnds;
    QJsonArray wires = wiresDoc->array();
    for (qsizetype i = 0; i < wires.size(); ++i) {
        QJsonObject wire = wires[i].toObject();
        const QString wireId = wire.value(u"id").toString();
        for (const auto &[field, suffix] : {std::pair{u"endA", u"/a"}, std::pair{u"endB", u"/b"}}) {
            const QJsonObject end = wire.value(field).toObject();
            if (end.value(u"type").toString() != QLatin1StringView("free"))
                continue;
            const QString id = newId();
            QJsonObject freeEnd{{u"id"_s, id},
                                {u"kind"_s, end.value(u"kind").toString(u"flyingLead"_s)},
                                {u"label"_s, end.value(u"label").toString()},
                                {u"terminalPartNumber"_s, end.value(u"terminalPartNumber").toString()}};
            freeEnds.append(freeEnd);
            idByOldKey.insert(wireId + QStringView(suffix), id);
            wire[field] = QJsonObject{{u"type"_s, u"free"_s}, {u"freeEndId"_s, id}};
        }
        wires[i] = wire;
    }
    entries[QString(kWiresEntry)] = QJsonDocument(wires).toJson(QJsonDocument::Indented);
    entries[QString(kFreeEndsEntry)] = QJsonDocument(freeEnds).toJson(QJsonDocument::Indented);

    if (const auto doc = parsed(entries, kSchematicEntry); doc && doc->isObject()) {
        QJsonObject schematic = doc->object();
        QJsonObject placements = schematic.value(u"placements").toObject();
        for (auto it = idByOldKey.cbegin(); it != idByOldKey.cend(); ++it) {
            if (placements.contains(it.key()))
                placements[it.value()] = placements.take(it.key());
        }
        schematic[u"placements"] = placements;
        entries[QString(kSchematicEntry)] = QJsonDocument(schematic).toJson(QJsonDocument::Indented);
    }
    if (const auto doc = parsed(entries, kLayoutEntry); doc && doc->isObject()) {
        QJsonObject layout = doc->object();
        QJsonArray nodes = layout.value(u"nodes").toArray();
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            QJsonObject node = nodes[i].toObject();
            const QString refId = node.value(u"refId").toString();
            if (idByOldKey.contains(refId)) {
                node[u"refId"] = idByOldKey.value(refId);
                nodes[i] = node;
            }
        }
        layout[u"nodes"] = nodes;
        entries[QString(kLayoutEntry)] = QJsonDocument(layout).toJson(QJsonDocument::Indented);
    }
    return true;
}

// kMigrations[i] upgrades schema version i+1 to i+2. Append one step (and bump
// kSchemaVersion) for every backward-incompatible schema change.
constexpr std::array<MigrationStep, kSchemaVersion - 1> kMigrations{migrateV1ToV2};

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
