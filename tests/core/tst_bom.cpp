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
        QCOMPARE(bom.sleeving[0].segments, 2);
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
        // The plug's seal P/N is not also counted as a seal; it is the plug part number.
        QCOMPARE(findPart(bom, PartCategory::CavityPlug, u"PLUG-1"_s)->quantity, 1);
        QCOMPARE(findPart(bom, PartCategory::Splice, u"SPL-1"_s)->quantity, 1);
        // Order: category, then part number, "(no part number)" last.
        QCOMPARE(bom.parts.front().category, PartCategory::Connector);
        QCOMPARE(bom.parts.front().partNumber, u"ECU-PN"_s);
        QVERIFY(bom.parts[1].partNumber.isEmpty());
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
