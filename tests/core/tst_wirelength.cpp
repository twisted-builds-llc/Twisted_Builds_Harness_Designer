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
        h.freeEnds[0].kind = FreeEndKind::RingTerminal;
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

    void pointAlongRoute()
    {
        const Harness h = sample();
        const LayoutGraph g(h);
        const Wire &w1 = wireById(h, u"w1"_s); // n1 --seg1 300-- n2 --seg2 450-- n3
        // 100 from end A: inside seg1, 100 from n1 (its node A).
        auto p = calc::pointAlongRoute(g, w1, false, 100);
        QVERIFY(p);
        QCOMPARE(p->segmentId, u"seg1"_s);
        QVERIFY(p->fromNodeA);
        QCOMPARE(p->offsetMm, 100.0);
        // 300 from end A is exactly the branch point n2.
        p = calc::pointAlongRoute(g, w1, false, 300);
        QVERIFY(p);
        QCOMPARE(p->atNodeId, u"n2"_s);
        QVERIFY(p->segmentId.isEmpty());
        // 500 from end A: 500 - 300 = 200 into seg2 from n2 (its node A).
        p = calc::pointAlongRoute(g, w1, false, 500);
        QVERIFY(p);
        QCOMPARE(p->segmentId, u"seg2"_s);
        QVERIFY(p->fromNodeA);
        QCOMPARE(p->offsetMm, 200.0);
        // 100 from end B (n3): inside seg2, 100 from n3, which is seg2's node B.
        p = calc::pointAlongRoute(g, w1, true, 100);
        QVERIFY(p);
        QCOMPARE(p->segmentId, u"seg2"_s);
        QVERIFY(!p->fromNodeA);
        QCOMPARE(p->offsetMm, 100.0);
        // Not strictly between the ends: 0, the full 750, beyond it; unrouted w5.
        QVERIFY(!calc::pointAlongRoute(g, w1, false, 0));
        QVERIFY(!calc::pointAlongRoute(g, w1, false, 750));
        QVERIFY(!calc::pointAlongRoute(g, w1, false, 800));
        QVERIFY(!calc::pointAlongRoute(g, wireById(h, u"w5"_s), false, 10));
    }

    void factoryParts()
    {
        Harness h = sample();
        h.wires[0].factory = true; // w1
        h.wires[3].factory = true; // w4
        h.connectors[2].factory = true; // lamp, placed below with no segments
        h.layoutNodes.push_back(node(u"n6"_s, LayoutNodeType::Connector, u"lamp"_s));
        const LayoutGraph g(h);
        const FactoryParts parts = factoryOnlyParts(h, wiresBySegment(h, g));
        // seg1 also carries w2 (not factory); seg2 carries only w1 and w4.
        QCOMPARE(parts.segmentIds, QSet<QString>{u"seg2"_s});
        // n3 has only seg2; n2 and n1 also have seg1; n6 is a factory connector on its own.
        QCOMPARE(parts.nodeIds, (QSet<QString>{u"n3"_s, u"n6"_s}));

        // An empty segment (no routed wires) counts only between two factory connectors:
        // n3 (sensor) - n6 (lamp) once the sensor is factory; n1 (ecu, not factory) - n6 never.
        // Both are long (5000 mm) so no wire takes them as a shortcut.
        h.layoutSegments.push_back(LayoutSegment{u"seg5"_s, u"n3"_s, u"n6"_s, 5000, {}, {}, SleevingCoverage::Full,
                                                 std::nullopt, {}});
        h.layoutSegments.push_back(LayoutSegment{u"seg6"_s, u"n1"_s, u"n6"_s, 5000, {}, {}, SleevingCoverage::Full,
                                                 std::nullopt, {}});
        h.connectors[1].factory = true; // sensor
        const LayoutGraph g2(h);
        const FactoryParts empty = factoryOnlyParts(h, wiresBySegment(h, g2));
        QCOMPARE(empty.segmentIds, (QSet<QString>{u"seg2"_s, u"seg5"_s}));
        // n6 now also has seg6 (not factory), so it is no longer faded; n3 has seg2 and seg5.
        QCOMPARE(empty.nodeIds, QSet<QString>{u"n3"_s});
    }

    void nearestFactoryConnector()
    {
        Harness h = sample();
        QVERIFY(!calc::nearestFactoryConnector(h, LayoutGraph(h), u"n4"_s)); // none is factory
        h.connectors[0].factory = true; // ecu (n1)
        h.connectors[1].factory = true; // sensor (n3)
        // From the splice n4: ecu 120 + 300 = 420, sensor 120 + 450 = 570.
        auto nearest = calc::nearestFactoryConnector(h, LayoutGraph(h), u"n4"_s);
        QVERIFY(nearest);
        QCOMPARE(nearest->connectorId, u"ecu"_s);
        QCOMPARE(nearest->distanceMm, 420.0);
        h.connectors[0].factory = false;
        nearest = calc::nearestFactoryConnector(h, LayoutGraph(h), u"n4"_s);
        QVERIFY(nearest);
        QCOMPARE(nearest->connectorId, u"sensor"_s);
        QCOMPARE(nearest->distanceMm, 570.0);
    }

    void spliceRunLength()
    {
        Harness h = sample();
        // n4 sits between branch n2 and flying lead n5: seg3 120 + seg4 80 = 200.
        QCOMPARE(spliceRunLengthMm(h, u"n4"_s), std::optional<double>(200.0));
        // Not a pass-through splice: a branch node, and an unknown node.
        QCOMPARE(spliceRunLengthMm(h, u"n2"_s), std::nullopt);
        QCOMPARE(spliceRunLengthMm(h, u"nope"_s), std::nullopt);

        // A second splice n6 in the run (n4 --50-- n6 --30-- n5) extends it:
        // 120 + 50 + 30 = 200 from either splice.
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.layoutNodes.push_back(node(u"n6"_s, LayoutNodeType::Splice, u"sp2"_s));
        h.layoutSegments[3] = segment(u"seg4"_s, u"n4"_s, u"n6"_s, 50);
        h.layoutSegments.push_back(segment(u"seg5"_s, u"n6"_s, u"n5"_s, 30));
        QCOMPARE(spliceRunLengthMm(h, u"n4"_s), std::optional<double>(200.0));
        QCOMPARE(spliceRunLengthMm(h, u"n6"_s), std::optional<double>(200.0));

        // A segment without a length leaves the total unknown.
        h.layoutSegments[4].lengthMm = 0;
        QCOMPARE(spliceRunLengthMm(h, u"n4"_s), std::nullopt);

        // A splice at the end of a stub (one segment) is not in a run.
        h.layoutSegments.pop_back();
        QCOMPARE(spliceRunLengthMm(h, u"n6"_s), std::nullopt);
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

    void duplicateCircuitIds()
    {
        Harness h = sample();
        h.wires[0].circuitId = u"C001"_s;
        h.wires[1].circuitId = u"c001"_s; // same id, other case
        h.wires[2].circuitId = u"C001"_s; // third use: still one problem for the id
        h.wires[3].circuitId = u"C002"_s;
        std::vector<Problem> found;
        for (const Problem &p : findProblems(h, h.library)) {
            if (p.kind == Problem::Kind::DuplicateCircuitId)
                found.push_back(p);
        }
        QCOMPARE(found.size(), size_t(1));
        QCOMPARE(found[0].itemId, h.wires[1].id); // reported on its second wire
        QVERIFY(found[0].message.contains(u"c001"_s));
    }
};

QTEST_GUILESS_MAIN(TestWireLength)
#include "tst_wirelength.moc"
