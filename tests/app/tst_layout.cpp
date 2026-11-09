// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"
#include "SelectionModel.h"
#include "calc/WireLength.h"
#include "commands/EditOps.h"
#include "commands/HarnessCommands.h"
#include "io/HarnessJson.h"
#include "io/TbhFile.h"
#include "layout/LayoutScene.h"
#include "layout/LayoutView.h"
#include "schematic/SchematicScene.h"
#include "schematic/SchematicView.h"
#include "ui/Format.h"
#include "ui/MainWindow.h"
#include "ui/ProblemsPanel.h"
#include "ui/theme/Theme.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTabWidget>
#include <QLineEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace tbh;
using namespace tbh::app;
using namespace Qt::StringLiterals;

namespace {

Connector connector(const QString &id, int pins)
{
    Connector c;
    c.id = id;
    c.label = id.toUpper();
    for (int i = 1; i <= pins; ++i)
        c.pins.push_back(Pin{id + QString::number(i), QString::number(i), {}, {}, {}, false, {}, {}});
    return c;
}

Wire wire(const QString &id, WireEnd a, WireEnd b)
{
    Wire w;
    w.id = id;
    w.endA = std::move(a);
    w.endB = std::move(b);
    w.baseColor = u"RED"_s;
    return w;
}

/* Same hand-calculated layout as tests/core/tst_wirelength.cpp:
     ecu(n1) --300-- branch(n2) --450-- sensor(n3);  n2 --120-- splice(n4)
   allowances pin 10, splice 5; loop 5 %; rounding 5 mm.
   w1 ecu:1 -> sensor:1  : (750 + 20) * 1.05 = 808.5 -> 810 mm.            */
Harness sample()
{
    Harness h;
    h.info.units = Units::Millimeters;
    h.connectors = {connector(u"ecu"_s, 4), connector(u"sensor"_s, 2), connector(u"lamp"_s, 1)};
    h.splices = {Splice{u"sp"_s, u"S1"_s, {}, {}}};
    h.wires = {wire(u"w1"_s, PinRef{u"ecu"_s, u"ecu1"_s}, PinRef{u"sensor"_s, u"sensor1"_s}),
               wire(u"w2"_s, PinRef{u"ecu"_s, u"ecu2"_s}, SpliceRef{u"sp"_s}),
               wire(u"w3"_s, SpliceRef{u"sp"_s}, FreeEnd{FreeEndKind::FlyingLead, {}, {}})};
    h.layoutNodes = {LayoutNode{u"n1"_s, LayoutNodeType::Connector, u"ecu"_s, QPointF(0, 0), {}},
                     LayoutNode{u"n2"_s, LayoutNodeType::Branch, {}, QPointF(300, 0), {}},
                     LayoutNode{u"n3"_s, LayoutNodeType::Connector, u"sensor"_s, QPointF(600, 0), {}},
                     LayoutNode{u"n4"_s, LayoutNodeType::Splice, u"sp"_s, QPointF(300, 200), {}},
                     LayoutNode{u"n5"_s, LayoutNodeType::FlyingLead, freeEndPlacementKey(u"w3"_s, true),
                                QPointF(500, 200), {}}};
    h.layoutSegments = {
        LayoutSegment{u"s1"_s, u"n1"_s, u"n2"_s, 300, {}, {}, SleevingCoverage::Full, {}},
        LayoutSegment{u"s2"_s, u"n2"_s, u"n3"_s, 450, {}, {}, SleevingCoverage::Full, {}},
        LayoutSegment{u"s3"_s, u"n2"_s, u"n4"_s, 120, {}, {}, SleevingCoverage::Full, {}},
        LayoutSegment{u"s4"_s, u"n4"_s, u"n5"_s, 80, {}, {}, SleevingCoverage::Full, {}}};
    h.settings.stripAllowanceMm = {{EndType::Pin, 10}, {EndType::Splice, 5}};
    h.settings.serviceLoopPercent = 5;
    h.settings.lengthRoundingIncrementMm = 5;
    h.schematic.placements[u"ecu"_s] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
    h.schematic.placements[u"sensor"_s] = SchematicPlacement{QPointF(600, 0), false, 0, {}};
    h.schematic.placements[u"lamp"_s] = SchematicPlacement{QPointF(600, 200), false, 0, {}};
    h.schematic.placements[u"sp"_s] = SchematicPlacement{QPointF(400, 300), false, 0, {}};
    return h;
}

// Every edit must leave a harness that still loads.
void verifyLoadable(const Harness &h)
{
    QString error;
    QVERIFY2(io::deserializeHarness(io::serializeHarness(h), &error), qPrintable(error));
}

QLineEdit *visibleEditor(QWidget *viewport)
{
    for (auto *e : viewport->findChildren<QLineEdit *>()) {
        if (e->isVisible())
            return e;
    }
    return nullptr;
}

} // namespace

class TestLayout : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_layout"_s);
        QSettings().clear();
    }

    void explainedLength()
    {
        const Harness h = sample();
        const calc::WireLength l = calc::wireLength(h, calc::LayoutGraph(h), h.wires[0]);
        const WireLengthText text = describeWireLength(h, h.wires[0], l);
        QCOMPARE(text.headline, u"810 mm"_s);
        QVERIFY2(text.detail.contains(u"750 mm"_s) && text.detail.contains(u"38.5 mm"_s)
                     && text.detail.contains(u"808.5 mm"_s) && text.detail.contains(u"810 mm"_s),
                 qPrintable(text.detail));
        Harness inches = h;
        inches.info.units = Units::Inches;
        // 810 mm / 25.4 = 31.889... in
        QCOMPARE(describeWireLength(inches, h.wires[0], l).headline, u"31.89 in"_s);
    }

    void drawSegmentAndTypeLength()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        Harness h = sample();
        h.layoutSegments.pop_back(); // n4-n5 missing
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(300, 100);
        view.setTool(LayoutView::Tool::Segment);

        const QPoint from = view.mapFromScene(QPointF(300, 200)); // splice node
        const QPoint to = view.mapFromScene(QPointF(500, 200));   // flying lead node
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(document.model()->harness().layoutSegments.size(), size_t(4));
        const LayoutSegment created = document.model()->harness().layoutSegments.back();
        QCOMPARE(created.lengthMm, 0.0);
        QTest::qWait(1);
        QLineEdit *editor = visibleEditor(view.viewport());
        QVERIFY(editor); // length editor opens right away
        editor->setText(u"80"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(document.model()->find<LayoutSegment>(created.id)->lengthMm, 80.0);

        // Drag to empty space adds a branch point plus segment.
        const QPoint empty = view.mapFromScene(QPointF(600, 350));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, view.mapFromScene(QPointF(600, 0)));
        QTest::mouseMove(view.viewport(), empty);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, empty);
        QCOMPARE(document.model()->harness().layoutNodes.size(), size_t(6));
        QCOMPARE(document.model()->harness().layoutNodes.back().type, LayoutNodeType::Branch);
        QTest::keyClick(visibleEditor(view.viewport()), Qt::Key_Escape);

        document.undoStack()->undo();
        QCOMPARE(document.model()->harness().layoutNodes.size(), size_t(5));
        verifyLoadable(document.model()->harness());
    }

    void deletingNodeRemovesSegmentsAndOverrides()
    {
        HarnessModel model;
        Harness h = sample();
        h.wires[0].routeSegmentIds = {u"s1"_s, u"s2"_s};
        model.reset(h);
        QUndoStack stack;
        stack.push(removeLayoutItems(&model, {u"n2"_s}, {}).release());
        QCOMPARE(model.harness().layoutNodes.size(), size_t(4));
        QCOMPARE(model.harness().layoutSegments.size(), size_t(1)); // only s4 is left
        QVERIFY(model.find<Wire>(u"w1"_s)->routeSegmentIds.empty());
        verifyLoadable(model.harness());
        stack.undo();
        QVERIFY(model.harness() == h);
    }

    void deletingConnectorClearsLayoutReference()
    {
        HarnessModel model;
        const Harness h = sample();
        model.reset(h);
        QUndoStack stack;
        stack.push(removeItems(&model, {u"sensor"_s}, {u"sp"_s}, {}).release());
        // Nodes keep their place but lose the reference; w3's free-end node too (w3 is deleted).
        QVERIFY(model.find<LayoutNode>(u"n3"_s)->refId.isEmpty());
        QVERIFY(model.find<LayoutNode>(u"n4"_s)->refId.isEmpty());
        QVERIFY(model.find<LayoutNode>(u"n5"_s)->refId.isEmpty());
        verifyLoadable(model.harness());
        stack.undo();
        QVERIFY(model.harness() == h);
    }

    void reattachOperations()
    {
        HarnessModel model;
        const Harness h = sample();
        model.reset(h);
        QUndoStack stack;

        // Pin -> another pin.
        stack.push(setWireEnd(&model, u"w1"_s, true, PinRef{u"lamp"_s, u"lamp1"_s}, {}).release());
        QCOMPARE(std::get<PinRef>(model.find<Wire>(u"w1"_s)->endB).connectorId, u"lamp"_s);
        // Pin -> flying lead, placed where dropped.
        stack.push(setWireEnd(&model, u"w1"_s, true, FreeEnd{}, QPointF(70, 80)).release());
        QVERIFY(std::holds_alternative<FreeEnd>(model.find<Wire>(u"w1"_s)->endB));
        QCOMPARE(model.placement(freeEndPlacementKey(u"w1"_s, true))->position, QPointF(70, 80));
        // Free end (with a layout node) -> pin: placement and layout reference go away.
        stack.push(setWireEnd(&model, u"w3"_s, true, PinRef{u"lamp"_s, u"lamp1"_s}, {}).release());
        QVERIFY(!model.placement(freeEndPlacementKey(u"w3"_s, true)));
        QVERIFY(model.find<LayoutNode>(u"n5"_s)->refId.isEmpty());
        verifyLoadable(model.harness());
        // No-ops: onto the same pin, or onto the wire's other end.
        QVERIFY(!setWireEnd(&model, u"w2"_s, false, PinRef{u"ecu"_s, u"ecu2"_s}, {}));
        QVERIFY(!setWireEnd(&model, u"w2"_s, false, SpliceRef{u"sp"_s}, {}));

        stack.undo();
        stack.undo();
        stack.undo();
        QVERIFY(model.harness() == h);
    }

    void reattachByDraggingWireEnd()
    {
        Document document;
        SelectionModel selection(document.model());
        SchematicView view(&document, &selection);
        view.resize(1200, 800);
        view.show();
        document.model()->reset(sample());
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(450, 150);

        Selection s;
        s.wires.insert(u"w1"_s);
        selection.setSelection(s);
        const Wire &w1 = *document.model()->find<Wire>(u"w1"_s);
        const QPoint end = view.mapFromScene(view.schematicScene()->endAnchor(w1, true).point);
        ConnectorItem *lamp = view.schematicScene()->connectorItem(u"lamp"_s);
        const QPoint target = view.mapFromScene(lamp->mapToScene(lamp->circuitCellRect(0).center()));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, end);
        QTest::mouseMove(view.viewport(), end + QPoint(30, 30));
        QTest::mouseMove(view.viewport(), target);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, target);
        QCOMPARE(std::get<PinRef>(document.model()->find<Wire>(u"w1"_s)->endB).pinId, u"lamp1"_s);

        // Drag the flying lead of w3 onto sensor:2 to attach it.
        Selection none;
        selection.setSelection(none);
        FreeEndItem *freeEnd = view.schematicScene()->freeEndItem(freeEndPlacementKey(u"w3"_s, true));
        QVERIFY(freeEnd);
        ConnectorItem *sensor = view.schematicScene()->connectorItem(u"sensor"_s);
        const QPoint from = view.mapFromScene(freeEnd->scenePos());
        const QPoint onPin = view.mapFromScene(sensor->mapToScene(sensor->circuitCellRect(1).center()));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(20, 0));
        QTest::mouseMove(view.viewport(), onPin);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, onPin);
        QCOMPARE(std::get<PinRef>(document.model()->find<Wire>(u"w3"_s)->endB).pinId, u"sensor2"_s);
        verifyLoadable(document.model()->harness());
    }

    void settingsCommandUndo()
    {
        HarnessModel model;
        model.reset(sample());
        const Settings before = model.harness().settings;
        Settings after = before;
        after.stripAllowanceMm[EndType::RingTerminal] = 12;
        after.serviceLoopPercent = 10;
        QUndoStack stack;
        stack.push(new SetHarnessSettingsCommand(&model, after));
        QVERIFY(model.harness().settings == after);
        stack.undo();
        QVERIFY(model.harness().settings == before);
    }

    void problemsPanelListsUnrouted()
    {
        Document document;
        ProblemsPanel panel(&document);
        QSignalSpy count(&panel, &ProblemsPanel::countChanged);
        Harness h = sample();
        h.wires.push_back(wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, PinRef{u"lamp"_s, u"lamp1"_s}));
        document.model()->reset(h);
        QVERIFY(count.wait(1000));
        QCOMPARE(panel.count(), 1); // lamp is not in the layout
        QVERIFY(panel.item(0)->text().contains(u"LAMP"_s));
    }

    // M3 acceptance: clicking a wire in the schematic shows its explained length.
    void clickingWireShowsLength()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(u"m3.tbh"_s);
        QVERIFY(io::writeTbhFile(file, sample(), nullptr));
        Theme theme;
        MainWindow window(&theme);
        window.resize(1500, 900);
        window.show();
        QVERIFY(window.openFile(file));
        QTest::qWait(10);
        auto *view = window.findChild<SchematicView *>();
        view->resetTransform();
        view->centerOn(450, 150);
        // Click on w1's horizontal stub just outside ECU's right edge, row 1.
        const Anchor a = view->schematicScene()->endAnchor(*view->schematicScene()->model()->find<Wire>(u"w1"_s), false);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->mapFromScene(a.point + QPointF(8, 0)));
        QTest::qWait(1);
        bool found = false;
        for (QLabel *label : window.findChildren<QLabel *>()) {
            if (label->isVisible() && label->text() == u"810 mm"_s)
                found = true;
        }
        QVERIFY(found);
        if (const QString dirPath = qEnvironmentVariable("TBH_SCREENSHOT_DIR"); !dirPath.isEmpty()) {
            window.grab().save(dirPath + u"/m3-wire.png"_s);
            auto *tabs = window.findChild<QTabWidget *>();
            tabs->setCurrentIndex(1);
            QTest::qWait(10);
            window.findChild<LayoutView *>()->zoomToFit();
            window.grab().save(dirPath + u"/m3-layout.png"_s);
        }
    }
};

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
