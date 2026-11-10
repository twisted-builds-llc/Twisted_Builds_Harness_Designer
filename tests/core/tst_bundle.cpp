// SPDX-License-Identifier: GPL-3.0-or-later
#include "SampleHarness.h"
#include "calc/Bundle.h"
#include "calc/Highlight.h"
#include "io/HarnessJson.h"
#include "model/Library.h"

#include <QTest>

using namespace tbh;
using namespace tbh::calc;
using namespace tbh::testing;

namespace {

Library library()
{
    Library lib;
    lib.wireTypes = {WireType{u"gxl"_s, u"GXL"_s,
                              {{Gauge{18, GaugeSystem::Awg}, 2.0}, {Gauge{20, GaugeSystem::Awg}, 1.5}}, false, {}}};
    auto sleeve = [](const QString &id, const QString &family, SleevingKind kind, double id_,
                     std::optional<double> recovered = std::nullopt) {
        return Sleeving{id, family, kind, id, id_, recovered, false, {}};
    };
    lib.sleevings = {sleeve(u"pet-6"_s, u"PET"_s, SleevingKind::Braided, 6.4),
                     sleeve(u"pet-3"_s, u"PET"_s, SleevingKind::Braided, 3.2),
                     sleeve(u"pet-9"_s, u"PET"_s, SleevingKind::Braided, 9.5),
                     sleeve(u"hs-5"_s, u"Shrink"_s, SleevingKind::HeatShrink, 4.8, 2.4),
                     sleeve(u"hs-6"_s, u"Shrink"_s, SleevingKind::HeatShrink, 6.4, 3.2)};
    return lib;
}

// sample(): w1 ecu1-sensor1, w2 ecu2-splice, w3 splice-flying lead, w4 ecu3-sensor2,
// w5 ecu4-lamp1 (unrouted). Give w1, w2, w4 GXL ODs 2.0, 2.0, 1.5.
Harness sampleWithTypes()
{
    Harness h = sample();
    for (const char *id : {"w1", "w2", "w4"}) {
        for (Wire &w : h.wires) {
            if (w.id == QLatin1StringView(id)) {
                w.wireTypeId = u"gxl"_s;
                w.gauge = Gauge{QLatin1StringView(id) == "w4" ? 20.0 : 18.0, GaugeSystem::Awg};
            }
        }
    }
    return h;
}

} // namespace

class TestBundle : public QObject
{
    Q_OBJECT

private slots:
    void estimatedOuterDiameter()
    {
        // 36 AWG: solid 0.127 mm; 0.127 * 1.15 + 1.0 = 1.14605 mm.
        QCOMPARE(estimatedOuterDiameterMm(Gauge{36, GaugeSystem::Awg}), 1.14605);
        // 18 AWG: 0.127 * 92^(18/39) = 1.02362 mm; * 1.15 + 1.0 = 2.17716 mm.
        QVERIFY(std::abs(estimatedOuterDiameterMm(Gauge{18, GaugeSystem::Awg}) - 2.17716) < 1e-4);
        // 1.0 mm2: sqrt(4 / pi) = 1.12838 mm; * 1.15 + 1.0 = 2.29764 mm.
        QVERIFY(std::abs(estimatedOuterDiameterMm(Gauge{1.0, GaugeSystem::Mm2}) - 2.29764) < 1e-4);
    }

    void wireDiameterFromLibraryOrEstimate()
    {
        const Library lib = library();
        Wire w;
        w.wireTypeId = u"gxl"_s;
        w.gauge = Gauge{18, GaugeSystem::Awg};
        QCOMPARE(wireOuterDiameter(lib, w).odMm, 2.0);
        QVERIFY(!wireOuterDiameter(lib, w).estimated);
        w.gauge = Gauge{14, GaugeSystem::Awg}; // not in the table
        QVERIFY(wireOuterDiameter(lib, w).estimated);
        w.wireTypeId.clear(); // no type
        QVERIFY(wireOuterDiameter(lib, w).estimated);
    }

    void bundleDiameterHandCalculated()
    {
        Harness h = sampleWithTypes();
        const Library lib = library();
        // seg1 carries w1, w2, w4: d = 2.0, 2.0, 1.5.
        // sum d^2 = 4 + 4 + 2.25 = 10.25; / 0.70 = 14.642857; sqrt = 3.826599 mm.
        const auto usage = wiresBySegment(h, LayoutGraph(h));
        BundleDiameter b = bundleDiameter(h, lib, usage.value(u"seg1"_s));
        QCOMPARE(b.wireCount, 3);
        QVERIFY(b.estimatedWireIds.empty());
        QVERIFY(std::abs(b.diameterMm - 3.826599) < 1e-6);

        // Packing factor 1.0: sqrt(10.25) = 3.201562 mm.
        h.settings.packingFactor = 1.0;
        b = bundleDiameter(h, lib, usage.value(u"seg1"_s));
        QVERIFY(std::abs(b.diameterMm - 3.201562) < 1e-6);

        // seg4 carries w3 (no wire type): estimated and flagged.
        b = bundleDiameter(h, lib, usage.value(u"seg4"_s));
        QCOMPARE(b.estimatedWireIds, (std::vector<QString>{u"w3"_s}));

        QCOMPARE(bundleDiameter(h, lib, {}).diameterMm, 0.0);
    }

    void gaugeChangeChangesDiameter()
    {
        // w4 from 20 AWG (1.5) to 18 AWG (2.0): sum 12; / 0.7 = 17.142857; sqrt = 4.140393 mm.
        Harness h = sampleWithTypes();
        h.wires[3].gauge = Gauge{18, GaugeSystem::Awg};
        const auto usage = wiresBySegment(h, LayoutGraph(h));
        QVERIFY(std::abs(bundleDiameter(h, library(), usage.value(u"seg1"_s)).diameterMm - 4.140393) < 1e-6);
    }

    void sleevingRecommendation()
    {
        const Library lib = library();
        // D 3.8266 + 10 % = 4.2093 required: 3.2 too small, 6.4 is the smallest that fits.
        SleevingRecommendation r = recommendSleeving(lib, u"PET"_s, 3.826599, 10);
        QVERIFY(r.sleeving);
        QCOMPARE(r.sleeving->id, u"pet-6"_s);
        QVERIFY(std::abs(r.requiredInnerDiameterMm - 4.2092589) < 1e-6);
        QCOMPARE(r.fit, SleevingFit::Fits);
        // D 2.8 + 10 % = 3.08: 3.2 fits.
        QCOMPARE(recommendSleeving(lib, u"PET"_s, 2.8, 10).sleeving->id, u"pet-3"_s);
        // Exactly the required size counts as fitting: 3.2 / 1.1 = 2.909...
        QCOMPARE(recommendSleeving(lib, u"PET"_s, 3.2 / 1.1, 10).sleeving->id, u"pet-3"_s);
        // Nothing big enough.
        r = recommendSleeving(lib, u"PET"_s, 10.0, 10);
        QVERIFY(!r.sleeving);
        QCOMPARE(r.fit, SleevingFit::TooSmall);
        // Unknown family.
        QVERIFY(!recommendSleeving(lib, u"Nope"_s, 1.0, 10).sleeving);
    }

    void heatShrink()
    {
        const Library lib = library();
        // D 2.0 + 10 % = 2.2: 4.8 supplied fits over, but recovers only to 2.4 > 2.0: warn.
        SleevingRecommendation r = recommendSleeving(lib, u"Shrink"_s, 2.0, 10);
        QCOMPARE(r.sleeving->id, u"hs-5"_s);
        QCOMPARE(r.fit, SleevingFit::ShrinkTooLoose);
        // D 3.0: 4.8 fits over (3.3 needed) and recovers to 2.4 <= 3.0.
        r = recommendSleeving(lib, u"Shrink"_s, 3.0, 10);
        QCOMPARE(r.fit, SleevingFit::Fits);
        QCOMPARE(checkSleeving(*lib.sleeving(u"pet-3"_s), 3.0, 10), SleevingFit::TooSmall);
    }

    void sleevingTooSmallIsAProblem()
    {
        Harness h = sampleWithTypes();
        h.layoutSegments[0].sleevingId = u"pet-3"_s; // 3.2 < 4.21 needed
        const auto problems = findProblems(h, library());
        const bool flagged = std::any_of(problems.begin(), problems.end(), [](const Problem &p) {
            return p.kind == Problem::Kind::SleevingTooSmall && p.itemId == u"seg1"_s;
        });
        QVERIFY(flagged);
    }

    void highlightFromConnector()
    {
        const Harness h = sample();
        HighlightSeed seed;
        seed.connectors = {u"sensor"_s};
        const HighlightSet s = relatedItems(h, LayoutGraph(h), seed);
        // sensor's wires w1 and w4 run ecu - n1 - seg1 - n2 - seg2 - n3 - sensor.
        QCOMPARE(s.wires, (QSet<QString>{u"w1"_s, u"w4"_s}));
        QCOMPARE(s.connectors, (QSet<QString>{u"sensor"_s, u"ecu"_s}));
        QCOMPARE(s.pins, (QSet<QString>{u"ecu/ecu1"_s, u"ecu/ecu3"_s, u"sensor/sensor1"_s, u"sensor/sensor2"_s}));
        QCOMPARE(s.layoutSegments, (QSet<QString>{u"seg1"_s, u"seg2"_s}));
        QCOMPARE(s.layoutNodes, (QSet<QString>{u"n1"_s, u"n2"_s, u"n3"_s}));
        QVERIFY(s.splices.isEmpty());
    }

    void highlightFromPin()
    {
        const Harness h = sample();
        HighlightSeed seed;
        seed.connectors = {u"ecu"_s};
        seed.pins = {pinKey(u"ecu"_s, u"ecu2"_s)}; // only this pin, not all of ECU
        const HighlightSet s = relatedItems(h, LayoutGraph(h), seed);
        QCOMPARE(s.wires, (QSet<QString>{u"w2"_s}));
        QCOMPARE(s.splices, (QSet<QString>{u"sp"_s}));
        QCOMPARE(s.layoutSegments, (QSet<QString>{u"seg1"_s, u"seg3"_s}));
    }

    void highlightFromSegmentWireSpliceAndBranch()
    {
        const Harness h = sample();
        const LayoutGraph g(h);
        HighlightSeed segment;
        segment.layoutSegments = {u"seg3"_s};
        QCOMPARE(relatedItems(h, g, segment).wires, (QSet<QString>{u"w2"_s}));

        HighlightSeed wireSeed;
        wireSeed.wires = {u"w3"_s};
        const HighlightSet w = relatedItems(h, g, wireSeed);
        QCOMPARE(w.freeEnds, (QSet<QString>{u"fl"_s}));
        QCOMPARE(w.layoutSegments, (QSet<QString>{u"seg4"_s}));
        QCOMPARE(w.layoutNodes, (QSet<QString>{u"n4"_s, u"n5"_s}));

        HighlightSeed splice;
        splice.splices = {u"sp"_s};
        QCOMPARE(relatedItems(h, g, splice).wires, (QSet<QString>{u"w2"_s, u"w3"_s}));

        HighlightSeed branch; // branch point n2: wires through seg1/seg2/seg3
        branch.layoutNodes = {u"n2"_s};
        QCOMPARE(relatedItems(h, g, branch).wires, (QSet<QString>{u"w1"_s, u"w2"_s, u"w4"_s}));

        HighlightSeed connectorNode; // node n4 stands for the splice
        connectorNode.layoutNodes = {u"n4"_s};
        QCOMPARE(relatedItems(h, g, connectorNode).wires, (QSet<QString>{u"w2"_s, u"w3"_s}));
    }

    void libraryMergeAndEmbed()
    {
        Library user = library();
        Library embedded;
        embedded.wireTypes = {WireType{u"gxl"_s, u"GXL other machine"_s, {}, false, {}},
                              WireType{u"custom"_s, u"Custom"_s, {}, false, {}}};
        const Library merged = mergeLibraries(user, embedded);
        QCOMPARE(merged.wireType(u"gxl"_s)->name, u"GXL"_s); // user wins
        QVERIFY(merged.wireType(u"custom"_s));                 // embedded fills gaps

        Harness h = sampleWithTypes();
        h.layoutSegments[1].sleevingId = u"pet-9"_s;
        const Library used = usedLibraryEntries(h, merged);
        QCOMPARE(used.wireTypes.size(), size_t(1));
        QCOMPARE(used.sleevings.size(), size_t(1));
        QCOMPARE(used.sleevings[0].id, u"pet-9"_s);
    }

    void libraryJsonRoundTrip()
    {
        Library lib = library();
        lib.wireTypes[0].extra[u"future"] = 1;
        lib.wireTypes[0].approximate = true;
        QString error;
        const auto back = io::deserializeLibrary(io::serializeLibrary(lib), &error);
        QVERIFY2(back, qPrintable(error));
        QVERIFY(*back == lib);

        Harness h = sampleWithTypes();
        h.library = lib;
        const auto loaded = io::deserializeHarness(io::serializeHarness(h), &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(loaded->library == lib);

        QVERIFY(!io::deserializeLibrary("{\"wireTypes\":[{\"id\":\"a\"},{\"id\":\"a\"}]}", &error));
        QVERIFY(error.contains(u"a"_s));
        QVERIFY(!io::deserializeLibrary(
            "{\"wireTypes\":[{\"id\":\"a\",\"outerDiameters\":[{\"gauge\":{\"value\":18},\"odMm\":0}]}]}", &error));
    }
};

QTEST_GUILESS_MAIN(TestBundle)
#include "tst_bundle.moc"
