// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Fragment.h"
#include "model/HarnessModel.h"
#include "model/Queries.h"

#include <QSignalSpy>
#include <QTest>

using namespace tbh;
using namespace Qt::StringLiterals;

namespace {

Connector connector(const QString &id, const QString &label, int pins)
{
    Connector c;
    c.id = id;
    c.label = label;
    for (int i = 1; i <= pins; ++i)
        c.pins.push_back(Pin{id + u"-p"_s + QString::number(i), QString::number(i), {}, {}, {}, false, {}, {}});
    return c;
}

Wire wire(const QString &id, WireEnd a, WireEnd b)
{
    Wire w;
    w.id = id;
    w.endA = std::move(a);
    w.endB = std::move(b);
    return w;
}

// J1 (2 pins) and J2 (1 pin), splice S1; w1 J1.1-J2.1, w2 J1.2-S1, w3 S1-flying lead.
Harness sample()
{
    Harness h;
    h.connectors = {connector(u"j1"_s, u"J1"_s, 2), connector(u"j2"_s, u"J2"_s, 1)};
    h.splices = {Splice{u"s1"_s, u"S1"_s, {}, {}}};
    h.wires = {wire(u"w1"_s, PinRef{u"j1"_s, u"j1-p1"_s}, PinRef{u"j2"_s, u"j2-p1"_s}),
               wire(u"w2"_s, PinRef{u"j1"_s, u"j1-p2"_s}, SpliceRef{u"s1"_s}),
               wire(u"w3"_s, SpliceRef{u"s1"_s}, FreeEnd{FreeEndKind::FlyingLead, {}, u"L"_s})};
    h.wires[0].name = u"5V"_s;
    h.schematic.placements[u"j1"_s] = SchematicPlacement{QPointF(1, 2), false, 0, {}};
    h.schematic.placements[u"s1"_s] = SchematicPlacement{QPointF(3, 4), false, 0, {}};
    h.schematic.placements[freeEndPlacementKey(u"w3"_s, true)] = SchematicPlacement{QPointF(5, 6), false, 0, {}};
    return h;
}

} // namespace

class TestModelOps : public QObject
{
    Q_OBJECT

private slots:
    void unitConversions()
    {
        // 1 in = 25.4 mm exactly; 1 cm = 10 mm.
        QCOMPARE(mmToUnits(25.4, Units::Inches), 1.0);
        QCOMPARE(mmToUnits(254.0, Units::Centimeters), 25.4);
        QCOMPARE(mmToUnits(12.5, Units::Millimeters), 12.5);
        QCOMPARE(unitsToMm(2.0, Units::Inches), 50.8);
        QCOMPARE(unitsToMm(3.5, Units::Centimeters), 35.0);
        QCOMPARE(unitsToMm(7.0, Units::Millimeters), 7.0);
    }

    void queries()
    {
        const Harness h = sample();
        QCOMPARE(wiresOnConnector(h, u"j1"_s), (std::vector<QString>{u"w1"_s, u"w2"_s}));
        QCOMPARE(wiresOnConnector(h, u"j2"_s), (std::vector<QString>{u"w1"_s}));
        QCOMPARE(wiresOnPins(h, u"j1"_s, {u"j1-p2"_s}), (std::vector<QString>{u"w2"_s}));
        QCOMPARE(wiresOnSplice(h, u"s1"_s), (std::vector<QString>{u"w2"_s, u"w3"_s}));
        QCOMPARE(describeWire(h, h.wires[0]), u"5V (J1 : 1 - J2 : 1)"_s);
        QCOMPARE(describeWire(h, h.wires[2]), u"S1 - Flying lead L"_s);
    }

    void insertUpdateRemoveEmitSignals()
    {
        HarnessModel model;
        model.reset(sample());
        QSignalSpy added(&model, &HarnessModel::entityAdded);
        QSignalSpy changed(&model, &HarnessModel::entityChanged);
        QSignalSpy removed(&model, &HarnessModel::entityRemoved);
        QSignalSpy placement(&model, &HarnessModel::placementChanged);

        model.insert(connector(u"j3"_s, u"J3"_s, 1), 0);
        QCOMPARE(added.count(), 1);
        QCOMPARE(model.indexOf<Connector>(u"j3"_s), 0);

        Connector c = *model.find<Connector>(u"j3"_s);
        model.update(c); // unchanged: no signal
        QCOMPARE(changed.count(), 0);
        c.label = u"J3b"_s;
        model.update(c);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.find<Connector>(u"j3"_s)->label, u"J3b"_s);

        // Removing a wire drops its free-end placement.
        model.remove<Wire>(u"w3"_s);
        QCOMPARE(removed.count(), 1);
        QVERIFY(!model.find<Wire>(u"w3"_s));
        QVERIFY(!model.placement(freeEndPlacementKey(u"w3"_s, true)));

        model.remove<Splice>(u"s1"_s);
        QVERIFY(!model.placement(u"s1"_s));

        model.setPlacement(u"j1"_s, SchematicPlacement{QPointF(9, 9), true, 0, {}});
        QCOMPARE(placement.count(), 1);
        model.setPlacement(u"j1"_s, SchematicPlacement{QPointF(9, 9), true, 0, {}}); // same
        QCOMPARE(placement.count(), 1);
        model.setPlacement(u"j1"_s, std::nullopt);
        QCOMPARE(placement.count(), 2);
        QVERIFY(!model.placement(u"j1"_s));
    }

    void fragmentIncludesInternalWiresOnly()
    {
        const Harness h = sample();
        // J1 + S1: w2 (J1-S1) and w3 (S1-free) come along, w1 (to J2) does not.
        const Harness f = extractFragment(h, {u"j1"_s}, {u"s1"_s});
        QCOMPARE(f.connectors.size(), size_t(1));
        QCOMPARE(f.splices.size(), size_t(1));
        QCOMPARE(f.wires.size(), size_t(2));
        QCOMPARE(f.wires[0].id, u"w2"_s);
        QCOMPARE(f.wires[1].id, u"w3"_s);
        QCOMPARE(f.schematic.placements.size(), size_t(3));
    }

    void assignNewIdsRewritesReferences()
    {
        Harness f = extractFragment(sample(), {u"j1"_s, u"j2"_s}, {u"s1"_s});
        assignNewIds(f);
        QSet<QString> oldIds{u"j1"_s, u"j2"_s, u"s1"_s, u"w1"_s, u"w2"_s, u"w3"_s};
        for (const Connector &c : f.connectors)
            QVERIFY(!oldIds.contains(c.id));
        const auto &w1 = f.wires[0];
        const auto &endA = std::get<PinRef>(w1.endA);
        QCOMPARE(endA.connectorId, f.connectors[0].id);
        QCOMPARE(endA.pinId, f.connectors[0].pins[0].id);
        QCOMPARE(std::get<PinRef>(w1.endB).pinId, f.connectors[1].pins[0].id);
        QCOMPARE(std::get<SpliceRef>(f.wires[1].endB).spliceId, f.splices[0].id);
        QVERIFY(f.schematic.placements.count(f.connectors[0].id));
        QVERIFY(f.schematic.placements.count(f.splices[0].id));
        QVERIFY(f.schematic.placements.count(freeEndPlacementKey(f.wires[2].id, true)));
        QCOMPARE(f.schematic.placements.size(), size_t(3));
    }
};

QTEST_GUILESS_MAIN(TestModelOps)
#include "tst_modelops.moc"
