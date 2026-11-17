// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/PartFit.h"
#include "io/HarnessJson.h"
#include "model/Library.h"

#include <QTest>

using namespace tbh;
using namespace tbh::calc;
using namespace Qt::StringLiterals;

namespace {

Gauge awg(double n)
{
    return {n, GaugeSystem::Awg};
}

Gauge mm2(double v)
{
    return {v, GaugeSystem::Mm2};
}

// A size 16 terminal for 16-14 AWG on 1.35-3.0 mm insulation (made-up test values).
PinPart terminal16()
{
    PinPart p;
    p.kind = PinPartKind::Terminal;
    p.partNumber = u"T16"_s;
    p.wireMin = awg(16);
    p.wireMax = awg(14);
    p.insulationMinMm = 1.35;
    p.insulationMaxMm = 3.0;
    return p;
}

Library sampleLibrary()
{
    Library lib;
    ConnectorPart housing;
    housing.partNumber = u"DT04-2P"_s;
    housing.manufacturer = u"Deutsch"_s;
    housing.family = u"DT"_s;
    housing.gender = Gender::Male;
    housing.cavityLabels = {u"1"_s, u"2"_s};
    housing.terminals = {u"T16"_s};
    housing.seals = {};
    housing.plugs = {u"PLUG-16"_s};
    housing.approximate = true;
    lib.connectorParts = {housing};
    PinPart plug;
    plug.kind = PinPartKind::Plug;
    plug.partNumber = u"PLUG-16"_s;
    PinPart seal;
    seal.kind = PinPartKind::Seal;
    seal.partNumber = u"T16"_s; // same number, other kind: a separate entry
    seal.insulationMinMm = 1.7;
    lib.pinParts = {terminal16(), plug, seal};
    return lib;
}

} // namespace

class TestPartFit : public QObject
{
    Q_OBJECT

private slots:
    void gaugeAreas()
    {
        // d = 0.127 * 92^((36 - n) / 39) mm; area = pi d^2 / 4.
        // 18 AWG: 92^(18/39) = 8.06053, d = 1.02369, area = 0.82305 mm2.
        QVERIFY(qAbs(gaugeAreaMm2(awg(18)) - 0.82305) < 1e-4);
        // 16 AWG: 92^(20/39) = 10.16414, d = 1.29085, area = 1.30870 mm2.
        QVERIFY(qAbs(gaugeAreaMm2(awg(16)) - 1.30870) < 1e-4);
        // 14 AWG: 92^(22/39) = 12.81675, d = 1.62773, area = 2.08091 mm2.
        QVERIFY(qAbs(gaugeAreaMm2(awg(14)) - 2.08091) < 1e-4);
        QCOMPARE(gaugeAreaMm2(mm2(1.5)), 1.5);
    }

    void wireSize()
    {
        const PinPart t = terminal16(); // 1.30870 to 2.08091 mm2, 2 % allowance
        QCOMPARE(wireSizeFit(t, awg(18)), Fit::TooSmall); // 0.82305
        QCOMPARE(wireSizeFit(t, awg(16)), Fit::Fits);
        QCOMPARE(wireSizeFit(t, awg(14)), Fit::Fits);
        QCOMPARE(wireSizeFit(t, awg(12)), Fit::TooLarge); // 3.30877 mm2
        QCOMPARE(wireSizeFit(t, mm2(1.5)), Fit::Fits);
        // 1.30 mm2 is inside the allowance: 1.30870 * 0.98 = 1.28253.
        QCOMPARE(wireSizeFit(t, mm2(1.30)), Fit::Fits);
        QCOMPARE(wireSizeFit(t, mm2(1.25)), Fit::TooSmall);
        // 2.1 mm2 is inside it too: 2.08091 * 1.02 = 2.12253.
        QCOMPARE(wireSizeFit(t, mm2(2.1)), Fit::Fits);
        QCOMPARE(wireSizeFit(t, mm2(2.2)), Fit::TooLarge);

        PinPart swapped = t; // "14-16 AWG": largest first gives the same range
        std::swap(swapped.wireMin, swapped.wireMax);
        QCOMPARE(wireSizeFit(swapped, awg(18)), Fit::TooSmall);
        QCOMPARE(wireSizeFit(swapped, awg(16)), Fit::Fits);

        PinPart maxOnly = t;
        maxOnly.wireMin.reset();
        QCOMPARE(wireSizeFit(maxOnly, awg(24)), Fit::Fits);
        PinPart none;
        QCOMPARE(wireSizeFit(none, awg(18)), Fit::Unknown);
    }

    void insulation()
    {
        const PinPart t = terminal16(); // 1.35 to 3.0 mm
        QCOMPARE(insulationFit(t, 2.0), Fit::Fits);
        QCOMPARE(insulationFit(t, 1.0), Fit::TooSmall);
        QCOMPARE(insulationFit(t, 3.05), Fit::Fits);    // <= 3.0 * 1.02 = 3.06
        QCOMPARE(insulationFit(t, 3.2), Fit::TooLarge);
        QCOMPARE(insulationFit(PinPart{}, 2.0), Fit::Unknown);
    }

    void lookups()
    {
        const Library lib = sampleLibrary();
        QCOMPARE(lib.connectorPart(u" DT04-2P "_s)->family, u"DT"_s);
        QVERIFY(!lib.connectorPart(u""_s));
        QCOMPARE(lib.pinPart(PinPartKind::Terminal, u"T16"_s)->wireMin, std::optional<Gauge>(awg(16)));
        QCOMPARE(lib.pinPart(PinPartKind::Seal, u"T16"_s)->insulationMinMm, std::optional<double>(1.7));
        QVERIFY(!lib.pinPart(PinPartKind::Plug, u"T16"_s));
        const ConnectorPart &housing = lib.connectorParts[0];
        QCOMPARE(housing.parts(PinPartKind::Terminal), QStringList{u"T16"_s});
        QCOMPARE(housing.parts(PinPartKind::Plug), QStringList{u"PLUG-16"_s});
        QVERIFY(housing.parts(PinPartKind::Seal).isEmpty());
    }

    void jsonRoundTrip()
    {
        Library lib = sampleLibrary();
        lib.connectorParts[0].extra[u"future"_s] = 1;
        QString error;
        const auto back = io::deserializeLibrary(io::serializeLibrary(lib), &error);
        QVERIFY2(back, qPrintable(error));
        QVERIFY(*back == lib);

        // Duplicate part numbers of one kind are rejected.
        Library twice = lib;
        twice.pinParts.push_back(terminal16());
        QVERIFY(!io::deserializeLibrary(io::serializeLibrary(twice), &error));
        QVERIFY(error.contains(u"T16"_s));
    }

    void mergeAndEmbed()
    {
        Library user = sampleLibrary();
        Library embedded;
        ConnectorPart other = user.connectorParts[0];
        other.description = u"from another machine"_s;
        ConnectorPart extra;
        extra.partNumber = u"DT06-2S"_s;
        embedded.connectorParts = {other, extra};
        PinPart socket;
        socket.partNumber = u"S16"_s;
        embedded.pinParts = {socket};
        const Library merged = mergeLibraries(user, embedded);
        QVERIFY(merged.connectorPart(u"DT04-2P"_s)->description.isEmpty()); // user wins
        QVERIFY(merged.connectorPart(u"DT06-2S"_s));                       // embedded fills gaps
        QVERIFY(merged.pinPart(PinPartKind::Terminal, u"S16"_s));

        // Embedded on save: the housing on a connector, the terminal and plug on its pins.
        Harness h;
        Connector c;
        c.id = u"c1"_s;
        c.partNumber = u"DT04-2P"_s;
        c.pins = {Pin{u"p1"_s, u"1"_s, {}, u"T16"_s, {}, false, {}, {}, {}},
                  Pin{u"p2"_s, u"2"_s, {}, {}, {}, true, u"PLUG-16"_s, {}, {}}};
        h.connectors = {c};
        const Library used = usedLibraryEntries(h, merged);
        QCOMPARE(used.connectorParts.size(), size_t(1));
        QCOMPARE(used.pinParts.size(), size_t(2)); // terminal T16 and the plug; not the seal T16
        QCOMPARE(used.pinParts[0].kind, PinPartKind::Terminal);
        QCOMPARE(used.pinParts[1].kind, PinPartKind::Plug);
    }
};

QTEST_GUILESS_MAIN(TestPartFit)
#include "tst_partfit.moc"
