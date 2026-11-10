// SPDX-License-Identifier: GPL-3.0-or-later
#include "SampleHarness.h"
#include "bom/BuildSheets.h"

#include <QTest>

using namespace tbh;
using namespace tbh::bom;
using namespace tbh::testing;

namespace {

CutLine line(const QString &name, double gauge, GaugeSystem system, const QString &base,
             const QString &stripe = {})
{
    CutLine c;
    c.name = name;
    c.gauge = Gauge{gauge, system};
    c.baseColor = base;
    c.stripeColor = stripe;
    return c;
}

QStringList names(const std::vector<CutLine> &lines)
{
    QStringList out;
    for (const CutLine &c : lines)
        out << (c.name.isEmpty() ? u"-"_s : c.name);
    return out;
}

} // namespace

class TestBuildSheets : public QObject
{
    Q_OBJECT

private slots:
    void cuttingOrder()
    {
        const std::vector<CutLine> input = {
            line(u"W10"_s, 20, GaugeSystem::Awg, u"BLK"_s),
            line(u"M1"_s, 1.5, GaugeSystem::Mm2, u"BLK"_s),
            line({}, 20, GaugeSystem::Awg, u"blk"_s),
            line(u"W3"_s, 20, GaugeSystem::Awg, u"BLK"_s, u"WHT"_s),
            line(u"W2"_s, 20, GaugeSystem::Awg, u"BLK"_s),
            line(u"W1"_s, 18, GaugeSystem::Awg, u"RED"_s),
            line(u"M2"_s, 2.5, GaugeSystem::Mm2, u"BLK"_s),
        };
        // Hand-sorted:
        //   18 AWG (thickest AWG): W1
        //   20 AWG BLK, no stripe: W2 < W10 (natural order), unnamed last ("blk" = "BLK")
        //   20 AWG BLK/WHT: W3
        //   mm2 after AWG, thickest first: 2.5 (M2), then 1.5 (M1)
        const QStringList expected = {u"W1"_s, u"W2"_s, u"W10"_s, u"-"_s, u"W3"_s, u"M2"_s, u"M1"_s};
        QCOMPARE(names(cutListInCuttingOrder(input)), expected);
    }

    void cuttingOrderKeepsTies()
    {
        CutLine a = line({}, 20, GaugeSystem::Awg, u"BLK"_s);
        a.wireId = u"first"_s;
        CutLine b = a;
        b.wireId = u"second"_s;
        const auto sorted = cutListInCuttingOrder({a, b});
        QCOMPARE(sorted[0].wireId, u"first"_s);
        QCOMPARE(sorted[1].wireId, u"second"_s);
    }

    void connectorPinouts()
    {
        Harness h = bomSample();
        h.connectors[0].pins[0].circuitText = u"5V REF"_s;
        const auto tables = pinoutTables(h);
        // ECU, SENSOR, LAMP, then splice S1 (it has wires).
        QCOMPARE(tables.size(), size_t(4));

        const PinoutTable &ecu = tables[0];
        QCOMPARE(ecu.title, u"ECU"_s);
        QCOMPARE(ecu.partNumber, u"ECU-PN"_s);
        QCOMPARE(ecu.description, u"ECU header"_s);
        // Pins 1-4 have one wire each; pin 5 is an empty cavity plug.
        QCOMPARE(ecu.rows.size(), size_t(5));
        QCOMPARE(ecu.rows[0].pin, u"1"_s);
        QCOMPARE(ecu.rows[0].circuit, u"5V REF"_s);
        QCOMPARE(ecu.rows[0].wireId, u"w1"_s);
        QCOMPARE(ecu.rows[0].baseColor, u"RED"_s);
        QCOMPARE(ecu.rows[0].stripeColor, u"WHT"_s);
        QCOMPARE(ecu.rows[0].gauge, (Gauge{18, GaugeSystem::Awg}));
        QCOMPARE(ecu.rows[0].otherEnd, u"SENSOR : 1"_s);
        QCOMPARE(ecu.rows[1].otherEnd, u"S1"_s);         // w2 goes to the splice
        QCOMPARE(ecu.rows[2].otherEnd, u"SENSOR : 2"_s); // w4
        QCOMPARE(ecu.rows[3].otherEnd, u"LAMP : 1"_s);   // w5
        QCOMPARE(ecu.rows[4].pin, u"5"_s);
        QVERIFY(ecu.rows[4].wireId.isEmpty());
        QVERIFY(ecu.rows[4].cavityPlug);

        // w4 runs from ECU pin 3 (end A) to SENSOR pin 2 (end B).
        QCOMPARE(tables[1].title, u"SENSOR"_s);
        QCOMPARE(tables[1].rows[1].otherEnd, u"ECU : 3"_s);
    }

    void splicePinouts()
    {
        const auto tables = pinoutTables(bomSample());
        const PinoutTable &splice = tables[3];
        QCOMPARE(splice.title, u"S1"_s);
        QCOMPARE(splice.partNumber, u"SPL-1"_s);
        QCOMPARE(splice.rows.size(), size_t(2));
        QVERIFY(splice.rows[0].pin.isEmpty());
        QCOMPARE(splice.rows[0].wireId, u"w2"_s);
        QCOMPARE(splice.rows[0].otherEnd, u"ECU : 2"_s);       // w2 ends B at the splice
        QCOMPARE(splice.rows[1].wireId, u"w3"_s);
        QCOMPARE(splice.rows[1].otherEnd, u"Ring terminal"_s); // w3 ends A at the splice
    }

    void splicesWithoutWiresAreLeftOut()
    {
        Harness h = sample();
        h.wires.clear();
        const auto tables = pinoutTables(h);
        QCOMPARE(tables.size(), size_t(3)); // the three connectors only
        QCOMPARE(tables[2].rows.size(), size_t(1)); // LAMP: its one pin, no wire
    }
};

QTEST_GUILESS_MAIN(TestBuildSheets)
#include "tst_buildsheets.moc"
