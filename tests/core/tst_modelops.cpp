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

    void fillEmptyCircuitTextCopiesIntoEmptyPin()
    {
        // w1 joins J1.1 and J2.1. Text only on J1.1 -> J2.1 gets "IGN"; J1 is untouched.
        // w1 already has the name "5V", so the name stays.
        Harness h = sample();
        h.connectors[0].pins[0].circuitText = u"IGN"_s;
        const CircuitTextFill fill = fillEmptyCircuitText(h, {u"w1"_s});
        QCOMPARE(h.connectors[1].pins[0].circuitText, u"IGN"_s);
        QCOMPARE(h.connectors[0].pins[0].circuitText, u"IGN"_s);
        QCOMPARE(h.wires[0].name, u"5V"_s);
        QCOMPARE(fill.connectorIds, QSet<QString>{u"j2"_s});
        QVERIFY(fill.wireIds.isEmpty());

        // Same from the other side: text only on J2.1 -> J1.1 gets it.
        Harness h2 = sample();
        h2.connectors[1].pins[0].circuitText = u"GND"_s;
        QCOMPARE(fillEmptyCircuitText(h2, {u"w1"_s}).connectorIds, QSet<QString>{u"j1"_s});
        QCOMPARE(h2.connectors[0].pins[0].circuitText, u"GND"_s);
    }

    void fillEmptyCircuitTextNeverOverwrites()
    {
        // Both pins have text (user renamed one side): nothing changes.
        Harness h = sample();
        h.connectors[0].pins[0].circuitText = u"IGN"_s;
        h.connectors[1].pins[0].circuitText = u"IGN SW"_s;
        const Harness before = h;
        const CircuitTextFill fill = fillEmptyCircuitText(h, {u"w1"_s});
        QVERIFY(fill.connectorIds.isEmpty());
        QVERIFY(fill.wireIds.isEmpty());
        QCOMPARE(h, before);

        // Neither pin has text: nothing to copy, and the name "5V" is not pushed onto pins.
        Harness empty = sample();
        QVERIFY(fillEmptyCircuitText(empty, {u"w1"_s}).connectorIds.isEmpty());
        QCOMPARE(empty, sample());
    }

    void fillEmptyCircuitTextNamesUnnamedWire()
    {
        // w2 joins J1.2 and splice S1 and has no name: it takes "SIG" from J1.2.
        // w3 (S1 to flying lead) is in the same net through S1, so it is named "SIG" too.
        // Unknown wire ids are skipped. No pins are empty, so no connector changes.
        Harness h = sample();
        h.connectors[0].pins[1].circuitText = u"SIG"_s;
        const CircuitTextFill fill = fillEmptyCircuitText(h, {u"w2"_s, u"nope"_s});
        QCOMPARE(h.wires[1].name, u"SIG"_s);
        QCOMPARE(h.wires[2].name, u"SIG"_s);
        QCOMPARE(fill.wireIds, (QSet<QString>{u"w2"_s, u"w3"_s}));
        QVERIFY(fill.connectorIds.isEmpty());

        // No pin text anywhere in the net (w3 + w2): nothing changes.
        Harness empty = sample();
        QVERIFY(fillEmptyCircuitText(empty, {u"w3"_s}).wireIds.isEmpty());
        QCOMPARE(empty, sample());
    }

    void fillEmptyCircuitTextCrossesSplice()
    {
        // J1.2 -w2- S1 -w4- J3.1 and S1 -w5- J4.1. Text "POWER" on J1.2 only.
        // Drawing w4 (S1 to J3.1) must reach J1.2 through S1: J3.1 and J4.1 get
        // "POWER"; unnamed w2, w3, w4, w5 are named "POWER"; J1 itself is unchanged.
        Harness h = sample();
        h.connectors.push_back(connector(u"j3"_s, u"J3"_s, 1));
        h.connectors.push_back(connector(u"j4"_s, u"J4"_s, 1));
        h.wires.push_back(wire(u"w4"_s, SpliceRef{u"s1"_s}, PinRef{u"j3"_s, u"j3-p1"_s}));
        h.wires.push_back(wire(u"w5"_s, PinRef{u"j4"_s, u"j4-p1"_s}, SpliceRef{u"s1"_s}));
        h.connectors[0].pins[1].circuitText = u"POWER"_s;
        const CircuitTextFill fill = fillEmptyCircuitText(h, {u"w4"_s});
        QCOMPARE(h.connectors[2].pins[0].circuitText, u"POWER"_s);
        QCOMPARE(h.connectors[3].pins[0].circuitText, u"POWER"_s);
        QCOMPARE(fill.connectorIds, (QSet<QString>{u"j3"_s, u"j4"_s}));
        QCOMPARE(fill.wireIds, (QSet<QString>{u"w2"_s, u"w3"_s, u"w4"_s, u"w5"_s}));
        QCOMPARE(h.wires[3].name, u"POWER"_s);
        // w1 (J1.1 to J2.1) is not on S1: untouched.
        QVERIFY(h.connectors[1].pins[0].circuitText.isEmpty());

        // A renamed pin in the net is never overwritten: J3.1 "POWER B" stays, and an
        // unnamed wire on it takes its own pin's text rather than the net's.
        Harness r = sample();
        r.connectors.push_back(connector(u"j3"_s, u"J3"_s, 1));
        r.wires.push_back(wire(u"w4"_s, SpliceRef{u"s1"_s}, PinRef{u"j3"_s, u"j3-p1"_s}));
        r.connectors[0].pins[1].circuitText = u"POWER"_s;
        r.connectors[2].pins[0].circuitText = u"POWER B"_s;
        fillEmptyCircuitText(r, {u"w2"_s});
        QCOMPARE(r.connectors[2].pins[0].circuitText, u"POWER B"_s);
        QCOMPARE(r.wires[3].name, u"POWER B"_s);
        QCOMPARE(r.wires[1].name, u"POWER"_s);
    }
};

QTEST_GUILESS_MAIN(TestModelOps)
#include "tst_modelops.moc"
