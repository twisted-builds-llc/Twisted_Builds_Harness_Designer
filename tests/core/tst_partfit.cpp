// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/PartFit.h"
#include "calc/WireLength.h"
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

    void choicesForHousing()
    {
        Library lib = sampleLibrary();
        lib.connectorParts[0].terminals = {u"MISSING"_s, u"T16"_s};
        PinPart other;
        other.partNumber = u"T20"_s;
        lib.pinParts.insert(lib.pinParts.begin(), other); // library order: T20 first
        // Listed for the housing first, in its order, then the rest of the library.
        const auto terminals = pinPartChoices(lib, u"DT04-2P"_s, PinPartKind::Terminal);
        QCOMPARE(terminals.size(), size_t(3));
        QCOMPARE(terminals[0].partNumber, u"MISSING"_s);
        QVERIFY(terminals[0].madeForHousing && !terminals[0].part);
        QCOMPARE(terminals[1].partNumber, u"T16"_s);
        QVERIFY(terminals[1].madeForHousing && terminals[1].part);
        QCOMPARE(terminals[2].partNumber, u"T20"_s);
        QVERIFY(!terminals[2].madeForHousing);
        // Unknown housing: the whole library, nothing marked.
        const auto any = pinPartChoices(lib, u"NOPE"_s, PinPartKind::Terminal);
        QCOMPARE(any.size(), size_t(2));
        QVERIFY(!any[0].madeForHousing);
        // Kinds stay apart: the seal T16 only among seals.
        QCOMPARE(pinPartChoices(lib, u"DT04-2P"_s, PinPartKind::Seal).size(), size_t(1));
    }

    void problems()
    {
        // Housing DT06-2S lists terminal T16 and plug P1 (no seals). T16: 20-16 AWG,
        // insulation 1.35-3.0 mm. Seal S1: insulation 1.7-2.5 mm. Ring terminal R1: 12-10 AWG.
        // Wire type GXL: 14 AWG 2.4 mm, 12 AWG 2.7 mm.
        Library lib;
        ConnectorPart housing;
        housing.partNumber = u"DT06-2S"_s;
        housing.terminals = {u"T16"_s};
        housing.plugs = {u"P1"_s};
        lib.connectorParts = {housing};
        PinPart seal;
        seal.kind = PinPartKind::Seal;
        seal.partNumber = u"S1"_s;
        seal.insulationMinMm = 1.7;
        seal.insulationMaxMm = 2.5;
        PinPart ring;
        ring.partNumber = u"R1"_s;
        ring.wireMin = awg(12);
        ring.wireMax = awg(10);
        PinPart plug;
        plug.kind = PinPartKind::Plug;
        plug.partNumber = u"P1"_s;
        lib.pinParts = {terminal16(), seal, ring, plug};
        lib.wireTypes = {WireType{u"gxl"_s, u"GXL"_s, {{awg(14), 2.4}, {awg(12), 2.7}}, false, {}}};

        Harness h;
        Connector c;
        c.id = u"c1"_s;
        c.label = u"X1"_s;
        c.partNumber = u"DT06-2S"_s;
        c.pins = {Pin{u"p1"_s, u"1"_s, {}, u"T16"_s, u"S1"_s, false, {}, {}, {}},
                  Pin{u"p2"_s, u"2"_s, {}, u"T99"_s, {}, false, u"P99"_s, {}, {}},
                  Pin{u"p3"_s, u"3"_s, {}, u"T16"_s, {}, false, {}, {}, {}},
                  Pin{u"p4"_s, u"4"_s, {}, {}, {}, true, u"P1"_s, {}, {}}};
        h.connectors = {c};
        h.freeEnds = {FreeEnd{u"f1"_s, FreeEndKind::RingTerminal, u"GND"_s, u"R1"_s, {}}};
        auto wire = [](const QString &id, WireEnd a, WireEnd b, double gauge) {
            Wire w;
            w.id = id;
            w.circuitId = id.toUpper();
            w.endA = std::move(a);
            w.endB = std::move(b);
            w.gauge = awg(gauge);
            w.wireTypeId = u"gxl"_s;
            return w;
        };
        PinRef p1{u"c1"_s, u"p1"_s}, p2{u"c1"_s, u"p2"_s}, p3{u"c1"_s, u"p3"_s};
        // wa: X1.1, 12 AWG (3.30877 mm2, OD 2.7). wb: X1.2 - X1.3, 14 AWG. wc: X1.3 - ring, 14 AWG.
        h.wires = {wire(u"wa"_s, p1, SpliceRef{}, 12), wire(u"wb"_s, p2, p3, 14),
                   wire(u"wc"_s, p3, FreeEndRef{u"f1"_s}, 14)};

        std::vector<Problem> found;
        addPartProblems(h, lib, found);
        QStringList messages;
        for (const Problem &p : found) {
            QCOMPARE(p.kind, Problem::Kind::PartMismatch);
            messages << p.message;
        }
        // T16 takes 16-14 AWG: 1.30870 * 0.98 = 1.28253 to 2.08091 * 1.02 = 2.12253 mm2.
        // 1. X1.1: 12 AWG 3.30877 mm2 > 2.12253: too large.
        // 2. X1.1: seal S1 is not listed (the housing lists no seals).
        // 3. X1.1: OD 2.7 mm > 2.5 * 1.02 = 2.55 mm for S1. (T16 takes it: 2.7 <= 3.0.)
        // 4. X1.2: T99 is not listed (and not in the library: no fit check).
        // 5. X1.3: two 14 AWG wires: 2 * 2.08091 = 4.16182, shown 4.16 mm2 > 2.12253: too large.
        //    (No insulation check with two wires in one cavity.)
        // 6. Ring GND: 14 AWG 2.08091 mm2 < 12 AWG 3.30877 * 0.98 = 3.24259: too small.
        // X1.4's plug P1 is listed: fine. X1.2's leftover plug P99 is not checked: the
        // cavity is open (not marked plugged).
        QCOMPARE(messages.size(), 6);
        QCOMPARE(messages[0], u"X1 : 1: terminal T16 takes 16 AWG to 14 AWG; its wire WA is 12 AWG."_s);
        QCOMPARE(messages[1], u"X1 : 1: seal S1 is not listed for housing DT06-2S."_s);
        QCOMPARE(messages[2], u"X1 : 1: seal S1 takes insulation 1.7 mm to 2.5 mm; wire WA is 2.7 mm."_s);
        QCOMPARE(messages[3], u"X1 : 2: terminal T99 is not listed for housing DT06-2S."_s);
        QCOMPARE(messages[4], u"X1 : 3: terminal T16 takes 16 AWG to 14 AWG; its wires total 4.16 mm²."_s);
        QCOMPARE(messages[5], u"Ring terminal GND: terminal R1 takes 12 AWG to 10 AWG; its wire WC is 14 AWG."_s);
        QCOMPARE(found[5].itemId, u"f1"_s);
        QCOMPARE(found[0].itemId, u"c1"_s);
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
