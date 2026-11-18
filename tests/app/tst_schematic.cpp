// SPDX-License-Identifier: GPL-3.0-or-later
#include "../core/SampleHarness.h"
#include "Autosave.h"
#include "calc/Highlight.h"
#include "Document.h"
#include "SelectionModel.h"
#include "Preferences.h"
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

    // A harness opened while the view is hidden is fitted once the view shows; switching
    // sheets fits that sheet's items only; after the user zooms, a resize leaves the view.
    void opensFittedToSheet()
    {
        Document document;
        SelectionModel selection(document.model());
        SchematicView view(&document, &selection);
        view.resize(800, 600);
        // ecu (0, 0), sensor (600, 0), sp, fl, note on the first sheet; lamp far off at
        // (3000, 3000) on sheet "far".
        Harness h = placedSample();
        h.schematic.sheets = {SchematicSheet{kDefaultSheetId, u"Engine"_s, {}},
                              SchematicSheet{u"far"_s, u"Far"_s, {}}};
        h.schematic.placements[u"lamp"_s].position = QPointF(3000, 3000);
        h.schematic.placements[u"lamp"_s].sheetId = u"far"_s;
        document.model()->reset(h);
        QTest::qWait(1);
        view.show();
        QTest::qWait(20);
        const auto shown = [&] { return view.mapToScene(view.viewport()->rect()).boundingRect(); };
        QVERIFY(shown().contains(QPointF(0, 0)));
        QVERIFY(shown().contains(QPointF(600, 0)));
        // Fitted to the first sheet (about 700 wide in 800 px), not out to the lamp at 3000:
        // that would take a scale near 800 / 3100 = 0.26.
        QVERIFY(view.transform().m11() > 0.5);
        QVERIFY(!shown().contains(QPointF(3000, 3000)));

        view.schematicScene()->setCurrentSheet(u"far"_s);
        QTest::qWait(20);
        QVERIFY(shown().contains(QPointF(3000, 3000)));
        QVERIFY(!shown().contains(QPointF(0, 0)));

        // A resize refits until the user zooms; after that it is left alone.
        view.resize(900, 700);
        QTest::qWait(20);
        QVERIFY(shown().contains(QPointF(3000, 3000)));
        view.zoomIn();
        const QTransform zoomed = view.transform();
        view.resize(700, 500);
        QTest::qWait(20);
        QCOMPARE(view.transform(), zoomed);
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

    void circuitTextCopiesAndRenamesFollow()
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

        // Renaming A afterwards follows: B:1 and the wire still had "IGN", so both take
        // "IGN SW", in the same undo step.
        a.pins[0].circuitText = u"IGN SW"_s;
        stack->push(updateConnector(f.model(), a, u"Edit"_s).release());
        QCOMPARE(circuitOf(0), u"IGN SW"_s);
        QCOMPARE(circuitOf(1), u"IGN SW"_s);
        QCOMPARE(f.model()->harness().wires[0].name, u"IGN SW"_s);

        // Clearing B:1 and typing new text there gives it its own text: clearing renames
        // nothing, and A:1 is not empty, so nothing is filled.
        Connector b = *f.model()->find<Connector>(h.connectors[1].id);
        b.pins[0].circuitText.clear();
        stack->push(updateConnector(f.model(), b, u"Edit"_s).release());
        b.pins[0].circuitText = u"IGN B"_s;
        stack->push(updateConnector(f.model(), b, u"Edit"_s).release());
        QCOMPARE(circuitOf(0), u"IGN SW"_s);
        QCOMPARE(circuitOf(1), u"IGN B"_s);
        QCOMPARE(f.model()->harness().wires[0].name, u"IGN SW"_s);

        // No change -> no command.
        QVERIFY(!updateConnector(f.model(), b, u"Edit"_s));

        for (int i = 0; i < 4; ++i)
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

    void outlineMovesWhatIsInsideIt()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"Wheel"_s, 2), makeConnector(u"ECU"_s, 2)};
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}, {}};
        h.schematic.placements[h.connectors[1].id] = SchematicPlacement{QPointF(600, 0), false, 0, {}, {}};
        f.reset(h);
        const QString wheel = h.connectors[0].id;
        const QString ecu = h.connectors[1].id;

        // Outline around the selected Wheel connector, then its title.
        Selection s;
        s.connectors.insert(wheel);
        f.selection.setSelection(s);
        f.view.addOutline();
        QCOMPARE(f.model()->harness().notes.size(), size_t(1));
        const Note outline = f.model()->harness().notes[0];
        QVERIFY(outline.frameSize);
        const QRectF wheelRect = f.scene()->connectorItem(wheel)->sceneBoundingRect();
        QVERIFY(QRectF(outline.position, *outline.frameSize).contains(wheelRect));
        QVERIFY(!QRectF(outline.position, *outline.frameSize)
                     .intersects(f.scene()->connectorItem(ecu)->sceneBoundingRect()));
        QLineEdit *editor = nullptr;
        for (auto *e : f.view.viewport()->findChildren<QLineEdit *>()) {
            if (e->isVisible())
                editor = e;
        }
        QVERIFY(editor);
        editor->setText(u"Steering wheel"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::qWait(1);
        QCOMPARE(f.model()->harness().notes[0].text, u"Steering wheel"_s);
        // Clearing an outline's title keeps the outline.
        auto clear = setNoteText(f.model(), outline.id, {});
        QVERIFY(clear);
        f.document.undoStack()->push(clear.release());
        QCOMPARE(f.model()->harness().notes.size(), size_t(1));
        QVERIFY(f.model()->harness().notes[0].text.isEmpty());
        f.document.undoStack()->undo();

        // Drag the outline by its left edge 100 right: the Wheel connector inside follows,
        // the ECU outside stays; one undo step puts both back.
        const QPointF edge = outline.position + QPointF(0, outline.frameSize->height() / 2);
        const QPointF wheelBefore = f.model()->placement(wheel)->position;
        const QPointF ecuBefore = f.model()->placement(ecu)->position;
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, f.toView(edge));
        QTest::mouseMove(f.view.viewport(), f.toView(edge + QPointF(30, 0)));
        QTest::mouseMove(f.view.viewport(), f.toView(edge + QPointF(100, 0)));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, f.toView(edge + QPointF(100, 0)));
        QCOMPARE(f.model()->harness().notes[0].position, outline.position + QPointF(100, 0));
        QCOMPARE(f.model()->placement(wheel)->position, wheelBefore + QPointF(100, 0));
        QCOMPARE(f.model()->placement(ecu)->position, ecuBefore);
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->harness().notes[0].position, outline.position);
        QCOMPARE(f.model()->placement(wheel)->position, wheelBefore);

        // Resize from the bottom-right handle (the outline is selected): 60 wider, 40 taller.
        Selection only;
        only.notes.insert(outline.id);
        f.selection.setSelection(only);
        const QSizeF size = *f.model()->harness().notes[0].frameSize;
        const QPointF corner = outline.position + QPointF(size.width(), size.height());
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, f.toView(corner));
        QTest::mouseMove(f.view.viewport(), f.toView(corner + QPointF(30, 20)));
        QTest::mouseMove(f.view.viewport(), f.toView(corner + QPointF(60, 40)));
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, f.toView(corner + QPointF(60, 40)));
        QCOMPARE(*f.model()->harness().notes[0].frameSize, size + QSizeF(60, 40));
        QCOMPARE(f.model()->harness().notes[0].position, outline.position); // only the size changed
    }

    void addGroundPlacesAGroundEyelet()
    {
        Fixture f;
        f.reset(Harness{});
        f.view.addFreeEnd(FreeEndKind::Eyelet, SchematicSymbol::Ground, u"GND"_s);
        QCOMPARE(f.model()->harness().freeEnds.size(), size_t(1));
        const FreeEnd end = f.model()->harness().freeEnds[0];
        QCOMPARE(end.kind, FreeEndKind::Eyelet);
        QCOMPARE(end.symbol, SchematicSymbol::Ground);
        QCOMPARE(end.label, u"GND"_s);
        // The symbol hangs below the eyelet and can be grabbed there.
        FreeEndItem *item = f.scene()->freeEndItem(end.id);
        QVERIFY(item->shape().contains(QPointF(0, FreeEndItem::kHalfWidth + 12)));
        FreeEnd plain = end;
        plain.symbol = SchematicSymbol::None;
        f.document.undoStack()->push(new UpdateEntityCommand<FreeEnd>(f.model(), plain, u"Edit"_s));
        QVERIFY(!item->shape().contains(QPointF(0, FreeEndItem::kHalfWidth + 12)));
    }

    void circuitTextSuggestionsAreOptional()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 2)};
        h.connectors[0].pins[0].circuitText = u"IGN"_s;
        h.connectors[0].pins[1].circuitText.clear();
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{};
        f.reset(h);
        ConnectorItem *item = f.scene()->connectorItem(h.connectors[0].id);
        auto openCircuitCell = [&] {
            QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, {},
                               f.toView(item->mapToScene(item->circuitCellRect(1).center())));
            QLineEdit *editor = nullptr;
            for (auto *e : f.view.viewport()->findChildren<QLineEdit *>()) {
                if (e->isVisible())
                    editor = e;
            }
            return editor;
        };

        // Off by default: no list.
        QVERIFY(!preferences::circuitSuggestions());
        QLineEdit *editor = openCircuitCell();
        QVERIFY(editor);
        QVERIFY(!editor->completer());
        QTest::keyClick(editor, Qt::Key_Escape);
        QTest::qWait(1);

        // On: the quick choices first, then the texts in the harness ("IGN"); "gnd" in the
        // defaults list is the same as "GND" and is offered once.
        preferences::setCircuitSuggestions(true);
        preferences::setCircuitSuggestionDefaults({u"12V+"_s, u"GND"_s, u"gnd"_s});
        QCOMPARE(preferences::circuitSuggestionsFor(f.model()->harness()),
                 (QStringList{u"12V+"_s, u"GND"_s, u"IGN"_s}));
        editor = openCircuitCell();
        QVERIFY(editor);
        QVERIFY(editor->completer());
        editor->setText(u"GND"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::qWait(1);
        QCOMPARE(f.model()->find<Connector>(h.connectors[0].id)->pins[1].circuitText, u"GND"_s);
        for (auto *e : f.view.viewport()->findChildren<QLineEdit *>())
            e->hide();
        preferences::setCircuitSuggestions(false);
        QSettings().remove(u"preferences/circuitSuggestionDefaults"_s);
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

    // Cutting a factory wire and splicing an add-on into it (ECU at x 0, SENSOR at x 600).
    void cutWireThenSpliceTheCutEnds()
    {
        Fixture f;
        Harness start = placedSample();
        start.wires[0].factory = true; // w1: ECU 1 to SENSOR 1
        start.wires[0].routeSegmentIds = {u"seg1"_s, u"seg2"_s};
        f.reset(start);
        const Harness before = f.model()->harness();
        auto loadable = [&] {
            QString error;
            QVERIFY2(io::deserializeHarness(io::serializeHarness(f.model()->harness()), &error), qPrintable(error));
        };

        f.view.cutWireAt(u"w1"_s, QPointF(300, 40), std::nullopt);
        const Harness &h = f.model()->harness();
        QCOMPARE(h.wires.size(), before.wires.size() + 1);
        const Wire &first = *f.model()->find<Wire>(u"w1"_s);
        const Wire &second = h.wires.back();
        QCOMPARE(first.endA, before.wires[0].endA);
        QCOMPARE(second.endB, before.wires[0].endB);
        QVERIFY(first.routeSegmentIds.empty()); // each piece routes on its own
        QVERIFY(second.factory);                // both pieces stay factory wires
        QVERIFY(!second.circuitId.isEmpty());
        QVERIFY(second.circuitId != first.circuitId);
        const QString cutA = std::get<FreeEndRef>(first.endB).freeEndId;
        const QString cutB = std::get<FreeEndRef>(second.endA).freeEndId;
        QCOMPARE(f.model()->find<FreeEnd>(cutA)->kind, FreeEndKind::Cut);
        QCOMPARE(f.model()->find<FreeEnd>(cutB)->kind, FreeEndKind::Cut);
        QCOMPARE(f.model()->find<FreeEnd>(cutA)->pairedCutId, cutB); // the two ends know each other
        QCOMPARE(f.model()->find<FreeEnd>(cutB)->pairedCutId, cutA);
        // End A's cut end is on end A's (ECU's) side of the cut.
        QVERIFY(f.model()->placement(cutA)->position.x() < 300);
        QVERIFY(f.model()->placement(cutB)->position.x() > 300);
        QCOMPARE(f.selection.selection().wires, (QSet<QString>{u"w1"_s, second.id}));
        loadable();

        // One cut end becomes a splice where it was; the other piece is re-attached to it,
        // which removes its now unused cut end.
        const QPointF cutAPosition = f.model()->placement(cutA)->position;
        f.view.replaceWithSplice(cutA);
        QVERIFY(!f.model()->find<FreeEnd>(cutA));
        QCOMPARE(h.splices.size(), before.splices.size() + 1);
        const QString spliceId = h.splices.back().id;
        QCOMPARE(h.splices.back().label, u"S2"_s);
        QCOMPARE(f.model()->find<Wire>(u"w1"_s)->endB, WireEnd(SpliceRef{spliceId}));
        QCOMPARE(f.model()->placement(spliceId)->position, cutAPosition);
        f.document.undoStack()->push(setWireEnd(f.model(), second.id, false, SpliceRef{spliceId}).release());
        QVERIFY(!f.model()->find<FreeEnd>(cutB));
        QCOMPARE(h.freeEnds.size(), before.freeEnds.size());
        loadable();

        for (int i = 0; i < 3; ++i)
            f.document.undoStack()->undo();
        QVERIFY(f.model()->harness() == before);
        for (int i = 0; i < 3; ++i)
            f.document.undoStack()->redo();
        QVERIFY(!f.model()->find<FreeEnd>(cutB));
    }

    // Selected wires become a twisted cable, drawn as a mark across their leads where they
    // leave a connector side by side; a shield wire gets loose ends to attach; deleting the
    // cable only ungroups.
    void cablesOnTheSchematic()
    {
        Fixture f;
        f.reset(placedSample()); // w1 ECU 1 - SENSOR 1, w4 ECU 3 - SENSOR 2, w5 ECU 4 - LAMP 1
        const Harness before = f.model()->harness();
        QUndoStack *stack = f.document.undoStack();
        auto loadable = [&] {
            QString error;
            QVERIFY2(io::deserializeHarness(io::serializeHarness(f.model()->harness()), &error), qPrintable(error));
        };

        Selection one;
        one.wires = {u"w1"_s};
        f.selection.setSelection(one);
        QVERIFY(!f.view.makeCableFromSelection()); // one wire is not a cable

        Selection pair;
        pair.wires = {u"w4"_s, u"w1"_s};
        f.selection.setSelection(pair);
        {
            // A right-click on one selected wire keeps both selected for the context menu.
            const WireItem *w1 = f.scene()->wireItem(u"w1"_s);
            const QPoint onW1 = f.toView({w1->runX(), w1->sceneBoundingRect().center().y()});
            QTest::mousePress(f.view.viewport(), Qt::RightButton, {}, onW1);
            QTest::mouseRelease(f.view.viewport(), Qt::RightButton, {}, onW1);
            QCOMPARE(f.selection.selection().wires, pair.wires);
        }
        QVERIFY(f.view.makeCableFromSelection());
        QCOMPARE(f.model()->harness().cables.size(), size_t(1));
        const Cable cable = f.model()->harness().cables[0];
        QCOMPARE(cable.label, u"TP1"_s);
        QVERIFY(cable.twisted);
        QCOMPARE(cable.wireIds, (std::vector<QString>{u"w1"_s, u"w4"_s})); // model order
        QCOMPARE(f.selection.selection().cables, QSet<QString>{cable.id});
        CableItem *item = f.scene()->cableItem(cable.id);
        QVERIFY(item && item->isSelected());
        // ECU 1 and 3 leave ECU's right side 40 apart, and SENSOR 1 and 2 its left side: a
        // mark at each end, kMarkOffset along the leads.
        QCOMPARE(item->marks().size(), size_t(2));
        QVERIFY(item->tags().empty());
        const WireItem *w1 = f.scene()->wireItem(u"w1"_s);
        const CableItem::Mark &atEcu = item->marks()[0];
        QCOMPARE(atEcu.x, w1->anchorA().point.x() + CableItem::kMarkOffset);
        QCOMPARE(atEcu.ys.size(), size_t(2));
        QCOMPARE(atEcu.ys[1] - atEcu.ys[0], 2 * ConnectorItem::kRowHeight);
        loadable();

        // Moving SENSOR keeps the marks on the leads.
        SchematicPlacement moved = *f.model()->placement(u"sensor"_s);
        moved.position += QPointF(100, 40);
        stack->push(new SetPlacementsCommand(f.model(), {{u"sensor"_s, moved}}, {}));
        QCOMPARE(item->marks()[1].x, w1->anchorB().point.x() - CableItem::kMarkOffset);

        // A shield wire between two bare ends, drawn dotted.
        QVERIFY(f.view.addShieldWire(cable.id));
        const Cable &shielded = *f.model()->find<Cable>(cable.id);
        QVERIFY(shielded.shielded());
        const Wire &shield = *f.model()->find<Wire>(shielded.shieldWireId);
        QCOMPARE(shield.name, u"TP1 shield"_s);
        QCOMPARE(f.model()->find<FreeEnd>(std::get<FreeEndRef>(shield.endA).freeEndId)->kind, FreeEndKind::Bare);
        QVERIFY(f.scene()->wireItem(shield.id)->isShield());
        QVERIFY(!f.scene()->wireItem(u"w1"_s)->isShield());
        QCOMPARE(f.selection.selection().wires, QSet<QString>{shield.id});
        QVERIFY(!f.view.addShieldWire(cable.id)); // one shield only
        loadable();
        // Removing the shield takes its unused bare ends along.
        stack->push(removeShieldWire(f.model(), cable.id).release());
        QVERIFY(!f.model()->find<Cable>(cable.id)->shielded());
        QCOMPARE(f.model()->harness().freeEnds.size(), before.freeEnds.size());
        stack->undo();
        QVERIFY(f.scene()->wireItem(f.model()->find<Cable>(cable.id)->shieldWireId)->isShield());

        // Conductors that do not leave one item side by side get a tag each.
        Selection apart;
        apart.wires = {u"w1"_s, u"w5"_s};
        f.selection.setSelection(apart);
        QVERIFY(f.view.makeCableFromSelection());
        const QString apartId = *f.selection.selection().cables.begin();
        QVERIFY(f.scene()->cableItem(apartId)->marks().empty());
        QCOMPARE(f.scene()->cableItem(apartId)->tags().size(), size_t(2));
        QCOMPARE(f.model()->find<Cable>(apartId)->label, u"TP2"_s);

        // Deleting a cable ungroups it: the wires stay.
        f.view.deleteSelection();
        QVERIFY(!f.model()->find<Cable>(apartId));
        QVERIFY(!f.scene()->cableItem(apartId));
        QCOMPARE(f.model()->harness().wires.size(), before.wires.size() + 1); // + the shield
        loadable();

        while (stack->canUndo())
            stack->undo();
        QVERIFY(f.model()->harness() == before);
        QVERIFY(!f.scene()->cableItem(cable.id));
    }

    // A cut on a routed wire puts both cut ends at one point in the loom, so an add-on drawn
    // from either cut end routes through the cut (hands-on test: two points, "not routed").
    void cutWireIntoTheLoom()
    {
        Fixture f;
        f.reset(placedSample()); // w1: ecu(n1) --seg1 300-- n2 --seg2 450-- sensor(n3)
        const Harness before = f.model()->harness();
        const Harness &h = f.model()->harness();
        auto loadable = [&] {
            QString error;
            QVERIFY2(io::deserializeHarness(io::serializeHarness(h), &error), qPrintable(error));
        };
        auto cutNode = [&]() -> const LayoutNode * {
            for (const LayoutNode &n : h.layoutNodes) {
                if (!n.sharedRefIds.empty())
                    return &n;
            }
            return nullptr;
        };

        // Not strictly inside the route (its full 750 mm), or on a connector: nothing changes.
        QVERIFY(!f.view.cutWireAt(u"w1"_s, QPointF(300, 40), SchematicView::CutPosition{false, 750}));
        QVERIFY(!f.view.cutWireAt(u"w1"_s, QPointF(300, 40), SchematicView::CutPosition{false, 0}));
        QVERIFY(h == before);

        // 500 mm from ECU: 300 along seg1, then 200 into seg2 (450 = 200 + 250).
        QVERIFY(f.view.cutWireAt(u"w1"_s, QPointF(300, 40), SchematicView::CutPosition{false, 500}));
        const Wire &first = *f.model()->find<Wire>(u"w1"_s);
        const Wire &second = h.wires.back();
        const QString cutA = std::get<FreeEndRef>(first.endB).freeEndId;
        const QString cutB = std::get<FreeEndRef>(second.endA).freeEndId;
        const LayoutNode *cut = cutNode();
        QVERIFY(cut);
        QCOMPARE(cut->type, LayoutNodeType::FlyingLead);
        QCOMPARE(cut->refId, cutA);
        QCOMPARE(cut->sharedRefIds, std::vector<QString>{cutB});
        QCOMPARE(h.layoutSegments.size(), before.layoutSegments.size() + 1);
        QCOMPARE(f.model()->find<LayoutSegment>(u"seg2"_s)->lengthMm, 200.0);
        QCOMPARE(h.layoutSegments.back().lengthMm, 250.0);
        // Each piece routes to the cut: 300 + 200 = 500 and 250.
        const calc::LayoutGraph graph(h);
        QCOMPARE(calc::wireLength(h, graph, first).baseMm, 500.0);
        QCOMPARE(calc::wireLength(h, graph, second).baseMm, 250.0);
        loadable();
        f.document.undoStack()->undo(); // one undo step
        QVERIFY(h == before);

        // 100 mm from SENSOR (end B): seg2 keeps 350 from n2, 100 from the cut to SENSOR.
        QVERIFY(f.view.cutWireAt(u"w1"_s, QPointF(300, 40), SchematicView::CutPosition{true, 100}));
        QCOMPARE(f.model()->find<LayoutSegment>(u"seg2"_s)->lengthMm, 350.0);
        QCOMPARE(h.layoutSegments.back().lengthMm, 100.0);
        loadable();
        f.document.undoStack()->undo();

        // 300 mm from ECU is the fork n2: the cut takes its place, no new segment.
        QVERIFY(f.view.cutWireAt(u"w1"_s, QPointF(300, 40), SchematicView::CutPosition{false, 300}));
        cut = cutNode();
        QVERIFY(cut);
        QCOMPARE(cut->id, u"n2"_s);
        QCOMPARE(h.layoutSegments.size(), before.layoutSegments.size());
        loadable();

        // An add-on wire from cut end A to the lamp, placed off the cut point, routes through it.
        const QString cutPoint = cut->id;
        const QString newCutA = cut->refId; // ids are new for each cut
        f.document.undoStack()->push(
            addWire(f.model(), testing::wire(u"addon"_s, FreeEndRef{newCutA}, PinRef{u"lamp"_s, u"lamp1"_s}), {}, {})
                .release());
        LayoutNode lamp = testing::node(u"n9"_s, LayoutNodeType::Connector, u"lamp"_s);
        f.document.undoStack()->push(new AddEntityCommand<LayoutNode>(f.model(), lamp, {}, {}));
        f.document.undoStack()->push(new AddEntityCommand<LayoutSegment>(
            f.model(), testing::segment(u"seg9"_s, cutPoint, u"n9"_s, 500), {}, {}));
        const calc::WireLength addon = calc::wireLength(h, calc::LayoutGraph(h), *f.model()->find<Wire>(u"addon"_s));
        QCOMPARE(addon.status, calc::RouteStatus::Ok);
        QCOMPARE(addon.baseMm, 500.0);
    }

    // A wire keeps the splice lead it was drawn from or dropped on, even when it heads off to
    // the other side (several wires on one side of a splice, connectors on both sides).
    void wireKeepsTheSpliceLeadItUses()
    {
        Fixture f;
        f.reset(placedSample()); // splice sp at (400, 200), SENSOR at x 600, LAMP at (600, 300)
        f.view.show();
        QTest::qWait(20);
        f.view.resetTransform();
        f.view.centerOn(QPointF(450, 200));
        const Harness &h = f.model()->harness();
        SpliceItem *splice = f.scene()->spliceItem(u"sp"_s);
        // Middle of the left lead: x = -(kRadius + 1 + kStubLength / 2) = -(6 + 1 + 7) = -14.
        const QPoint leftLead = f.toView(splice->mapToScene(QPointF(-14, 0)));
        // Left lead tip: x = 400 - (6 + 1 + 14) = 379.
        const QPointF leftTip(379, 200);
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, from);
            QTest::mouseMove(f.view.viewport(), from - QPoint(20, 0));
            QTest::mouseMove(f.view.viewport(), to);
            QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, to);
        };

        // From the splice's left lead to SENSOR pin 2, which is to the right.
        ConnectorItem *sensor = f.scene()->connectorItem(u"sensor"_s);
        const size_t wires = h.wires.size();
        drag(leftLead, f.toView(sensor->mapToScene(sensor->circuitCellRect(1).center())));
        QCOMPARE(h.wires.size(), wires + 1);
        QCOMPARE(h.wires.back().endA, WireEnd(SpliceRef{u"sp"_s}));
        QCOMPARE(h.wires.back().leadA, WireLead::Left);
        QCOMPARE(f.scene()->endAnchor(h.wires.back(), false).point, leftTip);

        // From LAMP pin 1 (to the right) dropped on the splice's left lead.
        ConnectorItem *lamp = f.scene()->connectorItem(u"lamp"_s);
        drag(f.toView(lamp->mapToScene(lamp->circuitCellRect(0).center())), leftLead);
        QCOMPARE(h.wires.size(), wires + 2);
        QCOMPARE(h.wires.back().endB, WireEnd(SpliceRef{u"sp"_s}));
        QCOMPARE(h.wires.back().leadB, WireLead::Left);
        QCOMPARE(f.scene()->endAnchor(h.wires.back(), true).point, leftTip);

        // Dropped on the dot instead: Auto, the wire meets the dot as before.
        drag(f.toView(lamp->mapToScene(lamp->circuitCellRect(0).center())), f.toView(splice->scenePos()));
        QCOMPARE(h.wires.back().leadB, WireLead::Auto);
        QCOMPARE(f.scene()->endAnchor(h.wires.back(), true).point, QPointF(400, 200));

        // Re-attaching an end to the other lead of the same splice moves it there (one undo step).
        const QString wireId = h.wires.back().id;
        f.document.undoStack()->push(
            setWireEnd(f.model(), wireId, true, SpliceRef{u"sp"_s}, WireLead::Right).release());
        QCOMPARE(f.model()->find<Wire>(wireId)->leadB, WireLead::Right);
        QVERIFY(!setWireEnd(f.model(), wireId, true, SpliceRef{u"sp"_s}, WireLead::Right)); // no change
        f.document.undoStack()->undo();
        QCOMPARE(f.model()->find<Wire>(wireId)->leadB, WireLead::Auto);
    }

    // An add-on drawn from a cut end's open lead stays on that lead, even when it heads off
    // past the factory piece's side (it used to jump to the lead the factory piece uses).
    void wireFromCutEndKeepsTheOpenLead()
    {
        Fixture f;
        Harness start = placedSample();
        start.wires[0].factory = true; // w1: ECU 1 (x 0) to SENSOR 1 (x 600)
        f.reset(start);
        f.view.cutWireAt(u"w1"_s, QPointF(300, 40), std::nullopt);
        const Harness &h = f.model()->harness();
        const QString secondId = h.wires.back().id;
        const QString cutB = std::get<FreeEndRef>(h.wires.back().endA).freeEndId; // SENSOR side
        FreeEndItem *cut = f.scene()->freeEndItem(cutB);
        const QPointF at = cut->scenePos();
        // Each piece uses the lead facing away from the other cut end; SENSOR's piece the right.
        // Lead tips: x = at.x -/+ (kHalfWidth + kStubLength) = at.x -/+ (12 + 14) = at.x -/+ 26.
        QCOMPARE(h.wires.back().leadA, WireLead::Right);
        QCOMPARE(f.model()->find<Wire>(u"w1"_s)->leadB, WireLead::Left);
        QCOMPARE(f.scene()->endAnchor(h.wires.back(), false).point, at + QPointF(26, 0));

        // Drag from the open (left) lead, x = -19, onto LAMP (x 600, right of the cut).
        f.view.show();
        QTest::qWait(20);
        f.view.resetTransform();
        f.view.centerOn(at);
        const QPoint lead = f.toView(cut->mapToScene(QPointF(-19, 0)));
        ConnectorItem *lamp = f.scene()->connectorItem(u"lamp"_s);
        const QPoint pin = f.toView(lamp->mapToScene(lamp->circuitCellRect(0).center()));
        const size_t wires = h.wires.size();
        QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, lead);
        QTest::mouseMove(f.view.viewport(), lead - QPoint(20, 0));
        QTest::mouseMove(f.view.viewport(), pin);
        QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, pin);
        QCOMPARE(h.wires.size(), wires + 1);
        const Wire &addOn = h.wires.back();
        QCOMPARE(addOn.endA, WireEnd(FreeEndRef{cutB}));
        QCOMPARE(f.scene()->endAnchor(addOn, false).point, at - QPointF(26, 0));
        // The factory piece keeps its lead.
        QCOMPARE(f.scene()->endAnchor(*f.model()->find<Wire>(secondId), false).point, at + QPointF(26, 0));
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

    // Where wires lie on top of each other, a drag grabs the wire whose vertical run is under
    // the mouse, not the topmost one whose horizontal leg passes there, and a stack of runs
    // comes apart one wire per drag.
    void draggingOverlappingWiresGrabsTheRunUnderTheMouse()
    {
        Fixture f;
        Harness h;
        h.connectors = {makeConnector(u"A"_s, 3), makeConnector(u"B"_s, 3)};
        h.schematic.placements[h.connectors[0].id] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        h.schematic.placements[h.connectors[1].id] = SchematicPlacement{QPointF(600, 0), false, 0, {}};
        auto add = [&](const QString &id, int pinA, int pinB) {
            Wire w;
            w.id = id;
            w.endA = PinRef{h.connectors[0].id, h.connectors[0].pins[size_t(pinA)].id};
            w.endB = PinRef{h.connectors[1].id, h.connectors[1].pins[size_t(pinB)].id};
            h.wires.push_back(w);
        };
        // All runs default to x 450 (see draggingWireMovesItsVerticalRun). Rows are at
        // y 56, 76 and 96: w1 runs 56..96, w2 96..56, w3 is straight along 76, w4 runs
        // 56..76 and is the topmost wire.
        add(u"w1"_s, 0, 2);
        add(u"w2"_s, 2, 0);
        add(u"w3"_s, 1, 1);
        add(u"w4"_s, 0, 1);
        f.reset(h);
        auto runs = [&] {
            QList<qreal> x;
            for (const QString &id : {u"w1"_s, u"w2"_s, u"w3"_s, u"w4"_s})
                x << f.scene()->wireItem(id)->runX();
            return x;
        };
        QCOMPARE(runs(), (QList<qreal>{450, 450, 450, 450}));

        // Drag 40 px right from (450, 61), 5 px below row 1, three times.
        auto drag = [&] {
            const QPoint grab = f.toView(QPointF(450, 61));
            const QPoint drop = f.toView(QPointF(490, 61));
            QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, grab);
            QTest::mouseMove(f.view.viewport(), grab + QPoint(10, 0));
            QTest::mouseMove(f.view.viewport(), drop);
            QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, drop);
        };
        // The topmost run there (w4) moves first, to 450 + 40 = 490.
        drag();
        QCOMPARE(runs(), (QList<qreal>{450, 450, 450, 490}));
        QCOMPARE(f.selection.selection().wires, QSet<QString>{u"w4"_s});
        // w4's horizontal leg still crosses (450, 61); w2's run under it is grabbed instead.
        drag();
        QCOMPARE(runs(), (QList<qreal>{450, 490, 450, 490}));
        QCOMPARE(f.selection.selection().wires, QSet<QString>{u"w2"_s});
        QCOMPARE(f.scene()->wireItem(u"w2"_s)->zValue(), WireItem::kSelectedZ); // drawn on top
        QCOMPARE(f.scene()->wireItem(u"w4"_s)->zValue(), 0.0);
        drag();
        QCOMPARE(runs(), (QList<qreal>{490, 490, 450, 490}));
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

        // Ground, chassis ground and power symbols, an outline around J1, and a selected
        // wire with its end handles.
        Harness symbols = ends;
        symbols.freeEnds[0].symbol = SchematicSymbol::Power;
        symbols.freeEnds[0].label = u"12V+"_s;
        symbols.freeEnds[1].symbol = SchematicSymbol::Ground;
        symbols.freeEnds[2].symbol = SchematicSymbol::ChassisGround;
        symbols.notes.push_back(Note{newId(), NoteCanvas::Schematic, u"Steering wheel"_s, QPointF(-40, -60), {},
                                     QSizeF(400, 200), {}});
        f.reset(symbols);
        Selection selectedWire;
        selectedWire.wires.insert(symbols.wires[3].id);
        f.selection.setSelection(selectedWire);
        f.view.resize(1400, 900);
        f.view.zoomToFit();
        f.view.grab().save(imageDir() + u"/schematic-symbols-outline.png"_s);
        f.view.resetTransform();

        // A factory harness (dashed) cut and spliced into an add-on.
        Harness addOn = placedSample();
        addOn.connectors[1].factory = true;
        addOn.wires[0].factory = true;
        addOn.wires[0].baseColor = u"GRN"_s;
        addOn.wires[0].gauge = {18, GaugeSystem::Awg};
        f.reset(addOn);
        f.view.cutWireAt(u"w1"_s, QPointF(510, 56), std::nullopt); // between ECU and SENSOR
        f.view.replaceWithSplice(std::get<FreeEndRef>(f.model()->find<Wire>(u"w1"_s)->endB).freeEndId);
        f.view.resize(1400, 900);
        f.view.zoomToFit();
        f.view.grab().save(imageDir() + u"/schematic-cut-factory.png"_s);
        f.view.resetTransform();

        // A shielded twisted pair and a bought triple with tags.
        Harness cables = placedSample();
        Cable tp;
        tp.id = u"tp"_s;
        tp.label = u"CAN"_s;
        tp.wireIds = {u"w1"_s, u"w4"_s};
        tp.shieldWireId = u"w2"_s;
        cables.cables = {tp};
        f.reset(cables);
        f.view.resize(1400, 900);
        f.view.zoomToFit();
        f.view.grab().save(imageDir() + u"/schematic-cables.png"_s);
        f.view.resetTransform();
        f.view.centerOn(QPointF(450, 100));
        f.view.scale(2, 2);
        f.view.grab().save(imageDir() + u"/schematic-cables-zoom.png"_s);
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
