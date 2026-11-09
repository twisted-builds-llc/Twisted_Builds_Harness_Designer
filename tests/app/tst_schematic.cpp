// SPDX-License-Identifier: GPL-3.0-or-later
#include "Autosave.h"
#include "Document.h"
#include "SelectionModel.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "io/TbhFile.h"
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
        f.document.undoStack()->push(removeItems(f.model(), {}, {}, {wireId}).release());
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
        QCOMPARE(std::get<FreeEnd>(lead.endB).kind, FreeEndKind::FlyingLead);
        QVERIFY(f.model()->placement(freeEndPlacementKey(lead.id, true)));

        // A click without dragging only selects.
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, from);
        QCOMPARE(f.model()->harness().wires.size(), size_t(2));
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
        f.document.undoStack()->push(removeItems(f.model(), {j1}, {}, {}).release());
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
        theme.setMode(Theme::Mode::System);
    }
};

QTEST_MAIN(TestSchematic)
#include "tst_schematic.moc"
