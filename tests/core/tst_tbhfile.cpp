// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/FileFormat.h"
#include "io/HarnessJson.h"
#include "io/TbhFile.h"
#include "io/ZipArchive.h"
#include "model/Harness.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace tbh;
using namespace Qt::StringLiterals;

namespace {

// A small harness touching every model field and every wire-end variant.
Harness sampleHarness()
{
    Harness h;
    h.info = {u"Test harness"_s, u"Line one\nLine two"_s, u"B"_s, u"Bench"_s, Units::Inches};
    h.settings.packingFactor = 0.75;
    h.settings.serviceLoopPercent = 2.5;
    h.settings.stripAllowanceMm = {{EndType::Pin, 6.0}, {EndType::RingTerminal, 8.5}};
    h.settings.lengthRoundingIncrementMm = 5.0;
    h.createdAt = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5, 678), QTimeZone::UTC);

    Connector ecu;
    ecu.id = u"c-ecu"_s;
    ecu.label = u"ECU-A"_s;
    ecu.partNumber = u"PN-1"_s;
    ecu.manufacturer = u"Maker"_s;
    ecu.gender = Gender::Female;
    for (int i = 1; i <= 3; ++i) {
        Pin p;
        p.id = u"ecu-p%1"_s.arg(i);
        p.label = u"A%1"_s.arg(i);
        p.circuitText = u"Circuit %1"_s.arg(i);
        p.terminalPartNumber = u"T-1"_s;
        ecu.pins.push_back(p);
    }
    ecu.pins[2].isCavityPlug = true;

    Connector sensor;
    sensor.id = u"c-sensor"_s;
    sensor.label = u"SENSOR"_s;
    sensor.gender = Gender::Male;
    sensor.pins.push_back(Pin{u"s-p1"_s, u"+"_s, u"5V"_s, {}, u"SEAL-1"_s, false, u"note"_s, {}});
    h.connectors = {ecu, sensor};

    h.splices.push_back(Splice{u"sp1"_s, u"S1"_s, u"SPL-1"_s, {}});

    Wire pinToPin;
    pinToPin.id = u"w1"_s;
    pinToPin.name = u"5V REF"_s;
    pinToPin.endA = PinRef{u"c-ecu"_s, u"ecu-p1"_s};
    pinToPin.endB = PinRef{u"c-sensor"_s, u"s-p1"_s};
    pinToPin.baseColor = u"RED"_s;
    pinToPin.stripeColor = u"#ffffff"_s;
    pinToPin.gauge = {20, GaugeSystem::Awg};
    pinToPin.lengthOverrideMm = 1234.5;
    pinToPin.routeSegmentIds = {u"seg1"_s};

    Wire toSplice;
    toSplice.id = u"w2"_s;
    toSplice.endA = PinRef{u"c-ecu"_s, u"ecu-p2"_s};
    toSplice.endB = SpliceRef{u"sp1"_s};
    toSplice.baseColor = u"BLK"_s;
    toSplice.gauge = {0.5, GaugeSystem::Mm2};

    Wire toRing;
    toRing.id = u"w3"_s;
    toRing.endA = SpliceRef{u"sp1"_s};
    toRing.endB = FreeEnd{FreeEndKind::RingTerminal, u"RING-M6"_s, u"GND"_s};
    toRing.baseColor = u"BLK"_s;
    h.wires = {pinToPin, toSplice, toRing};

    h.layoutNodes = {
        LayoutNode{u"n1"_s, LayoutNodeType::Connector, u"c-ecu"_s, QPointF(0, 0), {}},
        LayoutNode{u"n2"_s, LayoutNodeType::Branch, {}, QPointF(100.25, -50), {}},
        LayoutNode{u"n3"_s, LayoutNodeType::Connector, u"c-sensor"_s, QPointF(300, 0), {}},
        LayoutNode{u"n4"_s, LayoutNodeType::Splice, u"sp1"_s, QPointF(150, 80), {}},
    };
    h.layoutSegments = {
        LayoutSegment{u"seg1"_s, u"n1"_s, u"n2"_s, 250.0, u"Trunk"_s, {}, SleevingCoverage::Full, std::nullopt, {}},
        LayoutSegment{u"seg2"_s, u"n2"_s, u"n3"_s, 0.1, {}, u"sleeve-x"_s, SleevingCoverage::Full, std::nullopt, {}},
        LayoutSegment{u"seg3"_s, u"n2"_s, u"n4"_s, 75.0, {}, {}, SleevingCoverage::Full, QPointF(0.5, -0.25), {}},
    };
    // Layout notes first: files list layout.json notes before schematic.json notes.
    h.notes = {Note{u"note1"_s, NoteCanvas::Layout, u"Signal is blue, not yellow"_s, QPointF(-40, 120.5), {}},
               Note{u"note2"_s, NoteCanvas::Schematic, u"Check ECU pinout rev B"_s, QPointF(500, -80), {}}};

    h.otherEntries[u"future.json"_s] = QByteArrayLiteral("{\"future\": true}\n");
    h.bomExtras = {BomExtra{u"x1"_s, u"TAPE-19"_s, u"Cloth tape"_s, 2.5, u"roll"_s, {}, {}}};
    h.schematic.placements[u"c-ecu"_s] = SchematicPlacement{QPointF(10, 20), false, 40, {}};
    h.schematic.placements[u"sp1"_s] = SchematicPlacement{QPointF(-5.5, 7), true, 0, {}};
    h.schematic.placements[freeEndPlacementKey(u"w3"_s, true)] = SchematicPlacement{QPointF(400, 0), false, 0, {}};
    return h;
}

io::ZipEntries readRaw(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return io::readZip(file.readAll(), nullptr).value_or(io::ZipEntries{});
}

void writeRaw(const QString &path, const io::ZipEntries &entries)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(io::writeZip(entries, nullptr).value());
}

QByteArray toBytes(const QJsonObject &o)
{
    return QJsonDocument(o).toJson();
}

QByteArray toBytes(const QJsonArray &a)
{
    return QJsonDocument(a).toJson();
}

} // namespace

class TestTbhFile : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }

    // Writes the sample, applies `edit` to the raw entries, and returns the read error.
    template<typename Edit>
    QString readErrorAfter(Edit edit)
    {
        const QString file = path(u"edited.tbh"_s);
        if (!io::writeTbhFile(file, sampleHarness(), nullptr))
            return u"<write failed>"_s;
        io::ZipEntries entries = readRaw(file);
        edit(entries);
        writeRaw(file, entries);
        QString error;
        if (io::readTbhFile(file, &error))
            return {};
        return error;
    }

private slots:
    void roundTripPreservesEverything()
    {
        const Harness original = sampleHarness();
        const QString file = path(u"round.tbh"_s);
        QString error;
        QVERIFY2(io::writeTbhFile(file, original, &error), qPrintable(error));

        const auto loaded = io::readTbhFile(file, &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(loaded->info == original.info);
        QVERIFY(loaded->settings == original.settings);
        QCOMPARE(loaded->createdAt, original.createdAt);
        QVERIFY(loaded->connectors == original.connectors);
        QVERIFY(loaded->wires == original.wires);
        QVERIFY(loaded->splices == original.splices);
        QVERIFY(loaded->layoutNodes == original.layoutNodes);
        QVERIFY(loaded->layoutSegments == original.layoutSegments);
        QVERIFY(loaded->notes == original.notes);
        QVERIFY(loaded->otherEntries == original.otherEntries);
        QVERIFY(loaded->schematic == original.schematic);
        QVERIFY(loaded->bomExtras == original.bomExtras);
        QVERIFY(*loaded == original);
    }

    void manifestContents()
    {
        const QString file = path(u"manifest.tbh"_s);
        QVERIFY(io::writeTbhFile(file, sampleHarness(), nullptr));
        const io::ZipEntries entries = readRaw(file);
        const QJsonObject manifest =
            QJsonDocument::fromJson(entries.at(QString(io::kManifestEntry))).object();
        QCOMPARE(manifest.value(u"schemaVersion").toInt(), io::kSchemaVersion);
        QCOMPARE(manifest.value(u"appVersion").toString(), QStringLiteral(TBH_APP_VERSION));
        QCOMPARE(manifest.value(u"createdAt").toString(), u"2026-01-02T03:04:05.678Z"_s);
        QVERIFY(QDateTime::fromString(manifest.value(u"modifiedAt").toString(), Qt::ISODateWithMs)
                    .isValid());
    }

    void missingCreatedAtIsFilledOnSave()
    {
        Harness h = sampleHarness();
        h.createdAt = {};
        const QString file = path(u"created.tbh"_s);
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        const auto loaded = io::readTbhFile(file, nullptr);
        QVERIFY(loaded);
        QVERIFY(loaded->createdAt.isValid());
    }

    void serializationIsStable()
    {
        // Same model -> identical bytes; read + re-serialize -> identical bytes.
        const Harness h = sampleHarness();
        const io::ZipEntries first = io::serializeHarness(h);
        QVERIFY(first == io::serializeHarness(h));
        const auto reread = io::deserializeHarness(first, nullptr);
        QVERIFY(reread);
        QVERIFY(io::serializeHarness(*reread) == first);
    }

    void unknownKeysAndEntriesArePreserved()
    {
        const QString file = path(u"unknown.tbh"_s);
        QVERIFY(io::writeTbhFile(file, sampleHarness(), nullptr));
        io::ZipEntries entries = readRaw(file);

        QJsonObject harness = QJsonDocument::fromJson(entries.at(u"harness.json"_s)).object();
        harness[u"futureHarnessKey"] = 42;
        entries[u"harness.json"_s] = toBytes(harness);

        QJsonArray connectors = QJsonDocument::fromJson(entries.at(u"connectors.json"_s)).array();
        QJsonObject ecu = connectors[0].toObject();
        ecu[u"futureConnectorKey"] = u"x"_s;
        QJsonArray pins = ecu[u"pins"].toArray();
        QJsonObject pin = pins[0].toObject();
        pin[u"futurePinKey"] = true;
        pins[0] = pin;
        ecu[u"pins"] = pins;
        connectors[0] = ecu;
        entries[u"connectors.json"_s] = toBytes(connectors);
        entries[u"future/thing.bin"_s] = QByteArrayLiteral("\x00\x01\x02");
        writeRaw(file, entries);

        QString error;
        const auto loaded = io::readTbhFile(file, &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->extra.value(u"futureHarnessKey").toInt(), 42);
        QCOMPARE(loaded->connectors[0].extra.value(u"futureConnectorKey").toString(), u"x"_s);
        QCOMPARE(loaded->connectors[0].pins[0].extra.value(u"futurePinKey").toBool(), true);

        // Survives a second save.
        QVERIFY(io::writeTbhFile(file, *loaded, nullptr));
        const io::ZipEntries resaved = readRaw(file);
        QVERIFY(resaved.at(u"harness.json"_s).contains("futureHarnessKey"));
        QVERIFY(resaved.at(u"connectors.json"_s).contains("futureConnectorKey"));
        QVERIFY(resaved.at(u"connectors.json"_s).contains("futurePinKey"));
        QCOMPARE(resaved.at(u"future/thing.bin"_s), QByteArrayLiteral("\x00\x01\x02"));
        QCOMPARE(resaved.at(u"future.json"_s), QByteArrayLiteral("{\"future\": true}\n"));
    }

    void newerSchemaIsRejected()
    {
        const QString error = readErrorAfter([](io::ZipEntries &e) {
            e[QString(io::kManifestEntry)] = toBytes(QJsonObject{{u"schemaVersion"_s, 99}});
        });
        QVERIFY(error.contains(u"99"));
    }

    void invalidSchemaVersionIsRejected()
    {
        QVERIFY(!readErrorAfter([](io::ZipEntries &e) {
                     e[QString(io::kManifestEntry)] = toBytes(QJsonObject{{u"schemaVersion"_s, 0}});
                 }).isEmpty());
        QVERIFY(!readErrorAfter([](io::ZipEntries &e) {
                     e[QString(io::kManifestEntry)] = QByteArrayLiteral("{}");
                 }).isEmpty());
    }

    void missingManifestIsRejected()
    {
        QVERIFY(!readErrorAfter([](io::ZipEntries &e) { e.erase(QString(io::kManifestEntry)); })
                     .isEmpty());
    }

    void notAZipIsRejected()
    {
        const QString file = path(u"garbage.tbh"_s);
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("definitely not a zip file");
        f.close();
        QString error;
        QVERIFY(!io::readTbhFile(file, &error));
        QVERIFY(!error.isEmpty());
    }

    void missingFileIsRejected()
    {
        QString error;
        QVERIFY(!io::readTbhFile(path(u"does-not-exist.tbh"_s), &error));
        QVERIFY(!error.isEmpty());
    }

    void invalidJsonIsRejected()
    {
        const QString error = readErrorAfter(
            [](io::ZipEntries &e) { e[u"wires.json"_s] = QByteArrayLiteral("[{"); });
        QVERIFY(error.contains(u"wires.json"));
    }

    void wrongFieldTypeReportsLocation()
    {
        const QString error = readErrorAfter([](io::ZipEntries &e) {
            QJsonArray connectors = QJsonDocument::fromJson(e.at(u"connectors.json"_s)).array();
            QJsonObject c = connectors[1].toObject();
            QJsonArray pins = c[u"pins"].toArray();
            QJsonObject p = pins[0].toObject();
            p[u"label"] = 7;
            pins[0] = p;
            c[u"pins"] = pins;
            connectors[1] = c;
            e[u"connectors.json"_s] = toBytes(connectors);
        });
        QVERIFY2(error.contains(u"connectors[1].pins[0]") && error.contains(u"label"),
                 qPrintable(error));
    }

    void danglingReferencesAreRejected()
    {
        auto withWire = [this](auto edit) {
            return readErrorAfter([&](io::ZipEntries &e) {
                QJsonArray wires = QJsonDocument::fromJson(e.at(u"wires.json"_s)).array();
                QJsonObject w = wires[0].toObject();
                edit(w);
                wires[0] = w;
                e[u"wires.json"_s] = toBytes(wires);
            });
        };
        QVERIFY(withWire([](QJsonObject &w) {
                    w[u"endA"] = QJsonObject{{u"type"_s, u"pin"_s},
                                             {u"connectorId"_s, u"nope"_s},
                                             {u"pinId"_s, u"ecu-p1"_s}};
                }).contains(u"nope"));
        QVERIFY(withWire([](QJsonObject &w) {
                    w[u"endA"] = QJsonObject{{u"type"_s, u"pin"_s},
                                             {u"connectorId"_s, u"c-ecu"_s},
                                             {u"pinId"_s, u"nope"_s}};
                }).contains(u"nope"));
        QVERIFY(withWire([](QJsonObject &w) {
                    w[u"endB"] = QJsonObject{{u"type"_s, u"splice"_s}, {u"spliceId"_s, u"nope"_s}};
                }).contains(u"nope"));
        QVERIFY(withWire([](QJsonObject &w) {
                    w[u"routeSegmentIds"] = QJsonArray{u"nope"_s};
                }).contains(u"nope"));
    }

    void duplicateIdsAreRejected()
    {
        const QString error = readErrorAfter([](io::ZipEntries &e) {
            QJsonArray splices = QJsonDocument::fromJson(e.at(u"splices.json"_s)).array();
            splices.append(splices[0]);
            e[u"splices.json"_s] = toBytes(splices);
        });
        QVERIFY2(error.contains(u"sp1"), qPrintable(error));
    }

    // Files written before curved segments and notes still load: straight, no notes.
    void layoutWithoutBendsOrNotesLoads()
    {
        const QString file = path(u"old-layout.tbh"_s);
        QVERIFY(io::writeTbhFile(file, sampleHarness(), nullptr));
        io::ZipEntries entries = readRaw(file);
        QJsonObject layout = QJsonDocument::fromJson(entries.at(u"layout.json"_s)).object();
        layout.remove(u"notes"_s);
        QJsonArray segments = layout[u"segments"].toArray();
        QJsonObject seg3 = segments[2].toObject();
        QVERIFY(seg3.contains(u"bend"_s));
        seg3.remove(u"bend"_s);
        segments[2] = seg3;
        layout[u"segments"] = segments;
        entries[u"layout.json"_s] = QJsonDocument(layout).toJson();
        QJsonObject schematic = QJsonDocument::fromJson(entries.at(u"schematic.json"_s)).object();
        schematic.remove(u"notes"_s);
        entries[u"schematic.json"_s] = QJsonDocument(schematic).toJson();
        writeRaw(file, entries);
        QString error;
        const auto loaded = io::readTbhFile(file, &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(loaded->notes.empty());
        QVERIFY(!loaded->layoutSegments[2].bend);
    }

    // Each canvas file holds its own notes; the canvas comes from the file.
    void notesAreSavedWithTheirCanvas()
    {
        const io::ZipEntries entries = io::serializeHarness(sampleHarness());
        const QJsonArray layoutNotes =
            QJsonDocument::fromJson(entries.at(u"layout.json"_s)).object()[u"notes"].toArray();
        const QJsonArray schematicNotes =
            QJsonDocument::fromJson(entries.at(u"schematic.json"_s)).object()[u"notes"].toArray();
        QCOMPARE(layoutNotes.size(), 1);
        QCOMPARE(layoutNotes[0].toObject()[u"id"].toString(), u"note1"_s);
        QCOMPARE(schematicNotes.size(), 1);
        QCOMPARE(schematicNotes[0].toObject()[u"id"].toString(), u"note2"_s);
        QVERIFY(!schematicNotes[0].toObject().contains(u"canvas"_s));
        const auto loaded = io::deserializeHarness(entries, nullptr);
        QVERIFY(loaded);
        QCOMPARE(loaded->notes[1].canvas, NoteCanvas::Schematic);
    }

    void malformedBendAndDuplicateNotesAreRejected()
    {
        QString error = readErrorAfter([](io::ZipEntries &e) {
            QJsonObject layout = QJsonDocument::fromJson(e.at(u"layout.json"_s)).object();
            QJsonArray segments = layout[u"segments"].toArray();
            QJsonObject seg3 = segments[2].toObject();
            seg3[u"bend"] = QJsonObject{{u"x"_s, 0.5}}; // "y" missing
            segments[2] = seg3;
            layout[u"segments"] = segments;
            e[u"layout.json"_s] = QJsonDocument(layout).toJson();
        });
        QVERIFY2(error.contains(u"bend") && error.contains(u"\"y\""), qPrintable(error));
        error = readErrorAfter([](io::ZipEntries &e) {
            QJsonObject layout = QJsonDocument::fromJson(e.at(u"layout.json"_s)).object();
            QJsonArray notes = layout[u"notes"].toArray();
            notes.append(notes[0]);
            layout[u"notes"] = notes;
            e[u"layout.json"_s] = QJsonDocument(layout).toJson();
        });
        QVERIFY2(error.contains(u"note1"), qPrintable(error));
    }

    void pinCountLimits()
    {
        Harness h = sampleHarness();
        h.wires.clear();
        h.connectors[0].pins.clear();
        const QString file = path(u"pins.tbh"_s);
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        QString error;
        QVERIFY(!io::readTbhFile(file, &error)); // 0 pins

        for (int i = 0; i < kMaxPins; ++i)
            h.connectors[0].pins.push_back(Pin{newId(), QString::number(i + 1), {}, {}, {}, false, {}, {}});
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        const auto loaded = io::readTbhFile(file, &error);
        QVERIFY2(loaded, qPrintable(error)); // 999 pins
        QCOMPARE(loaded->connectors[0].pins.size(), size_t(kMaxPins));

        h.connectors[0].pins.push_back(Pin{newId(), u"1000"_s, {}, {}, {}, false, {}, {}});
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        QVERIFY(!io::readTbhFile(file, &error)); // 1000 pins
    }

    void everyUnitRoundTrips()
    {
        const QString file = path(u"units.tbh"_s);
        for (Units units : {Units::Inches, Units::Millimeters, Units::Centimeters}) {
            Harness h = sampleHarness();
            h.info.units = units;
            QVERIFY(io::writeTbhFile(file, h, nullptr));
            QCOMPARE(io::readTbhFile(file, nullptr)->info.units, units);
        }
        QVERIFY(readErrorAfter([](io::ZipEntries &e) {
                    QJsonObject harness = QJsonDocument::fromJson(e.at(u"harness.json"_s)).object();
                    harness[u"units"] = u"ft"_s;
                    e[u"harness.json"_s] = toBytes(harness);
                }).contains(u"ft"));
    }

    void layoutNodeReferences()
    {
        // A flying-lead node may reference a wire's free end ("<wireId>/b").
        Harness h = sampleHarness();
        h.layoutNodes.push_back(
            LayoutNode{u"n5"_s, LayoutNodeType::FlyingLead, freeEndPlacementKey(u"w3"_s, true), QPointF(), {}});
        QString error;
        QVERIFY2(io::deserializeHarness(io::serializeHarness(h), &error), qPrintable(error));

        Harness pinEnd = h; // w1's end B is a pin, not a free end
        pinEnd.layoutNodes.back().refId = freeEndPlacementKey(u"w1"_s, true);
        QVERIFY(!io::deserializeHarness(io::serializeHarness(pinEnd), &error));

        Harness duplicate = h; // two nodes for one connector
        duplicate.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Connector, u"c-ecu"_s, QPointF(), {}});
        QVERIFY(!io::deserializeHarness(io::serializeHarness(duplicate), &error));
        QVERIFY(error.contains(u"c-ecu"));

        Harness wrongType = h; // a connector id on a splice node
        wrongType.layoutNodes.back() = LayoutNode{u"n5"_s, LayoutNodeType::Splice, u"c-sensor"_s, QPointF(), {}};
        QVERIFY(!io::deserializeHarness(io::serializeHarness(wrongType), &error));
    }

    void sharedSplicesAtOnePoint()
    {
        // Splice node n4 (sp1) also holds sp2, side by side in the loom.
        Harness h = sampleHarness();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.layoutNodes[3].sharedSpliceIds = {u"sp2"_s};
        QString error;
        const auto loaded = io::deserializeHarness(io::serializeHarness(h), &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(loaded->layoutNodes == h.layoutNodes);
        // Written only when present, so files without it are unchanged.
        QVERIFY(!io::serializeHarness(sampleHarness()).at(u"layout.json"_s).contains("sharedSpliceIds"));

        Harness missing = h; // no such splice
        missing.layoutNodes[3].sharedSpliceIds = {u"gone"_s};
        QVERIFY(!io::deserializeHarness(io::serializeHarness(missing), &error));
        QVERIFY(error.contains(u"gone"));

        Harness twice = h; // sp1 is already the node's own splice
        twice.layoutNodes[3].sharedSpliceIds = {u"sp1"_s};
        QVERIFY(!io::deserializeHarness(io::serializeHarness(twice), &error));

        Harness notSplice = h; // only splice nodes share
        notSplice.layoutNodes[1].sharedSpliceIds = {u"sp2"_s};
        notSplice.layoutNodes[3].sharedSpliceIds.clear();
        QVERIFY(!io::deserializeHarness(io::serializeHarness(notSplice), &error));
    }

    void orphanPlacementsAreDropped()
    {
        Harness h = sampleHarness();
        h.schematic.placements[u"gone"_s] = SchematicPlacement{};
        h.schematic.placements[freeEndPlacementKey(u"w1"_s, false)] = SchematicPlacement{}; // pin end
        h.schematic.placements[u"gone-wire"_s] = SchematicPlacement{QPointF(100, 0), false, 0, {}};
        // A dragged wire route (keyed by an existing wire id) is kept.
        h.schematic.placements[u"w1"_s] = SchematicPlacement{QPointF(250, 0), false, 0, {}};
        const QString file = path(u"orphans.tbh"_s);
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        const auto loaded = io::readTbhFile(file, nullptr);
        QVERIFY(loaded);
        Schematic expected = sampleHarness().schematic;
        expected.placements[u"w1"_s] = SchematicPlacement{QPointF(250, 0), false, 0, {}};
        QVERIFY(loaded->schematic == expected);
    }

    void saveOverwritesExistingFile()
    {
        const QString file = path(u"overwrite.tbh"_s);
        Harness h = sampleHarness();
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        h.info.name = u"Second save"_s;
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        QCOMPARE(io::readTbhFile(file, nullptr)->info.name, u"Second save"_s);
    }

    void saveToMissingDirectoryFails()
    {
        QString error;
        QVERIFY(!io::writeTbhFile(path(u"no/such/dir/x.tbh"_s), sampleHarness(), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTbhFile)
#include "tst_tbhfile.moc"
