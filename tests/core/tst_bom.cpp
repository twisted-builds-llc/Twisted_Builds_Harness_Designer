// SPDX-License-Identifier: GPL-3.0-or-later
#include "SampleHarness.h"
#include "bom/Bom.h"
#include "bom/BomCsv.h"

#include <QTest>

using namespace tbh;
using namespace tbh::bom;
using namespace tbh::testing;

namespace {

const PartLine *findPart(const Bom &bom, PartCategory category, const QString &pn)
{
    for (const PartLine &p : bom.parts) {
        if (p.category == category && p.partNumber == pn)
            return &p;
    }
    return nullptr;
}

} // namespace

class TestBom : public QObject
{
    Q_OBJECT

private slots:
    void wireTotals()
    {
        const Bom bom = buildBom(bomSample(), bomLibrary());
        QCOMPARE(bom.wires.size(), size_t(2));
        const WireLine &red = bom.wires[0];
        QCOMPARE(red.baseColor, u"RED"_s);
        QCOMPARE(red.pieces, 2);
        QCOMPARE(red.lengthMm, 1270.0);          // 810 + 460
        QCOMPARE(red.lengthWithWasteMm, 1333.5); // 1270 * 1.05
        QCOMPARE(red.piecesWithoutLength, 0);
        const WireLine &black = bom.wires[1];
        QCOMPARE(black.pieces, 3);
        QCOMPARE(black.lengthMm, 1105.0);           // 105 + 1000; w5 unrouted
        QCOMPARE(black.lengthWithWasteMm, 1160.25); // 1105 * 1.05
        QCOMPARE(black.piecesWithoutLength, 1);
    }

    void wasteFactorFromSettings()
    {
        Harness h = bomSample();
        h.settings.wasteFactorWirePercent = 10;
        h.settings.wasteFactorSleevingPercent = 0;
        const Bom bom = buildBom(h, bomLibrary());
        QCOMPARE(bom.wires[0].lengthWithWasteMm, 1397.0); // 1270 * 1.10
        QCOMPARE(bom.sleeving[0].lengthWithWasteMm, 750.0);
    }

    void cutList()
    {
        const Bom bom = buildBom(bomSample(), bomLibrary());
        QCOMPARE(bom.cutList.size(), size_t(5));
        QCOMPARE(bom.cutList[0].cutMm, std::optional<double>(810.0));
        QCOMPARE(bom.cutList[0].endA, u"ECU : 1"_s);
        QCOMPARE(bom.cutList[0].endB, u"SENSOR : 1"_s);
        QVERIFY(bom.cutList[3].overridden);
        QCOMPARE(bom.cutList[3].cutMm, std::optional<double>(1000.0));
        QVERIFY(!bom.cutList[4].cutMm); // w5 not routed
    }

    void sleevingTotals()
    {
        const Bom bom = buildBom(bomSample(), bomLibrary());
        QCOMPARE(bom.sleeving.size(), size_t(3));
        QCOMPARE(bom.sleeving[0].sizeLabel, u"1/4 in"_s);
        QCOMPARE(bom.sleeving[0].pieces, 2);
        QCOMPARE(bom.sleeving[0].lengthMm, 750.0);          // 300 + 450
        QCOMPARE(bom.sleeving[0].lengthWithWasteMm, 787.5); // 750 * 1.05
        QCOMPARE(bom.sleeving[1].lengthWithWasteMm, 126.0); // 120 * 1.05
        QVERIFY(!bom.sleeving[2].known);                    // id not in the library
        QCOMPARE(bom.sleeving[2].lengthMm, 80.0);
    }

    void parts()
    {
        const Bom bom = buildBom(bomSample(), bomLibrary());
        QCOMPARE(findPart(bom, PartCategory::Connector, u"ECU-PN"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Connector, u"ECU-PN"_s)->description, u"ECU header"_s);
        const PartLine *noPn = findPart(bom, PartCategory::Connector, {});
        QCOMPARE(noPn->quantity, 2);
        QCOMPARE(noPn->references, (QStringList{u"SENSOR"_s, u"LAMP"_s}));
        // Terminals: T-1 on ecu1-3, T-2 on sensor1-2, ring RING-6 on w3; missing on ecu4, lamp1.
        QCOMPARE(findPart(bom, PartCategory::Terminal, u"T-1"_s)->quantity, 3);
        QCOMPARE(findPart(bom, PartCategory::Terminal, u"T-2"_s)->quantity, 2);
        QCOMPARE(findPart(bom, PartCategory::Terminal, u"RING-6"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Terminal, {})->quantity, 2);
        QCOMPARE(findPart(bom, PartCategory::Terminal, {})->references, (QStringList{u"ECU : 4"_s, u"LAMP : 1"_s}));
        QCOMPARE(findPart(bom, PartCategory::Seal, u"S-1"_s)->quantity, 2);
        // ecu5 is a cavity plug (no seal): counted from its plug part number.
        QCOMPARE(findPart(bom, PartCategory::CavityPlug, u"PLUG-1"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Splice, u"SPL-1"_s)->quantity, 1);
        // Order: category, then part number, "(no part number)" last.
        QCOMPARE(bom.parts.front().category, PartCategory::Connector);
        QCOMPARE(bom.parts.front().partNumber, u"ECU-PN"_s);
        QVERIFY(bom.parts[1].partNumber.isEmpty());
    }

    void factoryItemsAreNotCounted()
    {
        // An add-on to a factory harness: SENSOR is a factory connector; w1, w2 and w3 are
        // factory wires, so splice S1 and the ring terminal (only factory wires) exist too.
        // w4 (ECU 3 to SENSOR 2, override 1000 mm) and w5 (ECU 4 to LAMP, unrouted) are built.
        Harness h = bomSample();
        h.connectors[1].factory = true;
        for (Wire &w : h.wires)
            w.factory = w.id == u"w1"_s || w.id == u"w2"_s || w.id == u"w3"_s;
        h.freeEnds.push_back(FreeEnd{u"cut"_s, FreeEndKind::Cut, {}, u"X-1"_s, {}});
        const Bom bom = buildBom(h, bomLibrary());

        QCOMPARE(bom.cutList.size(), size_t(2));
        QCOMPARE(bom.cutList[0].wireId, u"w4"_s);
        QCOMPARE(bom.cutList[1].wireId, u"w5"_s);
        QCOMPARE(bom.wires.size(), size_t(1)); // the red group (w1, w2) is all factory
        QCOMPARE(bom.wires[0].pieces, 2);
        QCOMPARE(bom.wires[0].lengthMm, 1000.0); // w4's override; w5 unrouted
        QCOMPARE(bom.wires[0].piecesWithoutLength, 1);

        QCOMPARE(findPart(bom, PartCategory::Connector, u"ECU-PN"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Connector, {})->references, (QStringList{u"LAMP"_s}));
        // Terminals only where a built wire lands: ecu3 (T-1), SENSOR pin 2 (T-2, in the
        // factory housing), and ecu4, lamp1 without a part number.
        QCOMPARE(findPart(bom, PartCategory::Terminal, u"T-1"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Terminal, u"T-2"_s)->references, (QStringList{u"SENSOR : 2"_s}));
        QCOMPARE(findPart(bom, PartCategory::Terminal, {})->quantity, 2);
        QVERIFY(!findPart(bom, PartCategory::Terminal, u"RING-6"_s)); // only w3, a factory wire
        QVERIFY(!findPart(bom, PartCategory::Terminal, u"X-1"_s));    // cut ends never count
        QVERIFY(!findPart(bom, PartCategory::Seal, u"S-1"_s));        // ecu1, ecu2: factory wires
        QCOMPARE(findPart(bom, PartCategory::CavityPlug, u"PLUG-1"_s)->quantity, 1);
        QVERIFY(!findPart(bom, PartCategory::Splice, u"SPL-1"_s));
    }

    void labelsAndTies()
    {
        Harness h = bomSample();
        auto add = [&](const QString &id, AccessoryKind kind, const QString &segment, const QString &text,
                       const QString &partNumber, const QString &cover, double coverMm) {
            LayoutAccessory a;
            a.id = id;
            a.kind = kind;
            a.segmentId = segment;
            a.text = text;
            a.partNumber = partNumber;
            a.coverSleevingId = cover;
            a.coverLengthMm = coverMm;
            h.accessories.push_back(a);
        };
        add(u"a1"_s, AccessoryKind::Label, u"seg1"_s, u"ECU"_s, u"LBL-1"_s, u"hs"_s, 40);
        add(u"a2"_s, AccessoryKind::Label, u"seg2"_s, u"SENSOR"_s, u"LBL-1"_s, u"hs"_s, 40);
        add(u"a3"_s, AccessoryKind::Label, u"seg2"_s, u"LOGO"_s, u"LBL-2"_s, {}, 0); // no cover
        add(u"a4"_s, AccessoryKind::Tie, u"seg3"_s, {}, u"TIE-1"_s, u"pet-6"_s, 99); // ties have no cover
        const Bom bom = buildBom(h, bomLibrary());
        // Shrink "hs": seg3 120 + two covers 40 + 40 = 200 mm in 3 pieces; with waste 210.
        QCOMPARE(bom.sleeving[1].sleevingId, u"hs"_s);
        QCOMPARE(bom.sleeving[1].pieces, 3);
        QCOMPARE(bom.sleeving[1].lengthMm, 200.0);
        QCOMPARE(bom.sleeving[1].lengthWithWasteMm, 210.0);
        // PET is unchanged by the tie: 2 pieces, 750 mm.
        QCOMPARE(bom.sleeving[0].pieces, 2);
        QCOMPARE(bom.sleeving[0].lengthMm, 750.0);
        QCOMPARE(findPart(bom, PartCategory::Label, u"LBL-1"_s)->quantity, 2);
        QCOMPARE(findPart(bom, PartCategory::Label, u"LBL-1"_s)->references, (QStringList{u"ECU"_s, u"SENSOR"_s}));
        QCOMPARE(findPart(bom, PartCategory::Label, u"LBL-2"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Tie, u"TIE-1"_s)->quantity, 1);
        // Labels and ties come after the other parts.
        QCOMPARE(bom.parts.back().category, PartCategory::Tie);

        // A backshell and its strain relief tie on the ECU; the tie joins the layout ties.
        h.connectors[0].backshellPartNumber = u"BS-1"_s;
        h.connectors[0].strainReliefPartNumber = u"TIE-1"_s;
        const Bom withBackshell = buildBom(h, bomLibrary());
        QCOMPARE(findPart(withBackshell, PartCategory::Backshell, u"BS-1"_s)->quantity, 1);
        QCOMPARE(findPart(withBackshell, PartCategory::Backshell, u"BS-1"_s)->references, QStringList{u"ECU"_s});
        QCOMPARE(findPart(withBackshell, PartCategory::Tie, u"TIE-1"_s)->quantity, 2);
        // Backshells right after the connectors.
        QCOMPARE(withBackshell.parts[2].category, PartCategory::Backshell);
    }

    void flagged()
    {
        // BLK group without length, connector and terminal lines without P/N, unknown sleeving.
        QCOMPARE(buildBom(bomSample(), bomLibrary()).flaggedCount(), 4);
    }

    void csvWires()
    {
        const Bom bom = buildBom(bomSample(), bomLibrary());
        QCOMPARE(sectionToCsv(bom, CsvSection::Wires, Units::Millimeters),
                 u"Color,Stripe,Gauge,Wire type,Pieces,Length (mm),Length with waste (mm),Pieces without length\r\n"
                 "RED,WHT,18 AWG,,2,1270,1333.5,0\r\n"
                 "BLK,,20 AWG,,3,1105,1160.3,1\r\n"_s); // mm to 0.1
        // 1270 mm = 50 in; 1333.5 mm = 52.5 in.
        QVERIFY(sectionToCsv(bom, CsvSection::Wires, Units::Inches).contains(u"RED,WHT,18 AWG,,2,50,52.5,0"_s));
    }

    void csvQuotingAndSections()
    {
        const Bom bom = buildBom(bomSample(), bomLibrary());
        const QString cut = sectionToCsv(bom, CsvSection::CutList, Units::Millimeters);
        QVERIFY(cut.contains(u"\"5V, \"\"REF\"\"\",ECU : 1,SENSOR : 1,RED,WHT,18 AWG,,810,\r\n"_s));
        QVERIFY(cut.contains(u",,not routed\r\n"_s));
        QVERIFY(sectionToCsv(bom, CsvSection::Extras, Units::Millimeters)
                    .contains(u"TAPE,\"Cloth tape, 19 mm\",2,roll,\r\n"_s));
        QVERIFY(sectionToCsv(bom, CsvSection::Parts, Units::Millimeters)
                    .contains(u"Terminal,(no part number),,2,ECU : 4; LAMP : 1\r\n"_s));
        const QString all = bomToCsv(bom, Units::Millimeters);
        QVERIFY(all.startsWith(u"Wire\r\n"_s));
        QVERIFY(all.contains(u"\r\n\r\nCut list\r\n"_s));
        QVERIFY(all.contains(u"\r\n\r\nOther items\r\n"_s));
    }

    void emptyHarness()
    {
        const Bom bom = buildBom(Harness{}, Library{});
        QVERIFY(bom.wires.empty() && bom.parts.empty() && bom.sleeving.empty());
        QCOMPARE(bom.flaggedCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestBom)
#include "tst_bom.moc"
