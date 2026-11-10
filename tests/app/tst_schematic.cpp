// SPDX-License-Identifier: GPL-3.0-or-later
#include "../core/SampleHarness.h"
#include "Autosave.h"
#include "calc/Highlight.h"
#include "Document.h"
#include "SelectionModel.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "io/HarnessJson.h"
#include "io/TbhFile.h"
#include "model/Queries.h"
#include "model/Fragment.h"
#include "schematic/SchematicScene.h"
#include "schematic/SchematicView.h"
#include "ui/ConnectorEdits.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"

#include <QElapsedTimer>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QTest>

using namespace tbh;
using namespace tbh::app;
using namespace Qt::StringLiterals;

namespace {

Connector makeConnector(const QString &label, int pins)
{
    Connector c;
    c.id = newId();
    c.label = label;
    c.pins = resizedPins({}, pins);
    for (Pin &p : c.pins)
        p.circuitText = label + u" circuit "_s + p.label;
    return c;
}

// A 300-pin ECU wired to small connectors with striped wires (the M2 acceptance shape).
Harness ecuHarness(int ecuPins, int smallConnectors, int pinsEach)
{
    Harness h;
    h.connectors.push_back(makeConnector(u"ECU"_s, ecuPins));
    h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
    int ecuPin = 0;
    for (int i = 0; i < smallConnectors; ++i) {
        Connector c = makeConnector(u"J%1"_s.arg(i + 1), pinsEach);
        h.schematic.placements[c.id] =
            SchematicPlacement{QPointF(600, i * (ConnectorItem::kHeaderHeight + pinsEach * 20 + 40)), false, 0, {}};
        for (const Pin &p : c.pins) {
            if (ecuPin >= ecuPins)
                break;
            Wire w;
            w.id = newId();
            w.name = p.circuitText;
            w.endA = PinRef{h.connectors[0].id, h.connectors[0].pins[size_t(ecuPin++)].id};
            w.endB = PinRef{c.id, p.id};
            w.baseColor = (ecuPin % 2) ? u"RED"_s : u"BLU"_s;
            w.stripeColor = (ecuPin % 3) ? u"WHT"_s : QString();
            w.gauge = {20, GaugeSystem::Awg};
            h.wires.push_back(w);
        }
        h.connectors.push_back(c);
    }
    return h;
}

struct Fixture
{
    Document document;
    SelectionModel selection{document.model()};
    SchematicView view{&document, &selection};

    Fixture()
    {
        view.resize(1200, 800);
        view.show();
        QTest::qWait(1); // let deferred zoomToFit run
    }
    SchematicScene *scene() { return view.schematicScene(); }
    HarnessModel *model() { return document.model(); }
    QPoint toView(const QPointF &scenePos) { return view.mapFromScene(scenePos); }
    void reset(Harness h)
    {
        model()->reset(std::move(h));
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(QPointF(450, 300));
    }
};

// sample() with every item placed and one schematic note.
Harness placedSample()
{
    Harness h = testing::sample();
    h.schematic.placements[u"ecu"_s] = SchematicPlacement{QPointF(0, 0), false, 0, {}, {}};
    h.schematic.placements[u"sensor"_s] = SchematicPlacement{QPointF(600, 0), false, 0, {}, {}};
    h.schematic.placements[u"lamp"_s] = SchematicPlacement{QPointF(600, 300), false, 0, {}, {}};
    h.schematic.placements[u"sp"_s] = SchematicPlacement{QPointF(400, 200), false, 0, {}, {}};
    h.schematic.placements[u"fl"_s] = SchematicPlacement{QPointF(500, 260), false, 0, {}, {}};
    h.notes.push_back(Note{u"note"_s, NoteCanvas::Schematic, u"Dash side"_s, QPointF(600, -60), {}, {}});
    return h;
}

QString imageDir()
{
    return qEnvironmentVariable("TBH_SCREENSHOT_DIR");
}

} // namespace

class TestSchematic : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_schematic"_s);
        QSettings().clear();
        QDir(Autosave::recoveryDirectory()).removeRecursively(); // no recovery prompt
    }

    // Adding connectors/splices repeatedly without moving the view must not stack them:
    // each new one steps 20, 20 (two grid cells) past the one already there.
    void addedItemsAreOffset()
    {
        Fixture f;
        f.reset(Harness{});
        f.view.addConnector();
        f.view.addConnector();
        f.view.addConnector();
        const Harness &h = f.model()->harness();
        QCOMPARE(h.connectors.size(), size_t(3));
        const QPointF p0 = f.model()->placement(h.connectors[0].id)->position;
        QCOMPARE(f.model()->placement(h.connectors[1].id)->position, p0 + QPointF(20, 20));
        QCOMPARE(f.model()->placement(h.connectors[2].id)->position, p0 + QPointF(40, 40));
        f.view.addSplice();
        f.view.addSplice();
        const QPointF s0 = f.model()->placement(h.splices[0].id)->position;
        QCOMPARE(f.model()->placement(h.splices[1].id)->position, s0 + QPointF(20, 20));
        f.document.undoStack()->undo(); // one undo removes one splice
        QCOMPARE(h.splices.size(), size_t(1));
    }

    // Add Flying Lead / Add Eyelet place a free end with no wire, selected. Dragging from
    // its lead to a pin draws a wire; dragging from a pin onto it draws another one.
    void addedFreeEndIsWiredByDraggingItsLead()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 3)};
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        f.reset(h);

        f.view.addFreeEnd(FreeEndKind::Eyelet);
        f.view.addFreeEnd(FreeEndKind::FlyingLead);
        const Harness &model = f.model()->harness();
        QCOMPARE(model.freeEnds.size(), size_t(2));
        QVERIFY(model.wires.empty());
        QCOMPARE(model.freeEnds[0].kind, FreeEndKind::Eyelet);
        QCOMPARE(model.freeEnds[1].kind, FreeEndKind::FlyingLead);
        const QString eyeletId = model.freeEnds[0].id;
        QCOMPARE(f.selection.selection().freeEnds, QSet<QString>{model.freeEnds[1].id});
        // Added at the same view center: the second steps 20, 20 past the first.
        QCOMPARE(f.model()->placement(model.freeEnds[1].id)->position,
                 f.model()->placement(eyeletId)->position + QPointF(20, 20));

        // Move the eyelet to (500, 100), then drag from its left lead onto pin 2's circuit cell.
        f.document.undoStack()->push(new SetPlacementsCommand(
            f.model(), {{eyeletId, SchematicPlacement{QPointF(500, 100), false, 0, {}}}}, u"Move"_s));
        FreeEndItem *eyelet = f.scene()->freeEndItem(eyeletId);
        QCOMPARE(eyelet->pos(), QPointF(500, 100));
        // Middle of the left lead: x = -(kHalfWidth + kStubLength / 2) = -(12 + 7) = -19.
        const QPoint lead = f.toView(eyelet->mapToScene(QPointF(-19, 0)));
        ConnectorItem *a = f.scene()->connectorItem(h.connectors[0].id);
        const QPoint pin = f.toView(a->mapToScene(a->circuitCellRect(1).center()));
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, lead);
        QTest::mouseMove(f.view.viewport(), lead - QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), pin);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, pin);
        QCOMPARE(model.wires.size(), size_t(1));
        QCOMPARE(std::get<FreeEndRef>(model.wires[0].endA).freeEndId, eyeletId);
        QCOMPARE(std::get<PinRef>(model.wires[0].endB).pinId, h.connectors[0].pins[1].id);
        QCOMPARE(model.freeEnds.size(), size_t(2)); // the eyelet stays where it was
        QCOMPARE(eyelet->pos(), QPointF(500, 100));
        // The wire attaches at the tip of the eyelet's lead facing the connector (the left one):
        // x = 500 - (12 + 14) = 474.
        QCOMPARE(f.scene()->endAnchor(model.wires[0], false).point, QPointF(474, 100));

        // Pin 3 dragged onto the eyelet: a second wire on it.
        const QPoint pin3 = f.toView(a->mapToScene(a->circuitCellRect(2).center()));
        const QPoint onEyelet = f.toView(eyelet->scenePos());
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, pin3);
        QTest::mouseMove(f.view.viewport(), pin3 + QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), onEyelet);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, onEyelet);
        QCOMPARE(model.wires.size(), size_t(2));
        QCOMPARE(std::get<FreeEndRef>(model.wires[1].endB).freeEndId, eyeletId);

        f.document.undoStack()->undo();
        f.document.undoStack()->undo();
        QVERIFY(model.wires.empty());
    }

    void wireBendsAreRounded()
    {
        /* Route (0,0) -> (40,0) -> (40,100) -> (100,100), radius 12.
           Corner (40,0): legs 40 and 100 -> r = min(12, 20, 50) = 12: line to (28,0),
           curve via (40,0) to (40,12). Corner (40,100): legs 100 and 60 -> r = 12: line to
           (40,88), curve via (40,100) to (52,100). Then line to (100,100). */
        const QPainterPath path = WireItem::roundedPath(
            {QPointF(0, 0), QPointF(40, 0), QPointF(40, 100), QPointF(100, 100)}, 12);
        // Qt stores each quadratic curve as a cubic: 3 elements (2 controls, then the end).
        // move, line, curve (3), line, curve (3), line = 10 elements.
        QCOMPARE(path.elementCount(), 10);
        QCOMPARE(QPointF(path.elementAt(1)), QPointF(28, 0));
        QCOMPARE(QPointF(path.elementAt(4)), QPointF(40, 12));
        QCOMPARE(QPointF(path.elementAt(5)), QPointF(40, 88));
        QCOMPARE(QPointF(path.elementAt(8)), QPointF(52, 100));
        QCOMPARE(QPointF(path.elementAt(9)), QPointF(100, 100));

        /* A short leg limits the radius: (0,0) -> (10,0) -> (10,6) -> (30,6).
           Corner (10,0): legs 10 and 6 -> r = min(12, 5, 3) = 3: line to (7,0), to (10,3).
           Corner (10,6): legs 6 and 20 -> r = min(12, 3, 10) = 3: already at (10,3), so no
           line, curve to (13,6). Elements: move, line, curve (3), curve (3), line = 9. */
        const QPainterPath tight =
            WireItem::roundedPath({QPointF(0, 0), QPointF(10, 0), QPointF(10, 6), QPointF(30, 6)}, 12);
        QCOMPARE(tight.elementCount(), 9);
        QCOMPARE(QPointF(tight.elementAt(1)), QPointF(7, 0));
        QCOMPARE(QPointF(tight.elementAt(4)), QPointF(10, 3));
        QCOMPARE(QPointF(tight.elementAt(7)), QPointF(13, 6));
        QCOMPARE(QPointF(tight.elementAt(8)), QPointF(30, 6));

        // Repeated and collinear points add no corner: a straight wire stays a line.
        const QPainterPath straight =
            WireItem::roundedPath({QPointF(0, 0), QPointF(0, 0), QPointF(50, 0), QPointF(100, 0)}, 12);
        QCOMPARE(straight.elementCount(), 3); // move, line to (50,0), line to (100,0)
        QCOMPARE(QPointF(straight.elementAt(2)), QPointF(100, 0));
    }

    void schematicNotes()
    {
        Fixture f;
        f.reset(ecuHarness(4, 1, 4));
        HarnessModel *model = f.model();
        auto editor = [&f]() -> QLineEdit * {
            for (auto *e : f.view.viewport()->findChildren<QLineEdit *>()) {
                if (e->isVisible())
                    return e;
            }
            return nullptr;
        };

        f.view.addNote();
        QTest::keyClicks(editor(), u"Check pinout"_s);
        QTest::keyClick(editor(), Qt::Key_Return);
        QCOMPARE(model->harness().notes.size(), size_t(1));
        const Note note = model->harness().notes.front();
        QCOMPARE(note.canvas, NoteCanvas::Schematic);
        QVERIFY(f.scene()->noteItem(note.id));
        QVERIFY(f.selection.selection().notes.contains(note.id));

        // Double-click edits it.
        f.view.centerOn(note.position);
        const QPoint at = f.toView(note.position + QPointF(10, 6));
        QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, {}, at);
        QVERIFY(editor());
        editor()->setText(u"Check pinout rev B"_s);
        QTest::keyClick(editor(), Qt::Key_Return);
        QCOMPARE(model->find<Note>(note.id)->text, u"Check pinout rev B"_s);

        // Dragging moves it by whole grid steps, as one undo step.
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, at);
        QTest::mouseMove(f.view.viewport(), at + QPoint(10, 0));
        QTest::mouseMove(f.view.viewport(), at + QPoint(30, 20));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, at + QPoint(30, 20));
        QCOMPARE(model->find<Note>(note.id)->position, note.position + QPointF(30, 20));
        f.document.undoStack()->undo();
        QCOMPARE(model->find<Note>(note.id)->position, note.position);

        // Delete removes it with the selection; undo restores it. The layout scene of the
        // same document ignores schematic notes.
        Selection s;
        s.notes.insert(note.id);
        f.selection.setSelection(s);
        f.view.deleteSelection();
        QVERIFY(model->harness().notes.empty());
        f.document.undoStack()->undo();
        QCOMPARE(model->harness().notes.size(), size_t(1));
        QString error;
        QVERIFY2(io::deserializeHarness(io::serializeHarness(model->harness()), &error), qPrintable(error));
    }

    void sceneMirrorsModel()
    {
        Fixture f;
        f.reset(ecuHarness(12, 2, 6));
        const Harness &h = f.model()->harness();
        for (const Connector &c : h.connectors)
            QVERIFY(f.scene()->connectorItem(c.id));
        for (const Wire &w : h.wires)
            QVERIFY(f.scene()->wireItem(w.id));
        // Removing a wire through a command removes its item; undo restores it.
        const QString wireId = h.wires[0].id;
        f.document.undoStack()->push(removeItems(f.model(), {}, {}, {}, {wireId}).release());
        QVERIFY(!f.scene()->wireItem(wireId));
        f.document.undoStack()->undo();
        QVERIFY(f.scene()->wireItem(wireId));
    }

    void plusMinusButtonsChangePinCount()
    {
        Fixture f;
        Harness h;
        h.connectors.push_back(makeConnector(u"J1"_s, 1));
        h.connectors[0].pins[0].label = u"A8"_s;
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{};
        f.reset(h);
        const QString id = h.connectors[0].id;
        ConnectorItem *item = f.scene()->connectorItem(id);
        const QPointF plus = item->mapToScene(QPointF(ConnectorItem::kWidth - 17, 23));
        const QPointF minus = item->mapToScene(QPointF(ConnectorItem::kWidth - 83, 23));

        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.toView(plus));
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.toView(plus));
        const Connector *c = f.model()->find<Connector>(id);
        QCOMPARE(c->pins.size(), size_t(3));
        QCOMPARE(c->pins[1].label, u"A9"_s); // auto-increment
        QCOMPARE(c->pins[2].label, u"A10"_s);

        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.toView(minus));
        QCOMPARE(f.model()->find<Connector>(id)->pins.size(), size_t(2));
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->find<Connector>(id)->pins.size(), size_t(3));
    }

    void dragFromPinToPinCreatesWire()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 3), makeConnector(u"B"_s, 3)};
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        h.schematic.placements[h.connectors[1].id] = SchematicPlacement{QPointF(500, 0), false, 0, {}};
        f.reset(h);
        ConnectorItem *a = f.scene()->connectorItem(h.connectors[0].id);
        ConnectorItem *b = f.scene()->connectorItem(h.connectors[1].id);
        const QPoint from = f.toView(a->mapToScene(a->circuitCellRect(1).center()));
        const QPoint to = f.toView(b->mapToScene(b->circuitCellRect(2).center()));

        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(f.view.viewport(), from + QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), to);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, to);

        const auto &wires = f.model()->harness().wires;
        QCOMPARE(wires.size(), size_t(1));
        QCOMPARE(std::get<PinRef>(wires[0].endA).pinId, h.connectors[0].pins[1].id);
        QCOMPARE(std::get<PinRef>(wires[0].endB).pinId, h.connectors[1].pins[2].id);
        QCOMPARE(wires[0].name, h.connectors[0].pins[1].circuitText); // named after the pin
        QCOMPARE(wires[0].baseColor, u"BLK"_s);                        // default style
        QVERIFY(f.selection.selection().wires.contains(wires[0].id));

        // Drag to empty space: flying lead with a placement where it was dropped.
        const QPoint empty = f.toView(QPointF(300, 400));
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(f.view.viewport(), from + QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), empty);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, empty);
        QCOMPARE(f.model()->harness().wires.size(), size_t(2));
        const Wire &lead = f.model()->harness().wires[1];
        const QString leadId = std::get<FreeEndRef>(lead.endB).freeEndId;
        QCOMPARE(f.model()->find<FreeEnd>(leadId)->kind, FreeEndKind::FlyingLead);
        QCOMPARE(f.model()->placement(leadId)->position, QPointF(300, 400));
        // One undo removes the wire and its new flying lead.
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->harness().wires.size(), size_t(1));
        QVERIFY(f.model()->harness().freeEnds.empty());
        f.document.undoStack()->redo();

        // A click without dragging only selects.
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, from);
        QCOMPARE(f.model()->harness().wires.size(), size_t(2));
    }

    void dragFromLeadCreatesWireAndCopiesCircuit()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 3), makeConnector(u"B"_s, 3)};
        for (Pin &p : h.connectors[1].pins)
            p.circuitText.clear();
        Splice splice{newId(), u"S1"_s, {}, {}};
        h.splices.push_back(splice);
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        h.schematic.placements[h.connectors[1].id] = SchematicPlacement{QPointF(500, 0), false, 0, {}};
        h.schematic.placements[splice.id] = SchematicPlacement{QPointF(400, 300), false, 0, {}};
        f.reset(h);
        ConnectorItem *a = f.scene()->connectorItem(h.connectors[0].id);
        ConnectorItem *b = f.scene()->connectorItem(h.connectors[1].id);
        // Middle of A's right lead on row 1, middle of B's left lead on row 2.
        const qreal half = ConnectorItem::kStubLength / 2;
        const QPoint from = f.toView(a->mapToScene(QPointF(ConnectorItem::kWidth + half, a->rowRect(1).center().y())));
        const QPoint to = f.toView(b->mapToScene(QPointF(-half, b->rowRect(2).center().y())));
        QCOMPARE(a->hitTest(a->mapFromScene(f.view.mapToScene(from))).part, ConnectorItem::Part::PinStub);

        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(f.view.viewport(), from + QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), to);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, to);

        const auto &wires = f.model()->harness().wires;
        QCOMPARE(wires.size(), size_t(1));
        QCOMPARE(std::get<PinRef>(wires[0].endA).pinId, h.connectors[0].pins[1].id);
        QCOMPARE(std::get<PinRef>(wires[0].endB).pinId, h.connectors[1].pins[2].id);
        // "A circuit 2" copied to the empty pin B:3, and the wire is named after it.
        const QString circuit = h.connectors[0].pins[1].circuitText;
        QCOMPARE(f.model()->find<Connector>(h.connectors[1].id)->pins[2].circuitText, circuit);
        QCOMPARE(wires[0].name, circuit);
        // The wire attaches at the tips of the leads.
        const Anchor tip = f.scene()->endAnchor(wires[0], false);
        QCOMPARE(tip.point, a->mapToScene(QPointF(ConnectorItem::kWidth + ConnectorItem::kStubLength,
                                                  a->rowRect(1).center().y())));

        // One undo step removes the wire and the copied text.
        f.document.undoStack()->undo();
        QVERIFY(f.model()->harness().wires.empty());
        QVERIFY(f.model()->find<Connector>(h.connectors[1].id)->pins[2].circuitText.isEmpty());

        // A splice's lead starts a wire in the Select tool; its dot still moves it.
        SpliceItem *s = f.scene()->spliceItem(splice.id);
        const QPoint lead = f.toView(s->mapToScene(QPointF(SpliceItem::kRadius + 1 + half, 0)));
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, lead);
        QTest::mouseMove(f.view.viewport(), lead + QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), to);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(f.model()->harness().wires.size(), size_t(1));
        QCOMPARE(std::get<SpliceRef>(f.model()->harness().wires[0].endA).spliceId, splice.id);

        f.selection.setSelection({}); // else the dot grabs the selected wire's end
        const QPoint dot = f.toView(s->scenePos());
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, dot);
        QTest::mouseMove(f.view.viewport(), dot + QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), dot + QPoint(100, 0));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, dot + QPoint(100, 0));
        QCOMPARE(f.model()->harness().wires.size(), size_t(1));
        QVERIFY(f.model()->placement(splice.id)->position.x() > 400);
    }

    void circuitTextCopiesOnceAndStaysEditable()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 1), makeConnector(u"B"_s, 1)};
        h.connectors[0].pins[0].circuitText.clear();
        h.connectors[1].pins[0].circuitText.clear();
        Wire w;
        w.id = newId();
        w.endA = PinRef{h.connectors[0].id, h.connectors[0].pins[0].id};
        w.endB = PinRef{h.connectors[1].id, h.connectors[1].pins[0].id};
        h.wires.push_back(w);
        f.reset(h);
        QUndoStack *stack = f.document.undoStack();
        auto circuitOf = [&](int c) { return f.model()->harness().connectors[size_t(c)].pins[0].circuitText; };

        // First text on A:1 -> the empty B:1 and the unnamed wire take it.
        Connector a = h.connectors[0];
        a.pins[0].circuitText = u"IGN"_s;
        stack->push(updateConnector(f.model(), a, u"Edit"_s).release());
        QCOMPARE(circuitOf(1), u"IGN"_s);
        QCOMPARE(f.model()->harness().wires[0].name, u"IGN"_s);

        // Renaming either side afterwards is not mirrored.
        a.pins[0].circuitText = u"IGN SW"_s;
        stack->push(updateConnector(f.model(), a, u"Edit"_s).release());
        QCOMPARE(circuitOf(0), u"IGN SW"_s);
        QCOMPARE(circuitOf(1), u"IGN"_s);
        Connector b = *f.model()->find<Connector>(h.connectors[1].id);
        b.pins[0].circuitText = u"IGN B"_s;
        stack->push(updateConnector(f.model(), b, u"Edit"_s).release());
        QCOMPARE(circuitOf(0), u"IGN SW"_s);
        QCOMPARE(f.model()->harness().wires[0].name, u"IGN"_s);

        // No change -> no command.
        QVERIFY(!updateConnector(f.model(), b, u"Edit"_s));

        stack->undo();
        stack->undo();
        stack->undo();
        QVERIFY(f.model()->harness() == h);
    }

    void inlineEditingMovesDown()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 3)};
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{};
        f.reset(h);
        ConnectorItem *item = f.scene()->connectorItem(h.connectors[0].id);
        QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, {},
                           f.toView(item->mapToScene(item->labelCellRect(0).center())));
        auto *editor = f.view.viewport()->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(u"X1"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::qWait(1);
        QCOMPARE(f.model()->find<Connector>(h.connectors[0].id)->pins[0].label, u"X1"_s);
        // Editor moved to row 2 of the same column.
        QLineEdit *next = nullptr;
        for (auto *e : f.view.viewport()->findChildren<QLineEdit *>()) {
            if (e->isVisible())
                next = e;
        }
        QVERIFY(next);
        QCOMPARE(next->text(), h.connectors[0].pins[1].label);
        QTest::keyClick(next, Qt::Key_Escape);
    }

    void deleteConnectorRemovesWiresAndUndoRestoresOrder()
    {
        Fixture f;
        f.reset(ecuHarness(8, 2, 4));
        const Harness before = f.model()->harness();
        const QString j1 = before.connectors[1].id;
        f.document.undoStack()->push(removeItems(f.model(), {j1}, {}, {}, {}).release());
        QCOMPARE(f.model()->harness().connectors.size(), size_t(2));
        QCOMPARE(f.model()->harness().wires.size(), size_t(4));
        f.document.undoStack()->undo();
        QVERIFY(f.model()->harness() == before);
        f.document.undoStack()->redo();
        f.document.undoStack()->undo();
        QVERIFY(f.model()->harness() == before);
    }

    void shrinkingPinsRemovesTheirWires()
    {
        Fixture f;
        f.reset(ecuHarness(8, 2, 4));
        const Harness before = f.model()->harness();
        const Connector &ecu = before.connectors[0];
        f.document.undoStack()->push(setConnectorPins(f.model(), ecu.id, resizedPins(ecu.pins, 5), u"x"_s).release());
        QCOMPARE(f.model()->find<Connector>(ecu.id)->pins.size(), size_t(5));
        QCOMPARE(f.model()->harness().wires.size(), size_t(5));
        f.document.undoStack()->undo();
        QVERIFY(f.model()->harness() == before);
    }

    void pasteCreatesIndependentCopy()
    {
        Fixture f;
        f.reset(ecuHarness(4, 1, 4));
        const Harness before = f.model()->harness();
        Harness fragment = extractFragment(before, {before.connectors[0].id, before.connectors[1].id}, {});
        QSet<QString> newIds;
        f.document.undoStack()->push(pasteFragment(f.model(), fragment, QPointF(40, 40), &newIds).release());
        QCOMPARE(f.model()->harness().connectors.size(), size_t(4));
        QCOMPARE(f.model()->harness().wires.size(), size_t(8));
        QCOMPARE(newIds.size(), 2);
        const SchematicPlacement *p = f.model()->placement(*newIds.begin());
        QVERIFY(p);
        f.document.undoStack()->undo();
        QVERIFY(f.model()->harness() == before);
    }

    // Sheet edits keep every item on the sheet it was on, including items with an empty
    // sheet id (first sheet) when another sheet becomes first, and undo restores the file.
    void sheetCommandsKeepItemsOnTheirSheets()
    {
        HarnessModel model;
        model.reset(placedSample());
        const Harness original = model.harness();
        QUndoStack stack;
        const Schematic &sch = model.harness().schematic;

        QString dash;
        stack.push(addSheet(&model, &dash).release());
        QCOMPARE(sch.sheets.size(), size_t(2));
        QCOMPARE(sheetDisplayName(sch, 1), u"Sheet 2"_s);
        QVERIFY(!renameSheet(&model, dash, u"  "_s)); // still unnamed: no change
        stack.push(renameSheet(&model, dash, u"  Dash "_s).release());
        QCOMPARE(sheetDisplayName(sch, 1), u"Dash"_s);

        stack.push(moveToSheet(&model, {u"sensor"_s, u"lamp"_s}, {u"note"_s}, kDefaultSheetId, dash).release());
        QCOMPARE(sch.sheetOf(u"sensor"_s), dash);
        QCOMPARE(sch.sheetOf(u"lamp"_s), dash);
        QCOMPARE(model.find<Note>(u"note"_s)->sheetId, dash);
        QCOMPARE(sch.sheetOf(u"ecu"_s), kDefaultSheetId);
        QCOMPARE(model.placement(u"sensor"_s)->position, QPointF(600, 0)); // position kept
        QVERIFY(!moveToSheet(&model, {u"sensor"_s}, {}, kDefaultSheetId, dash)); // not on sheet1 any more
        QVERIFY(!moveToSheet(&model, {u"ecu"_s}, {}, kDefaultSheetId, u"nope"_s)); // no such sheet

        // Dash becomes first: ecu, sp and fl (empty ids) must stay on sheet1.
        stack.push(moveSheet(&model, dash, 0).release());
        QCOMPARE(sch.sheets[0].id, dash);
        QCOMPARE(sch.sheetOf(u"ecu"_s), kDefaultSheetId);
        QCOMPARE(model.placement(u"ecu"_s)->sheetId, kDefaultSheetId);
        QCOMPARE(sch.sheetOf(u"sensor"_s), dash);
        QVERIFY(!moveSheet(&model, dash, -5)); // clamped to 0: no change

        // Deleting the first sheet moves its items to the next one.
        stack.push(removeSheet(&model, dash).release());
        QCOMPARE(sch.sheets.size(), size_t(1));
        QCOMPARE(sch.sheetOf(u"sensor"_s), kDefaultSheetId);
        QCOMPARE(model.find<Note>(u"note"_s)->sheetId, kDefaultSheetId);
        QVERIFY(!removeSheet(&model, kDefaultSheetId)); // the last sheet stays

        // The result still loads, and undo returns to the original harness.
        QString error;
        QVERIFY2(io::deserializeHarness(io::serializeHarness(model.harness()), &error), qPrintable(error));
        while (stack.canUndo())
            stack.undo();
        QVERIFY(model.harness() == original);
    }

    void sceneShowsCurrentSheetWithOffSheetTags()
    {
        Fixture f;
        f.reset(placedSample());
        QString dash;
        f.document.undoStack()->push(addSheet(f.model(), &dash).release());
        f.document.undoStack()->push(moveToSheet(f.model(), {u"sensor"_s, u"lamp"_s}, {u"note"_s}, kDefaultSheetId, dash).release());
        SchematicScene *scene = f.scene();
        QCOMPARE(scene->currentSheet(), kDefaultSheetId);
        QVERIFY(scene->connectorItem(u"ecu"_s)->isVisible());
        QVERIFY(!scene->connectorItem(u"sensor"_s)->isVisible());
        QVERIFY(!scene->noteItem(u"note"_s)->isVisible());
        // w1 ecu-sensor crosses sheets; w2 ecu-splice and w3 splice-flying lead stay here.
        WireItem *w1 = scene->wireItem(u"w1"_s);
        QVERIFY(w1->isVisible() && w1->isOffSheet());
        QVERIFY(!w1->tagRect().isNull());
        QVERIFY(!scene->wireItem(u"w2"_s)->isOffSheet());
        QVERIFY(scene->wireItem(u"w3"_s)->isVisible());
        // The tag leads right from ecu's right-hand pin lead.
        QVERIFY(w1->tagRect().left() > scene->connectorItem(u"ecu"_s)->sceneBoundingRect().right());
        if (!imageDir().isEmpty()) {
            f.view.resetTransform();
            f.view.scale(1.5, 1.5);
            f.view.centerOn(QPointF(300, 100));
            f.view.grab().save(imageDir() + u"/schematic-off-sheet.png"_s);
        }

        scene->setCurrentSheet(dash);
        QVERIFY(!scene->connectorItem(u"ecu"_s)->isVisible());
        QVERIFY(scene->connectorItem(u"sensor"_s)->isVisible());
        QVERIFY(scene->noteItem(u"note"_s)->isVisible());
        QVERIFY(w1->isVisible() && w1->isOffSheet());
        QVERIFY(!scene->wireItem(u"w3"_s)->isVisible()); // both ends on sheet 1

        // Double-click target: the far end's sheet, wire selected.
        f.view.goToOtherSheet(u"w1"_s);
        QCOMPARE(scene->currentSheet(), kDefaultSheetId);
        QVERIFY(f.selection.selection().wires.contains(u"w1"_s));

        // Selecting something on another sheet (e.g. from the layout) shows that sheet.
        Selection s;
        s.connectors.insert(u"lamp"_s);
        f.selection.setSelection(s);
        QCOMPARE(scene->currentSheet(), dash);
        QVERIFY(scene->connectorItem(u"lamp"_s)->isSelected());

        // New items go on the sheet shown.
        f.view.addSplice();
        const QString added = f.model()->harness().splices.back().id;
        QCOMPARE(f.model()->harness().schematic.sheetOf(added), dash);

        // Renaming a sheet updates tags; removing it shows the remaining one.
        f.document.undoStack()->push(renameSheet(f.model(), kDefaultSheetId, u"Engine"_s).release());
        f.document.undoStack()->push(removeSheet(f.model(), dash).release());
        QCOMPARE(scene->currentSheet(), kDefaultSheetId);
        QVERIFY(scene->connectorItem(u"sensor"_s)->isVisible());
        QVERIFY(!w1->isOffSheet());
    }

    void moveSelectionToSheetFollowsTheItems()
    {
        Fixture f;
        f.reset(placedSample());
        QString dash;
        f.document.undoStack()->push(addSheet(f.model(), &dash).release());
        Selection s;
        s.connectors.insert(u"sensor"_s);
        f.selection.setSelection(s);
        f.view.moveSelectionToSheet(dash);
        QCOMPARE(f.scene()->currentSheet(), dash);
        QVERIFY(f.selection.selection().connectors.contains(u"sensor"_s));
        QVERIFY(f.scene()->connectorItem(u"sensor"_s)->isSelected());
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->harness().schematic.sheetOf(u"sensor"_s), kDefaultSheetId);
    }

    // Pasting onto a sheet the copied items are not on shows the same items there (one ECU,
    // one splice in the layout and BOM). Pasting where they are already shown copies them.
    void pasteOnAnotherSheetReusesItems()
    {
        HarnessModel model;
        Harness h = placedSample();
        h.schematic.sheets.push_back(SchematicSheet{u"coils"_s, {}, {}});
        model.reset(h);
        QUndoStack stack;
        const Harness fragment = extractFragment(model.harness(), {u"ecu"_s}, {u"sp"_s});
        QSet<QString> ids;
        stack.push(pasteFragment(&model, fragment, QPointF(40, 40), &ids, u"coils"_s).release());
        const Harness &after = model.harness();
        // fl comes along: copying a splice copies the flying leads wired to it.
        QCOMPARE(ids, (QSet<QString>{u"ecu"_s, u"sp"_s, u"fl"_s}));
        QCOMPARE(after.connectors.size(), size_t(3));
        QCOMPARE(after.splices.size(), size_t(1));
        QCOMPARE(after.freeEnds.size(), size_t(1));
        QCOMPARE(after.wires.size(), size_t(5)); // w2 ecu-sp exists already: not copied
        QCOMPARE(after.schematic.sheetsOf(u"ecu"_s), (std::vector<QString>{kDefaultSheetId, u"coils"_s}));
        QCOMPARE(after.schematic.positionOn(u"ecu"_s, u"coils"_s), std::optional(QPointF(40, 40)));
        QCOMPARE(after.schematic.positionOn(u"ecu"_s, kDefaultSheetId), std::optional(QPointF(0, 0)));
        // w2 keeps being drawn on sheet1, the first sheet both ends share.
        QCOMPARE(wireSheets(after, testing::wireById(after, u"w2"_s)).sheets, std::vector<QString>{kDefaultSheetId});

        // Same paste again: now on coils already, so a copy.
        ids.clear();
        stack.push(pasteFragment(&model, fragment, QPointF(80, 80), &ids, u"coils"_s).release());
        QCOMPARE(model.harness().connectors.size(), size_t(4));
        QCOMPARE(model.harness().wires.size(), size_t(7)); // copies of w2 and w3 between the copies
        const QString copiedWire = model.harness().wires.back().id;
        QCOMPARE(wireSheets(model.harness(), model.harness().wires.back()).sheets, std::vector<QString>{u"coils"_s});
        QVERIFY(model.placement(Schematic::wireSheetKey(copiedWire)));

        stack.undo();
        stack.undo();
        QVERIFY(model.harness() == h);
    }

    // The ECU and a ground splice on two sheets: each wire stays on the sheet it was drawn on,
    // and deleting a shared item on one sheet only removes it from that sheet.
    void sharedItemsKeepWiresOnTheirSheets()
    {
        Fixture f;
        Harness h = placedSample();
        h.schematic.sheets.push_back(SchematicSheet{u"coils"_s, u"Coils"_s, {}});
        // ecu and sp on both sheets; lamp only on coils.
        h.schematic.placements[u"ecu"_s].alsoOn[u"coils"_s] = QPointF(0, 0);
        h.schematic.placements[u"sp"_s].alsoOn[u"coils"_s] = QPointF(400, 200);
        h.schematic.placements[u"lamp"_s].sheetId = u"coils"_s;
        f.reset(h);
        SchematicScene *scene = f.scene();
        // Draw a new wire ecu pin 4 -> splice on coils by hand: it records coils.
        scene->setCurrentSheet(u"coils"_s);
        Wire w;
        w.id = u"wnew"_s;
        w.endA = PinRef{u"ecu"_s, u"ecu4"_s};
        w.endB = SpliceRef{u"sp"_s};
        SchematicPlacement drawn;
        drawn.sheetId = u"coils"_s;
        f.document.undoStack()->push(
            addWire(f.model(), w, {{Schematic::wireSheetKey(u"wnew"_s), drawn}}, u"Add wire"_s).release());
        QVERIFY(scene->wireItem(u"wnew"_s)->isVisible());
        QVERIFY(!scene->wireItem(u"w2"_s)->isVisible()); // ecu-sp drawn on sheet1
        QVERIFY(scene->wireItem(u"w5"_s)->isVisible());  // ecu-lamp: lamp only here
        QVERIFY(!scene->wireItem(u"w5"_s)->isOffSheet());
        QVERIFY(!scene->wireItem(u"w1"_s)->isVisible()); // sensor not on coils
        scene->setCurrentSheet(kDefaultSheetId);
        QVERIFY(!scene->wireItem(u"wnew"_s)->isVisible());
        QVERIFY(scene->wireItem(u"w2"_s)->isVisible());
        QVERIFY(!scene->wireItem(u"w5"_s)->isVisible()); // drawn in full on coils
        QVERIFY(scene->connectorItem(u"ecu"_s)->isVisible());

        // Delete the ECU on coils: it leaves coils, the connector and its wires stay.
        scene->setCurrentSheet(u"coils"_s);
        Selection s;
        s.connectors.insert(u"ecu"_s);
        f.selection.setSelection(s);
        f.view.deleteSelection();
        const Harness &after = f.model()->harness();
        QVERIFY(f.model()->find<Connector>(u"ecu"_s));
        QCOMPARE(after.wires.size(), size_t(6));
        QCOMPARE(after.schematic.sheetsOf(u"ecu"_s), (std::vector<QString>{kDefaultSheetId}));
        // wnew's ends now only share sheet1, so it is drawn there instead of on coils.
        QVERIFY(!scene->wireItem(u"wnew"_s)->isVisible());
        QCOMPARE(wireSheets(after, testing::wireById(after, u"wnew"_s)).sheets, std::vector<QString>{kDefaultSheetId});
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->harness().schematic.sheetsOf(u"ecu"_s).size(), size_t(2));

        // Moving an item on one sheet keeps its position on the other.
        scene->setCurrentSheet(u"coils"_s);
        SchematicPlacement moved = *f.model()->placement(u"ecu"_s);
        moved.setPositionOn(kDefaultSheetId, u"coils"_s, QPointF(-300, 0));
        f.document.undoStack()->push(new SetPlacementsCommand(f.model(), {{u"ecu"_s, moved}}, u"Move"_s));
        QCOMPARE(scene->connectorItem(u"ecu"_s)->pos(), QPointF(-300, 0));
        scene->setCurrentSheet(kDefaultSheetId);
        QCOMPARE(scene->connectorItem(u"ecu"_s)->pos(), QPointF(0, 0));

        // Add Existing Item: the sensor joins coils.
        scene->setCurrentSheet(u"coils"_s);
        f.view.showExistingItem(u"sensor"_s);
        QVERIFY(scene->connectorItem(u"sensor"_s)->isVisible());
        QCOMPARE(f.model()->harness().schematic.sheetsOf(u"sensor"_s).size(), size_t(2));
    }

    // Copy the ECU, S1 and the wire between them, paste onto an Ignition sheet: the same
    // three show there, the wire is still one wire, and Delete on Ignition only hides it there.
    void copiedWireShowsOnAnotherSheet()
    {
        Fixture f;
        f.reset(placedSample());
        QString ignition;
        f.document.undoStack()->push(addSheet(f.model(), &ignition).release());
        Selection s;
        s.connectors.insert(u"ecu"_s);
        s.splices.insert(u"sp"_s);
        s.wires.insert(u"w2"_s); // ecu pin 2 - S1
        f.selection.setSelection(s);
        f.view.copySelection();
        f.scene()->setCurrentSheet(ignition);
        f.view.paste();
        const Harness &h = f.model()->harness();
        QCOMPARE(h.connectors.size(), size_t(3));
        QCOMPARE(h.wires.size(), size_t(5));
        const WireSheets w2 = wireSheets(h, testing::wireById(h, u"w2"_s));
        QCOMPARE(w2.sheets, (std::vector<QString>{kDefaultSheetId, ignition}));
        QVERIFY(f.scene()->wireItem(u"w2"_s)->isVisible());
        QVERIFY(!f.scene()->wireItem(u"w2"_s)->isOffSheet());
        // w3 (S1 - flying lead) was not selected: the flying lead came along, its wire did not.
        QVERIFY(!wireSheets(h, testing::wireById(h, u"w3"_s)).drawnOn(ignition));
        QVERIFY(!f.scene()->wireItem(u"w3"_s)->isVisible());

        // A dragged route on Ignition does not move the wire on sheet1.
        SchematicPlacement route;
        route.sheetId = ignition;
        route.position = QPointF(250, 0);
        f.document.undoStack()->push(new SetPlacementsCommand(f.model(), {{u"w2"_s, route}}, u"Move wire"_s));
        QCOMPARE(f.scene()->wireItem(u"w2"_s)->runX(), 250.0);
        f.scene()->setCurrentSheet(kDefaultSheetId);
        QVERIFY(f.scene()->wireItem(u"w2"_s)->runX() != 250.0);

        // Delete the wire on Ignition: it stays, on sheet1 only.
        f.scene()->setCurrentSheet(ignition);
        Selection wire;
        wire.wires.insert(u"w2"_s);
        f.selection.setSelection(wire);
        f.view.deleteSelection();
        QVERIFY(f.model()->find<Wire>(u"w2"_s));
        QCOMPARE(wireSheets(f.model()->harness(), *f.model()->find<Wire>(u"w2"_s)).sheets,
                 std::vector<QString>{kDefaultSheetId});
        QVERIFY(!f.scene()->wireItem(u"w2"_s)->isVisible());
        // ...and Also Show on Sheet brings it back.
        f.scene()->setCurrentSheet(kDefaultSheetId);
        f.selection.setSelection(wire);
        f.view.showSelectionOnSheet(ignition);
        QVERIFY(wireSheets(f.model()->harness(), *f.model()->find<Wire>(u"w2"_s)).drawnOn(ignition));
        // Deleting it on its last sheet deletes the wire.
        f.scene()->setCurrentSheet(ignition);
        f.document.undoStack()->push(
            removeFromSheet(f.model(), {}, {u"w2"_s}, ignition).release());
        f.selection.setSelection(wire);
        f.scene()->setCurrentSheet(kDefaultSheetId);
        f.selection.setSelection(wire);
        f.view.deleteSelection();
        QVERIFY(!f.model()->find<Wire>(u"w2"_s));
    }

    void placementCommandsMoveItems()
    {
        Fixture f;
        f.reset(ecuHarness(4, 1, 4));
        const QString id = f.model()->harness().connectors[1].id;
        SchematicPlacement p = *f.model()->placement(id);
        p.position = QPointF(1000, 500);
        f.document.undoStack()->push(new SetPlacementsCommand(f.model(), {{id, p}}, u"Move"_s));
        QCOMPARE(f.scene()->connectorItem(id)->pos(), QPointF(1000, 500));
        f.document.undoStack()->undo();
        QCOMPARE(f.scene()->connectorItem(id)->pos(), QPointF(600, 0));
    }

    void draggingWireMovesItsVerticalRun()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 3), makeConnector(u"B"_s, 3)};
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        h.schematic.placements[h.connectors[1].id] = SchematicPlacement{QPointF(600, 0), false, 0, {}};
        Wire w;
        w.id = newId();
        w.endA = PinRef{h.connectors[0].id, h.connectors[0].pins[0].id};
        w.endB = PinRef{h.connectors[1].id, h.connectors[1].pins[2].id};
        h.wires.push_back(w);
        f.reset(h);
        WireItem *item = f.scene()->wireItem(w.id);
        QVERIFY(item);
        /* A's right lead tip: x = 300 + 14 = 314, route stub to 330.
           B's left lead tip: x = 600 - 14 = 586, route stub to 570.
           Default run: (330 + 570) / 2 = 450. */
        QCOMPARE(item->runX(), 450.0);

        // Grab the vertical run halfway between the pin rows and drag it 80 px right.
        const ConnectorItem *a = f.scene()->connectorItem(h.connectors[0].id);
        const ConnectorItem *b = f.scene()->connectorItem(h.connectors[1].id);
        const qreal midY = (a->mapToScene(a->rowRect(0).center()).y() + b->mapToScene(b->rowRect(2).center()).y()) / 2;
        const QPoint grab = f.toView(QPointF(450, midY));
        const QPoint drop = f.toView(QPointF(530, midY));
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, grab);
        QTest::mouseMove(f.view.viewport(), grab + QPoint(10, 0));
        QTest::mouseMove(f.view.viewport(), drop);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, drop);

        // 450 + 80 = 530; snapped to the 10 px grid it stays 530.
        const SchematicPlacement *route = f.model()->placement(w.id);
        QVERIFY(route);
        QCOMPARE(route->position.x(), 530.0);
        QCOMPARE(item->runX(), 530.0);
        QVERIFY(f.selection.selection().wires.contains(w.id));

        // The run stays where it was put when a connector moves.
        SchematicPlacement moved = *f.model()->placement(h.connectors[1].id);
        moved.position = QPointF(700, 0);
        f.document.undoStack()->push(new SetPlacementsCommand(f.model(), {{h.connectors[1].id, moved}}, u"Move"_s));
        QCOMPARE(item->runX(), 530.0);
        f.document.undoStack()->undo();

        // Undo goes back to the automatic run.
        f.document.undoStack()->undo();
        QVERIFY(!f.model()->placement(w.id));
        QCOMPARE(item->runX(), 450.0);
        f.document.undoStack()->redo();
        QCOMPARE(item->runX(), 530.0);

        // Deleting the wire drops its route; undo brings both back. The file still loads.
        f.document.undoStack()->push(removeItems(f.model(), {}, {}, {}, {w.id}).release());
        QVERIFY(!f.model()->placement(w.id));
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->placement(w.id)->position.x(), 530.0);
        QCOMPARE(f.scene()->wireItem(w.id)->runX(), 530.0);
        QString error;
        const auto loaded = io::deserializeHarness(io::serializeHarness(f.model()->harness()), &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->schematic.placements.at(w.id).position.x(), 530.0);

        // Copy/paste carries the route to the new wire id.
        Harness fragment = extractFragment(f.model()->harness(), {h.connectors[0].id, h.connectors[1].id}, {});
        QCOMPARE(fragment.schematic.placements.at(w.id).position.x(), 530.0);
        assignNewIds(fragment);
        QVERIFY(!fragment.schematic.placements.contains(w.id));
        QCOMPARE(fragment.schematic.placements.at(fragment.wires[0].id).position.x(), 530.0);
    }

    // M2 acceptance: a 300-pin ECU wired to smaller connectors with striped wires
    // saves, reopens identically, and stays editable.
    void ecuHarnessSavesAndReopens()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"ecu.tbh"_s);
        Fixture f;
        Harness h = ecuHarness(300, 30, 10);
        h.schematic.placements[h.connectors[0].id].maxVisibleRows = 40;
        f.reset(h);
        QString error;
        QVERIFY2(f.document.saveAs(path, &error), qPrintable(error));

        Document reopened;
        QVERIFY2(reopened.open(path, &error), qPrintable(error));
        Harness loaded = reopened.model()->harness();
        loaded.createdAt = h.createdAt; // stamped on first save
        QVERIFY(loaded == h);

        SelectionModel selection(reopened.model());
        SchematicView view(&reopened, &selection);
        QCOMPARE(view.schematicScene()->connectorItem(h.connectors[0].id)->visibleRowCount(), 40);
        QVERIFY(changePinCount(nullptr, &reopened, h.connectors[1].id, 12));
        QCOMPARE(reopened.model()->find<Connector>(h.connectors[1].id)->pins.size(), size_t(12));
        QVERIFY(reopened.isModified());
    }

    void scrollModeAnchorsHiddenRows()
    {
        Connector c = makeConnector(u"BIG"_s, 100);
        ConnectorItem item(c);
        item.setDisplay(false, 10);
        QVERIFY(item.isScrollMode());
        QCOMPARE(item.visibleRowCount(), 10);
        QVERIFY(item.rowRect(50).isNull());
        item.ensureRowVisible(50);
        QVERIFY(!item.rowRect(50).isNull());
        QCOMPARE(item.firstVisibleRow(), 41);
        // Hidden rows anchor on the box edge, inside its height.
        const Anchor anchor = item.pinAnchor(0, QPointF(1000, 0));
        QCOMPARE(anchor.direction, 1);
        QVERIFY(anchor.point.y() <= item.boundingRect().bottom());
    }

    // Section 10 performance sanity budget (documented in docs/PROJECT_PLAN.md and
    // README): with a 999-pin connector and 500 striped wires, building the scene must
    // take < 1500 ms and one full-view render < 1000 ms, even in a debug build.
    void largeConnectorPerformance()
    {
        Harness h = ecuHarness(999, 50, 10);
        QCOMPARE(h.wires.size(), size_t(500));
        Document document;
        SelectionModel selection(document.model());
        QElapsedTimer timer;
        timer.start();
        document.model()->reset(h);
        SchematicView view(&document, &selection);
        const qint64 buildMs = timer.elapsed();
        view.resize(1600, 1000);
        view.resetTransform();
        view.centerOn(QPointF(450, 900));

        QImage image(view.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        timer.restart();
        {
            QPainter painter(&image);
            view.render(&painter);
        }
        const qint64 renderMs = timer.elapsed();
        view.scale(0.1, 0.1); // zoomed out: everything visible
        timer.restart();
        {
            QPainter painter(&image);
            view.render(&painter);
        }
        const qint64 zoomedOutMs = timer.elapsed();
        qInfo("build %lld ms, render %lld ms, zoomed-out render %lld ms", buildMs, renderMs, zoomedOutMs);
        QVERIFY2(buildMs < 1500, qPrintable(QString::number(buildMs)));
        QVERIFY2(renderMs < 1000, qPrintable(QString::number(renderMs)));
        QVERIFY2(zoomedOutMs < 1000, qPrintable(QString::number(zoomedOutMs)));
    }

    // Writes screenshots for manual review when TBH_SCREENSHOT_DIR is set.
    void screenshots()
    {
        if (imageDir().isEmpty())
            QSKIP("TBH_SCREENSHOT_DIR not set");
        Fixture f;
        Harness h = ecuHarness(300, 6, 8);
        h.schematic.placements[h.connectors[0].id].maxVisibleRows = 40;
        f.reset(h);
        f.view.resize(1400, 900);
        f.view.zoomToFit();
        f.view.grab().save(imageDir() + u"/schematic-fit.png"_s);
        f.view.resetTransform();
        f.view.centerOn(QPointF(450, 300));
        f.view.grab().save(imageDir() + u"/schematic-100.png"_s);

        // One free end of each kind, wired to a connector, plus an unwired eyelet.
        Harness ends;
        ends.connectors = {makeConnector(u"J1"_s, 4)};
        ends.schematic.placements[ends.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        const FreeEndKind kinds[] = {FreeEndKind::FlyingLead, FreeEndKind::Eyelet, FreeEndKind::RingTerminal,
                                     FreeEndKind::Bare};
        for (int i = 0; i < 4; ++i) {
            FreeEnd end{newId(), kinds[i], i == 1 ? u"GND"_s : QString(), {}, {}};
            ends.schematic.placements[end.id] = SchematicPlacement{QPointF(450, 40 + i * 60), false, 0, {}};
            Wire w;
            w.id = newId();
            w.endA = PinRef{ends.connectors[0].id, ends.connectors[0].pins[size_t(i)].id};
            w.endB = FreeEndRef{end.id};
            w.baseColor = u"RED"_s;
            w.gauge = {20, GaugeSystem::Awg};
            ends.wires.push_back(w);
            ends.freeEnds.push_back(end);
        }
        FreeEnd loose{newId(), FreeEndKind::Eyelet, {}, {}, {}};
        ends.schematic.placements[loose.id] = SchematicPlacement{QPointF(600, 100), false, 0, {}};
        ends.freeEnds.push_back(loose);
        f.reset(ends);
        f.view.centerOn(QPointF(350, 120));
        f.view.scale(2, 2);
        f.view.grab().save(imageDir() + u"/schematic-free-ends.png"_s);
        f.view.resetTransform();

        // Whole window, dark theme, with a wire and then a connector selected.
        const QString file = imageDir() + u"/sample.tbh"_s;
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        Theme theme;
        theme.setMode(Theme::Mode::Dark);
        MainWindow window(&theme);
        window.resize(1600, 950);
        window.show();
        QVERIFY(window.openFile(file));
        QTest::qWait(20);
        auto *view = window.findChild<SchematicView *>();
        auto *selection = window.findChild<SelectionModel *>();
        Selection wire;
        wire.wires.insert(h.wires[3].id);
        selection->setSelection(wire);
        QTest::qWait(20);
        window.grab().save(imageDir() + u"/window-wire.png"_s);
        Selection connector;
        connector.connectors.insert(h.connectors[0].id);
        selection->setSelection(connector);
        view->resetTransform();
        view->centerOn(QPointF(450, 300));
        QTest::qWait(20);
        window.grab().save(imageDir() + u"/window-connector.png"_s);

        // Sheet tabs: two sheets, light theme, a wire crossing between them.
        Harness sheets = placedSample();
        sheets.schematic.sheets = {SchematicSheet{kDefaultSheetId, u"Engine"_s, {}}, SchematicSheet{u"dash"_s, u"Dash"_s, {}}};
        sheets.schematic.placements[u"sensor"_s].sheetId = u"dash"_s;
        sheets.schematic.placements[u"lamp"_s].sheetId = u"dash"_s;
        QVERIFY(io::writeTbhFile(file, sheets, nullptr));
        theme.setMode(Theme::Mode::Light);
        QVERIFY(window.openFile(file));
        selection->setSelection({});
        view->resetTransform();
        view->scale(1.5, 1.5);
        view->centerOn(QPointF(300, 150));
        QTest::qWait(20);
        window.grab().save(imageDir() + u"/window-sheets.png"_s);
        theme.setMode(Theme::Mode::System);
    }
};

QTEST_MAIN(TestSchematic)
#include "tst_schematic.moc"
