// SPDX-License-Identifier: GPL-3.0-or-later
#include "SampleHarness.h"
#include "calc/WireLength.h"

#include <QTest>

using namespace tbh;
using namespace tbh::calc;
using namespace Qt::StringLiterals;

using namespace tbh::testing;


class TestWireLength : public QObject
{
    Q_OBJECT

private slots:
    void rounding()
    {
        QCOMPARE(roundUpToIncrement(808.5, 5), 810.0);
        QCOMPARE(roundUpToIncrement(810.0, 5), 810.0);
        QCOMPARE(roundUpToIncrement(100.0000000001, 1), 100.0); // float noise is not a step
        QCOMPARE(roundUpToIncrement(100.01, 1), 101.0);
        QCOMPARE(roundUpToIncrement(12.34, 0), 12.34); // no rounding
    }

    void pinToPin()
    {
        const Harness h = sample();
        const LayoutGraph g(h);
        const WireLength l = wireLength(h, g, wireById(h, u"w1"_s));
        // base 300 + 450 = 750; ends 10 + 10 = 20; loop 770 * 5 % = 38.5;
        // total 808.5; cut rounds up to 810.
        QCOMPARE(l.status, RouteStatus::Ok);
        QCOMPARE(l.segmentIds, (std::vector<QString>{u"seg1"_s, u"seg2"_s}));
        QCOMPARE(l.baseMm, 750.0);
        QCOMPARE(l.endAllowanceAMm, 10.0);
        QCOMPARE(l.endAllowanceBMm, 10.0);
        QCOMPARE(l.serviceLoopMm, 38.5);
        QCOMPARE(l.totalMm, 808.5);
        QCOMPARE(l.cutMm, 810.0);
        QVERIFY(!l.overridden);
    }

    void pinToSplice()
    {
        const Harness h = sample();
        const WireLength l = wireLength(h, LayoutGraph(h), wireById(h, u"w2"_s));
        // base 300 + 120 = 420; ends 10 + 5 = 15; 435 * 1.05 = 456.75; cut 460.
        QCOMPARE(l.baseMm, 420.0);
        QCOMPARE(l.totalMm, 456.75);
        QCOMPARE(l.cutMm, 460.0);
    }

    void spliceToFlyingLead()
    {
        const Harness h = sample();
        const WireLength l = wireLength(h, LayoutGraph(h), wireById(h, u"w3"_s));
        // The lead segment counts: base 80; ends 5 + 0 = 5; 85 * 1.05 = 89.25; cut 90.
        QCOMPARE(l.baseMm, 80.0);
        QCOMPARE(l.totalMm, 89.25);
        QCOMPARE(l.cutMm, 90.0);
    }

    void freeEndKindPicksAllowance()
    {
        Harness h = sample();
        std::get<FreeEnd>(h.wires[2].endB).kind = FreeEndKind::RingTerminal;
        const WireLength l = wireLength(h, LayoutGraph(h), h.wires[2]);
        // base 80; ends 5 + 15 = 20; 100 * 1.05 = 105; cut 105.
        QCOMPARE(l.totalMm, 105.0);
        QCOMPARE(l.cutMm, 105.0);
    }

    void overrideWins()
    {
        const Harness h = sample();
        const WireLength l = wireLength(h, LayoutGraph(h), wireById(h, u"w4"_s));
        // Routed like w1 (808.5 before override) but the manual 1000 mm is the cut length.
        QVERIFY(l.overridden);
        QCOMPARE(l.totalMm, 808.5);
        QCOMPARE(l.cutMm, 1000.0);
        QVERIFY(l.isKnown());
    }

    void unplacedEnd()
    {
        const Harness h = sample();
        const WireLength l = wireLength(h, LayoutGraph(h), wireById(h, u"w5"_s));
        QCOMPARE(l.status, RouteStatus::EndBNotInLayout);
        QVERIFY(!l.isKnown());
    }

    void noPath()
    {
        Harness h = sample();
        h.layoutSegments.erase(h.layoutSegments.begin() + 1); // drop seg2
        QCOMPARE(LayoutGraph(h).route(wireById(h, u"w1"_s)).status, RouteStatus::NoPath);
    }

    void cycleUsesShortestPathAndOverride()
    {
        Harness h = sample();
        h.layoutSegments.push_back(segment(u"direct"_s, u"n1"_s, u"n3"_s, 500));
        const LayoutGraph g(h);
        // 500 direct beats 300 + 450.
        QCOMPARE(wireLength(h, g, h.wires[0]).baseMm, 500.0);

        Wire forced = h.wires[0];
        forced.routeSegmentIds = {u"seg1"_s, u"seg2"_s};
        QCOMPARE(wireLength(h, g, forced).baseMm, 750.0);

        forced.routeSegmentIds = {u"seg1"_s, u"seg3"_s}; // ends at the splice, not the sensor
        QCOMPARE(g.route(forced).status, RouteStatus::InvalidOverride);
    }

    void sameNodeBothEnds()
    {
        Harness h = sample();
        h.wires.push_back(wire(u"loop"_s, PinRef{u"ecu"_s, u"ecu1"_s}, PinRef{u"ecu"_s, u"ecu2"_s}));
        const WireLength l = wireLength(h, LayoutGraph(h), h.wires.back());
        // base 0; ends 10 + 10 = 20; 20 * 1.05 = 21; cut 25.
        QCOMPARE(l.status, RouteStatus::Ok);
        QCOMPARE(l.baseMm, 0.0);
        QCOMPARE(l.cutMm, 25.0);
    }

    void segmentUsage()
    {
        const Harness h = sample();
        const auto usage = wiresBySegment(h, LayoutGraph(h));
        // seg1 carries w1, w2, w4 (w3 starts at the splice; w5 is unrouted).
        QCOMPARE(usage.value(u"seg1"_s), (std::vector<QString>{u"w1"_s, u"w2"_s, u"w4"_s}));
        QCOMPARE(usage.value(u"seg2"_s), (std::vector<QString>{u"w1"_s, u"w4"_s}));
        QCOMPARE(usage.value(u"seg3"_s), (std::vector<QString>{u"w2"_s}));
        QCOMPARE(usage.value(u"seg4"_s), (std::vector<QString>{u"w3"_s}));
    }

    void problems()
    {
        Harness h = sample();
        h.layoutSegments[3].lengthMm = 0;
        const auto problems = findProblems(h, h.library);
        QCOMPARE(problems.size(), size_t(2));
        QCOMPARE(problems[0].kind, Problem::Kind::UnroutedWire);
        QCOMPARE(problems[0].itemId, u"w5"_s);
        QVERIFY(problems[0].message.contains(u"LAMP"_s));
        QCOMPARE(problems[1].kind, Problem::Kind::SegmentWithoutLength);
        QCOMPARE(problems[1].itemId, u"seg4"_s);
    }
};

QTEST_GUILESS_MAIN(TestWireLength)
#include "tst_wirelength.moc"
