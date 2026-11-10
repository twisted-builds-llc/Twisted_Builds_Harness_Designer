// SPDX-License-Identifier: GPL-3.0-or-later
#include "Autosave.h"
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
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QLineEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QTest>

#include <cmath>

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
    h.freeEnds = {FreeEnd{u"fl"_s, FreeEndKind::FlyingLead, {}, {}, {}}};
    h.wires = {wire(u"w1"_s, PinRef{u"ecu"_s, u"ecu1"_s}, PinRef{u"sensor"_s, u"sensor1"_s}),
               wire(u"w2"_s, PinRef{u"ecu"_s, u"ecu2"_s}, SpliceRef{u"sp"_s}),
               wire(u"w3"_s, SpliceRef{u"sp"_s}, FreeEndRef{u"fl"_s})};
    h.layoutNodes = {LayoutNode{u"n1"_s, LayoutNodeType::Connector, u"ecu"_s, QPointF(0, 0), {}},
                     LayoutNode{u"n2"_s, LayoutNodeType::Branch, {}, QPointF(300, 0), {}},
                     LayoutNode{u"n3"_s, LayoutNodeType::Connector, u"sensor"_s, QPointF(600, 0), {}},
                     LayoutNode{u"n4"_s, LayoutNodeType::Splice, u"sp"_s, QPointF(300, 200), {}},
                     LayoutNode{u"n5"_s, LayoutNodeType::FlyingLead, u"fl"_s, QPointF(500, 200), {}}};
    h.layoutSegments = {
        LayoutSegment{u"s1"_s, u"n1"_s, u"n2"_s, 300, {}, {}, SleevingCoverage::Full, std::nullopt, {}},
        LayoutSegment{u"s2"_s, u"n2"_s, u"n3"_s, 450, {}, {}, SleevingCoverage::Full, std::nullopt, {}},
        LayoutSegment{u"s3"_s, u"n2"_s, u"n4"_s, 120, {}, {}, SleevingCoverage::Full, std::nullopt, {}},
        LayoutSegment{u"s4"_s, u"n4"_s, u"n5"_s, 80, {}, {}, SleevingCoverage::Full, std::nullopt, {}}};
    h.settings.stripAllowanceMm = {{EndType::Pin, 10}, {EndType::Splice, 5}};
    h.settings.serviceLoopPercent = 5;
    h.settings.lengthRoundingIncrementMm = 5;
    h.schematic.placements[u"ecu"_s] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
    h.schematic.placements[u"sensor"_s] = SchematicPlacement{QPointF(600, 0), false, 0, {}};
    h.schematic.placements[u"lamp"_s] = SchematicPlacement{QPointF(600, 200), false, 0, {}};
    h.schematic.placements[u"sp"_s] = SchematicPlacement{QPointF(400, 300), false, 0, {}};
    h.schematic.placements[u"fl"_s] = SchematicPlacement{QPointF(600, 300), false, 0, {}};
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
        QDir(Autosave::recoveryDirectory()).removeRecursively(); // no recovery prompt
    }

    // Place All puts every unplaced item into the layout in one undo step, each offset from
    // the last (cascade step 30, 30 within a run of 8) so none hide under another.
    void placeAllUnplaced()
    {
        Document document;
        SelectionModel selection(document.model());
        Harness h = sample();
        h.layoutNodes.clear();
        h.layoutSegments.clear();
        document.model()->reset(h);
        LayoutEditor editor(&document, &selection);
        editor.resize(1200, 800);
        editor.show();
        QTest::qWait(1);
        auto *list = editor.findChild<QListWidget *>();
        QVERIFY(list);
        // Sample has 3 connectors, 1 splice and 1 free end: 5 refs, at most 8, so one run.
        const int expected = int(layoutRefs(document.model()->harness()).size());
        QCOMPARE(expected, 5);
        QCOMPARE(list->count(), expected);
        QPushButton *placeAll = nullptr;
        for (auto *b : editor.findChildren<QPushButton *>()) {
            if (b->text() == u"Place All"_s)
                placeAll = b;
        }
        QVERIFY(placeAll);
        placeAll->click();
        QTest::qWait(1);
        const Harness &after = document.model()->harness();
        QCOMPARE(int(after.layoutNodes.size()), expected);
        QCOMPARE(list->count(), 0);
        QCOMPARE(int(selection.selection().layoutNodes.size()), expected);
        for (size_t i = 1; i < after.layoutNodes.size(); ++i)
            QCOMPARE(after.layoutNodes[i].position - after.layoutNodes[i - 1].position, QPointF(30, 30));
        verifyLoadable(after);
        document.undoStack()->undo();
        QCOMPARE(document.model()->harness().layoutNodes.size(), size_t(0));
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

    void dragLeadDrawsSegmentInSelectTool()
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
        HarnessModel *model = document.model();

        // Splice node n4 at (300, 200): its right lead spans x 308..322 (radius 7 + 1, length 14).
        const QPoint lead = view.mapFromScene(QPointF(315, 200));
        const QPoint to = view.mapFromScene(QPointF(500, 200)); // flying lead node n5
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, lead);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(model->harness().layoutSegments.size(), size_t(4));
        const LayoutSegment created = model->harness().layoutSegments.back();
        QCOMPARE(created.nodeA, u"n4"_s);
        QCOMPARE(created.nodeB, u"n5"_s);
        QCOMPARE(model->find<LayoutNode>(u"n4"_s)->position, QPointF(300, 200)); // not moved
        QTest::qWait(1);
        QTest::keyClick(visibleEditor(view.viewport()), Qt::Key_Escape);

        // Connector node n3 at (600, 0): its segment leaves to the left, so the plug faces
        // right (rotated 90 degrees clockwise). Its leads in the facing-up frame are centered
        // at (+-21, -13) (body half width 12 + 2 + half of 14; y halfway up the 26 px plug);
        // rotating (x, y) -> (-y, x) puts the right lead's center at (13, 21), scene (613, 21).
        // Dragging it to empty space adds a branch point.
        const QPoint connectorLead = view.mapFromScene(QPointF(613, 21));
        const QPoint empty = view.mapFromScene(QPointF(700, 350));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, connectorLead);
        QTest::mouseMove(view.viewport(), empty);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, empty);
        QCOMPARE(model->harness().layoutNodes.size(), size_t(6));
        QCOMPARE(model->harness().layoutSegments.back().nodeA, u"n3"_s);
        QTest::qWait(1);
        QTest::keyClick(visibleEditor(view.viewport()), Qt::Key_Escape);

        // Dragging the node itself still moves it, without a segment.
        const QPoint dot = view.mapFromScene(QPointF(300, 200));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, dot);
        QTest::mouseMove(view.viewport(), dot + QPoint(10, 10));
        QTest::mouseMove(view.viewport(), dot + QPoint(40, 40));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, dot + QPoint(40, 40));
        QCOMPARE(model->harness().layoutSegments.size(), size_t(5));
        QVERIFY(model->find<LayoutNode>(u"n4"_s)->position != QPointF(300, 200));
        verifyLoadable(model->harness());
    }

    void branchPointsGrowLegs()
    {
        HarnessModel model;
        SelectionModel selection(&model);
        LayoutScene scene(&model, &selection);
        model.reset(sample());
        // n2 has s1 (to n1, left), s2 (to n3, right), s3 (to n4, down): trunk plus two outlets.
        LayoutNodeItem *branch = scene.nodeItem(u"n2"_s);
        QCOMPARE(branch->legCount(), 3);
        QUndoStack stack;
        stack.push(removeLayoutItems(&model, {}, {u"s3"_s}).release());
        QCOMPARE(branch->legCount(), 2);
        stack.undo();
        QCOMPARE(branch->legCount(), 3);
        // A fourth segment adds a leg.
        LayoutNode extra{u"n9"_s, LayoutNodeType::Branch, {}, QPointF(300, -200), {}};
        model.insert(extra);
        model.insert(LayoutSegment{u"s9"_s, u"n2"_s, u"n9"_s, 50, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        QCOMPARE(branch->legCount(), 4);
        QCOMPARE(scene.nodeItem(u"n9"_s)->legCount(), 1);
    }

    void spliceShowsRunTotal()
    {
        HarnessModel model;
        SelectionModel selection(&model);
        LayoutScene scene(&model, &selection);
        model.reset(sample());
        // Splice n4 sits between branch n2 and flying lead n5: s3 120 + s4 80 = 200 mm.
        // Unit spelled out, two lines: "200 millimeters total" / "branch length".
        const QString total = LayoutScene::tr("%1 total\nbranch length").arg(formatLengthWords(200, Units::Millimeters));
        QCOMPARE(formatLengthWords(200, Units::Millimeters), u"200 millimeters"_s);
        QCOMPARE(formatLengthWords(254, Units::Inches), u"10 inches"_s); // 254 / 25.4
        QCOMPARE(formatLengthWords(25.4, Units::Inches), u"1 inch"_s);
        QTRY_COMPARE(scene.nodeItem(u"n4"_s)->subLabel(), total);
        QVERIFY(scene.nodeItem(u"n1"_s)->subLabel().isEmpty()); // connectors have none
        // Removing s4 leaves the splice at the end of a stub: no run, no total.
        QUndoStack stack;
        stack.push(removeLayoutItems(&model, {}, {u"s4"_s}).release());
        QTRY_VERIFY(scene.nodeItem(u"n4"_s)->subLabel().isEmpty());
        stack.undo();
        QTRY_COMPARE(scene.nodeItem(u"n4"_s)->subLabel(), total);
    }

    void branchPathShapes()
    {
        using Leg = LayoutNodeItem::Leg;
        // Line width 4 (no wires) plus a 3 px boot margin each side: boots are 10 wide.
        // 0-2 segments: a 28 x 10 block along the line. A straight pass-through spans
        // x = -14..14, y = -5..5.
        const QPainterPath straight = LayoutNodeItem::branchPath({Leg{{-1, 0}, 4}, Leg{{1, 0}, 4}});
        QVERIFY(straight.contains(QPointF(10, 0)));
        QVERIFY(straight.contains(QPointF(-10, 0)));
        QVERIFY(!straight.contains(QPointF(0, 8)));
        // One vertical segment turns the block upright.
        const QPainterPath end = LayoutNodeItem::branchPath({Leg{{0, 1}, 4}});
        QVERIFY(end.contains(QPointF(0, 10)));
        QVERIFY(!end.contains(QPointF(10, 0)));
        // A 10 px bundle line makes the block 16 wide: y = -8..8.
        const QPainterPath thick = LayoutNodeItem::branchPath({Leg{{-1, 0}, 10}, Leg{{1, 0}, 10}});
        QVERIFY(thick.contains(QPointF(0, 7)));
        QVERIFY(!thick.contains(QPointF(0, 9)));

        /* Y: trunk left (-1, 0), outlets (0.6, -0.8) and (0.6, 0.8), all 4 wide.
           Trunk = the leg with the smallest dot product with the sum of the others:
             d0: (-1)(0.6 + 0.6) + 0 = -1.2
             d1: 0.6(-1 + 0.6) + (-0.8)(0 + 0.8) = -0.24 - 0.64 = -0.88 (d2 the same)
           so the trunk is d0: hub radius 4/2 + 3 = 5, trunk block x = 5 to -20 (10 wide),
           outlet legs 26 long and 10 wide. (7.2, -9.6) = 12 * d1 is on a leg; (10, 0) lies
           between the legs, 8 px from each leg's center line (more than 5) and outside the hub. */
        const QPainterPath y =
            LayoutNodeItem::branchPath({Leg{{-1, 0}, 4}, Leg{{0.6, -0.8}, 4}, Leg{{0.6, 0.8}, 4}});
        QVERIFY(y.contains(QPointF(-12, 0)));
        QVERIFY(y.contains(QPointF(7.2, -9.6)));
        QVERIFY(y.contains(QPointF(7.2, 9.6)));
        QVERIFY(!y.contains(QPointF(10, 0)));
        QVERIFY(!y.contains(QPointF(12, 0)));
    }

    void segmentsDrawByBundleSize()
    {
        // 1.5 px per wire, clamped to 4..18: 0 -> 4, 2 -> 3 -> 4, 4 -> 6, 9 -> 13.5, 100 -> 18.
        QCOMPARE(LayoutSegmentItem::lineWidthFor(0), 4.0);
        QCOMPARE(LayoutSegmentItem::lineWidthFor(2), 4.0);
        QCOMPARE(LayoutSegmentItem::lineWidthFor(4), 6.0);
        QCOMPARE(LayoutSegmentItem::lineWidthFor(9), 13.5);
        QCOMPARE(LayoutSegmentItem::lineWidthFor(100), 18.0);

        HarnessModel model;
        SelectionModel selection(&model);
        LayoutScene scene(&model, &selection);
        model.reset(sample());
        // s1 (n1 -> n2) carries w1 and w2, both red without stripe, in model order.
        LayoutSegmentItem *s1 = scene.segmentItem(u"s1"_s);
        QCOMPARE(s1->wireCount(), 2);
        QCOMPARE(s1->lineWidth(), 4.0);
        // Its caption sits above the horizontal line (y = 0), clear of it, centered on x = 150.
        QVERIFY(s1->labelRect().bottom() < -s1->lineWidth() / 2);
        QCOMPARE(s1->labelRect().center().x(), 150.0);
        // s3 (n2 -> n4) is vertical at x = 300: its caption reads bottom to top, to the left.
        LayoutSegmentItem *s3 = scene.segmentItem(u"s3"_s);
        QVERIFY(s3->labelRect().right() < 300 - s3->lineWidth() / 2);
        QVERIFY(s3->toolTip().contains(u"1 wire"_s));
    }

    void endIconsFaceAwayFromTheirWires()
    {
        using Leg = LayoutNodeItem::Leg;
        // Facing = away from the sum of leg directions, snapped to an axis; 0 up, 1 right,
        // 2 down, 3 left.
        QCOMPARE(LayoutNodeItem::facingFor({}), 0);
        QCOMPARE(LayoutNodeItem::facingFor({Leg{{0, 1}, 4}}), 0);   // wire below -> up
        QCOMPARE(LayoutNodeItem::facingFor({Leg{{-1, 0}, 4}}), 1);  // wire left -> right
        QCOMPARE(LayoutNodeItem::facingFor({Leg{{0, -1}, 4}}), 2);  // wire above -> down
        QCOMPARE(LayoutNodeItem::facingFor({Leg{{0.8, 0.6}, 4}}), 3); // away (-0.8, -0.6): left
        // Equal x and y: vertical wins. Away = (-0.6, -0.6) -> up.
        QCOMPARE(LayoutNodeItem::facingFor({Leg{{0.6, 0.6}, 4}}), 0);

        HarnessModel model;
        SelectionModel selection(&model);
        LayoutScene scene(&model, &selection);
        model.reset(sample());
        // ecu (n1) at x = 0 leads right to n2: it faces left, its label left of the plug.
        LayoutNodeItem *ecu = scene.nodeItem(u"n1"_s);
        QCOMPARE(ecu->facing(), 3);
        QVERIFY(ecu->labelRect().right() < -21);
        // sensor (n3) at x = 600 leads left: faces right, label right of the plug.
        QCOMPARE(scene.nodeItem(u"n3"_s)->facing(), 1);
        QVERIFY(scene.nodeItem(u"n3"_s)->labelRect().left() > 21);
    }

    void bendGeometry()
    {
        const QPointF a(0, 0), b(100, 0);
        /* Curve through (50, 20) halfway: control c = 2 * (50, 20) - (a + b) / 2 = (50, 40).
           Chord d = (100, 0), n = (0, 100), |d|^2 = 10000:
           x = c . d / 10000 = 5000 / 10000 = 0.5, y = c . n / 10000 = 4000 / 10000 = 0.4. */
        const auto bend = bendThroughMidpoint(a, b, QPointF(50, 20));
        QVERIFY(bend);
        QCOMPARE(*bend, QPointF(0.5, 0.4));
        QCOMPARE(bendControlPoint(a, b, bend), QPointF(50, 40));
        // Asymmetric: through (80, 30): c = (160, 60) - (50, 0) = (110, 60) -> (1.1, 0.6).
        QCOMPARE(*bendThroughMidpoint(a, b, QPointF(80, 30)), QPointF(1.1, 0.6));
        // Within 4 px of the chord midpoint (50, 0): (52, 3) is sqrt(13) = 3.6 away -> straight.
        QVERIFY(!bendThroughMidpoint(a, b, QPointF(52, 3)));
        QVERIFY(!bendThroughMidpoint(a, a, QPointF(10, 10))); // no chord
        // Straight: the control point is the chord midpoint.
        QCOMPARE(bendControlPoint(a, b, std::nullopt), QPointF(50, 0));
    }

    void draggingBendHandleCurvesSegment()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        document.model()->reset(sample());
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(300, 100);
        HarnessModel *model = document.model();
        Selection s;
        s.layoutSegments.insert(u"s1"_s);
        selection.setSelection(s);

        // s1 runs n1 (0, 0) -> n2 (300, 0); its handle is at the midpoint (150, 0). Dragged
        // to (150, 60): control = (300, 120) - (150, 0) = (150, 120); d = (300, 0),
        // n = (0, 300), |d|^2 = 90000: x = 45000 / 90000 = 0.5, y = 36000 / 90000 = 0.4.
        LayoutSegmentItem *item = view.layoutScene()->segmentItem(u"s1"_s);
        QCOMPARE(item->midpoint(), QPointF(150, 0));
        const QPoint handle = view.mapFromScene(QPointF(150, 0));
        const QPoint to = view.mapFromScene(QPointF(150, 60));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, handle);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(*model->find<LayoutSegment>(u"s1"_s)->bend, QPointF(0.5, 0.4));
        QCOMPARE(model->find<LayoutNode>(u"n2"_s)->position, QPointF(300, 0)); // nothing moved
        QCOMPARE(item->midpoint(), QPointF(150, 60));
        // ecu (n1) now leaves towards the control point (150, 120): down-right, more right
        // than down, so the plug still faces left.
        QCOMPARE(view.layoutScene()->nodeItem(u"n1"_s)->facing(), 3);
        verifyLoadable(model->harness());

        document.undoStack()->undo();
        QVERIFY(!model->find<LayoutSegment>(u"s1"_s)->bend);
        document.undoStack()->redo();
        view.straightenSegment(u"s1"_s);
        QVERIFY(!model->find<LayoutSegment>(u"s1"_s)->bend);
    }

    void notesAddEditMoveDelete()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        document.model()->reset(sample());
        QTest::qWait(1);
        HarnessModel *model = document.model();

        // Cancelling the editor (Esc) adds nothing.
        view.addNote();
        QTest::keyClick(visibleEditor(view.viewport()), Qt::Key_Escape);
        QVERIFY(model->harness().notes.empty());

        view.addNote();
        QLineEdit *editor = visibleEditor(view.viewport());
        QVERIFY(editor);
        QTest::keyClicks(editor, u"Signal is blue"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(model->harness().notes.size(), size_t(1));
        const Note note = model->harness().notes.front();
        QCOMPARE(note.text, u"Signal is blue"_s);
        QVERIFY(selection.selection().notes.contains(note.id));
        QVERIFY(view.layoutScene()->noteItem(note.id));
        verifyLoadable(model->harness());

        // Edit in place.
        view.editNote(note.id);
        editor = visibleEditor(view.viewport());
        editor->setText(u"Signal is blue not yellow"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(model->find<Note>(note.id)->text, u"Signal is blue not yellow"_s);

        // Drag it 40 px right (snapped to the 10 px grid).
        view.resetTransform();
        view.centerOn(note.position);
        const QPoint at = view.mapFromScene(note.position + QPointF(10, 6));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, at);
        QTest::mouseMove(view.viewport(), at + QPoint(10, 0));
        QTest::mouseMove(view.viewport(), at + QPoint(40, 0));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, at + QPoint(40, 0));
        QCOMPARE(model->find<Note>(note.id)->position, note.position + QPointF(40, 0));

        // Delete with the selection; undo brings it back.
        view.deleteSelection();
        QVERIFY(model->harness().notes.empty());
        document.undoStack()->undo();
        QCOMPARE(model->harness().notes.size(), size_t(1));

        // Clearing the text deletes the note.
        view.editNote(note.id);
        editor = visibleEditor(view.viewport());
        editor->clear();
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(model->harness().notes.empty());
    }

    void mergeBranchIntoReattachesSegments()
    {
        HarnessModel model;
        Harness h = sample();
        // A second branch point n6 on top of n2, carrying lamp (n7).
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Branch, {}, QPointF(300, 0), {}});
        h.layoutNodes.push_back(LayoutNode{u"n7"_s, LayoutNodeType::Connector, u"lamp"_s, QPointF(300, -200), {}});
        h.layoutSegments.push_back(LayoutSegment{u"s6"_s, u"n7"_s, u"n6"_s, 200, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        h.layoutSegments.push_back(LayoutSegment{u"s7"_s, u"n6"_s, u"n2"_s, 10, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        h.wires[0].routeSegmentIds = {u"s1"_s, u"s7"_s, u"s6"_s}; // not a real path; only checks clearing
        model.reset(h);
        QUndoStack stack;
        QVERIFY(!mergeBranchInto(&model, u"n1"_s, u"n2"_s)); // n1 is a connector
        QVERIFY(!mergeBranchInto(&model, u"n6"_s, u"n6"_s));
        stack.push(mergeBranchInto(&model, u"n6"_s, u"n2"_s).release());
        // s6 now ends at n2; s7 (n6-n2) is gone with n6; w1's manual route used s7.
        QVERIFY(!model.find<LayoutNode>(u"n6"_s));
        QVERIFY(!model.find<LayoutSegment>(u"s7"_s));
        QCOMPARE(model.find<LayoutSegment>(u"s6"_s)->nodeB, u"n2"_s);
        QVERIFY(model.find<Wire>(u"w1"_s)->routeSegmentIds.empty());
        verifyLoadable(model.harness());
        stack.undo();
        QVERIFY(model.harness() == h);
    }

    void droppingBranchOnPointJoinsIt()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        Harness h = sample();
        // A lone branch point n6 at (100, 300) carrying lamp (n7, (100, 400)).
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Branch, {}, QPointF(100, 300), {}});
        h.layoutNodes.push_back(LayoutNode{u"n7"_s, LayoutNodeType::Connector, u"lamp"_s, QPointF(100, 400), {}});
        h.layoutSegments.push_back(LayoutSegment{u"s6"_s, u"n7"_s, u"n6"_s, 200, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(300, 150);
        HarnessModel *model = document.model();

        // The center of a branch point is clickable even where its dot and boot overlap.
        QVERIFY(view.layoutScene()->nodeItem(u"n6"_s)->shape().contains(QPointF(0, 0)));
        QVERIFY(view.layoutScene()->nodeItem(u"n2"_s)->shape().contains(QPointF(0, 0)));
        // Drag n6 onto branch n2 at (300, 0): they join, lamp's segment now ends at n2.
        const QPoint from = view.mapFromScene(QPointF(100, 300));
        const QPoint to = view.mapFromScene(QPointF(300, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(10, -10));
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QVERIFY(!model->find<LayoutNode>(u"n6"_s));
        QCOMPARE(model->find<LayoutSegment>(u"s6"_s)->nodeB, u"n2"_s);
        verifyLoadable(model->harness());
        // One undo step brings back the separate point where it was.
        document.undoStack()->undo();
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->position, QPointF(100, 300));
        QCOMPARE(model->find<LayoutSegment>(u"s6"_s)->nodeB, u"n6"_s);
    }

    void splitSegmentAtAndDistanceAlongRun()
    {
        HarnessModel model;
        Harness h = sample();
        // A second splice S2 placed as a lone node n6; w1 has a manual route through s2.
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(450, 300), {}});
        h.wires[0].routeSegmentIds = {u"s1"_s, u"s2"_s};
        model.reset(h);
        QUndoStack stack;
        QVERIFY(!splitSegmentAt(&model, u"s2"_s, u"n4"_s, 0.5, {}, {})); // n4 already has segments
        QVERIFY(!splitSegmentAt(&model, u"nope"_s, u"n6"_s, 0.5, {}, {}));
        // s2 (n2-n3, 450 mm) split at 0.2: s2 = n2-n6 450 * 0.2 = 90, new n6-n3 = 360.
        QString newId;
        stack.push(splitSegmentAt(&model, u"s2"_s, u"n6"_s, 0.2, {}, {}, &newId).release());
        const LayoutSegment *first = model.find<LayoutSegment>(u"s2"_s);
        const LayoutSegment *second = model.find<LayoutSegment>(newId);
        QVERIFY(second);
        QCOMPARE(first->nodeB, u"n6"_s);
        QCOMPARE(first->lengthMm, 90.0);
        QCOMPARE(second->nodeA, u"n6"_s);
        QCOMPARE(second->nodeB, u"n3"_s);
        QCOMPARE(second->lengthMm, 360.0);
        QVERIFY(model.find<Wire>(u"w1"_s)->routeSegmentIds.empty());
        verifyLoadable(model.harness());
        // w1 ecu -> sensor still 300 + 90 + 360 = 750 along the bundle: (750 + 20) * 1.05
        // = 808.5 -> 810 mm, as before the split.
        const Harness &split = model.harness();
        QCOMPARE(calc::wireLength(split, calc::LayoutGraph(split), split.wires[0]).cutMm, 810.0);

        // Splice 150 mm from n2: s2 = 150, the other half 450 - 150 = 300.
        QVERIFY(!setDistanceAlongRun(&model, u"n6"_s, u"s2"_s, 450)); // not inside the run
        QVERIFY(!setDistanceAlongRun(&model, u"n6"_s, u"s2"_s, 0));
        QVERIFY(!setDistanceAlongRun(&model, u"n2"_s, u"s2"_s, 100)); // n2 has three segments
        QVERIFY(!setDistanceAlongRun(&model, u"n6"_s, u"s2"_s, 90)); // unchanged
        stack.push(setDistanceAlongRun(&model, u"n6"_s, u"s2"_s, 150).release());
        QCOMPARE(model.find<LayoutSegment>(u"s2"_s)->lengthMm, 150.0);
        QCOMPARE(model.find<LayoutSegment>(newId)->lengthMm, 300.0);
        stack.undo();
        stack.undo();
        QVERIFY(model.harness() == h);
    }

    void splitNearCurveKeepsShape()
    {
        // a (0,0), b (200,0), control (100,100): bend x = 100/200 = 0.5, y = 100/200 = 0.5.
        // Curve midpoint (a + 2c + b)/4 = (100, 50); a drop at (100, 60) is nearest there.
        // Halves by de Casteljau at 0.5: controls (50, 50) and (150, 50).
        // First half (0,0)-(100,50): d (100,50), n (-50,100), |d|^2 12500, v (50,50):
        //   x = (5000 + 2500)/12500 = 0.6, y = (-2500 + 5000)/12500 = 0.2.
        // Second half (100,50)-(200,0): d (100,-50), n (50,100), v (50,0):
        //   x = 5000/12500 = 0.4, y = 2500/12500 = 0.2. Symmetric curve: fraction 0.5.
        const SegmentSplit split = splitNear(QPointF(0, 0), QPointF(200, 0), QPointF(0.5, 0.5), QPointF(100, 60));
        QCOMPARE(split.point, QPointF(100, 50));
        QVERIFY(std::abs(split.fraction - 0.5) < 1e-9);
        QVERIFY(split.bendA && split.bendB);
        QVERIFY(QLineF(*split.bendA, QPointF(0.6, 0.2)).length() < 1e-9);
        QVERIFY(QLineF(*split.bendB, QPointF(0.4, 0.2)).length() < 1e-9);
        // Straight segment: no bends; a drop past an end stays 5 % inside.
        const SegmentSplit end = splitNear(QPointF(0, 0), QPointF(200, 0), std::nullopt, QPointF(-50, 0));
        QVERIFY(!end.bendA && !end.bendB);
        QCOMPARE(end.point, QPointF(10, 0));
        QVERIFY(std::abs(end.fraction - 0.05) < 1e-9);
    }

    void droppingLoneSpliceOnSegmentPutsItInside()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(450, 300), {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(450, 150);
        HarnessModel *model = document.model();
        // Drag n6 onto the middle of s2 (n2 (300,0) - n3 (600,0), 450 mm): halves 225 + 225.
        const QPoint from = view.mapFromScene(QPointF(450, 300));
        const QPoint to = view.mapFromScene(QPointF(450, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(0, -10));
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->position, QPointF(450, 0));
        QCOMPARE(model->find<LayoutSegment>(u"s2"_s)->nodeB, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s2"_s)->lengthMm, 225.0);
        QCOMPARE(model->harness().layoutSegments.size(), size_t(5));
        QCOMPARE(model->harness().layoutSegments.back().lengthMm, 225.0);
        verifyLoadable(model->harness());
        // One undo step puts the splice back where it was, outside the bundle.
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void droppingStubSpliceOnSegmentKeepsStubDistance()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(450, 300), {}});
        // Stub n3 - n6, 50.8 mm (2 in), drawn off s2's far end n3.
        LayoutSegment stub;
        stub.id = u"stub"_s;
        stub.nodeA = u"n3"_s;
        stub.nodeB = u"n6"_s;
        stub.lengthMm = 50.8;
        h.layoutSegments.push_back(stub);
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(450, 150);
        HarnessModel *model = document.model();
        // Drag n6 onto s2 (n2 - n3, 450 mm), dropped at 300 px from n2 (fraction 0.5).
        const QPoint from = view.mapFromScene(QPointF(450, 300));
        const QPoint to = view.mapFromScene(QPointF(450, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(0, -10));
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        // Stub gone; n6 sits 50.8 mm from n3: s2 = n2-n6 450 - 50.8 = 399.2, new n6-n3 = 50.8.
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->harness().layoutSegments.size(), size_t(5));
        QCOMPARE(model->find<LayoutSegment>(u"s2"_s)->nodeB, u"n6"_s);
        QVERIFY(std::abs(model->find<LayoutSegment>(u"s2"_s)->lengthMm - 399.2) < 1e-9);
        const LayoutSegment &added = model->harness().layoutSegments.back();
        QCOMPARE(added.nodeA, u"n6"_s);
        QCOMPARE(added.nodeB, u"n3"_s);
        QCOMPARE(added.lengthMm, 50.8);
        verifyLoadable(model->harness());
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void typingStubLengthPutsSpliceInLoom()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (node n6) drawn as a stub off the ECU (n1), whose only loom is s1
        // (n1 - n2, 300 mm). Wire w4 runs sp2 -> sensor.
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, SpliceRef{u"sp2"_s}, PinRef{u"sensor"_s, u"sensor2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(100, 100), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n1"_s, u"n6"_s, 0, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        HarnessModel *model = document.model();

        // Past the fork at n2 the walk follows w4 (sp2 -> sensor) into s2: 400 - 300 = 100 mm
        // into s2 from n2. 800 mm is past the sensor (300 + 450 = 750): refused.
        const auto beyondFork = loomPositionForStub(model->harness(), u"stub"_s, 400);
        QVERIFY(beyondFork);
        QCOMPARE(beyondFork->segmentId, u"s2"_s);
        QVERIFY(beyondFork->fromNodeA);
        QCOMPARE(beyondFork->offsetMm, 100.0);
        QVERIFY(!loomPositionForStub(model->harness(), u"stub"_s, 800));

        // Typing 50 mm on the stub: sp2 sits in s1 50 mm from the ECU.
        view.editSegmentLength(u"stub"_s);
        QTest::qWait(1);
        QLineEdit *editor = visibleEditor(view.viewport());
        QVERIFY(editor);
        editor->setText(u"50"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->nodeB, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->lengthMm, 50.0); // n1 - n6
        const LayoutSegment rest = model->harness().layoutSegments.back();
        QCOMPARE(rest.nodeA, u"n6"_s);
        QCOMPARE(rest.nodeB, u"n2"_s);
        QCOMPARE(rest.lengthMm, 250.0); // 300 - 50
        // Drawn on the loom, 20 % in from the ECU (50/300 is clamped to 0.2): (60, 0).
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->position, QPointF(60, 0));
        verifyLoadable(model->harness());

        // w4 route: 250 (n6 - n2) + 450 (n2 - n3) = 700 mm, not 50 + 300 + 450 = 800.
        const calc::LayoutGraph graph(model->harness());
        const calc::WireLength length = calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s));
        QCOMPARE(length.baseMm, 700.0);

        // 120 mm walks past n6: s1 is 50, so 70 mm into n6 - n2.
        Harness more = model->harness();
        more.splices.push_back(Splice{u"sp3"_s, u"S3"_s, {}, {}});
        more.wires.push_back(wire(u"w5"_s, SpliceRef{u"sp3"_s}, PinRef{u"sensor"_s, u"sensor2"_s}));
        more.layoutNodes.push_back(LayoutNode{u"n7"_s, LayoutNodeType::Splice, u"sp3"_s, QPointF(100, -100), {}});
        more.layoutSegments.push_back(
            LayoutSegment{u"stub2"_s, u"n1"_s, u"n7"_s, 0, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        model->reset(more);
        const auto past = loomPositionForStub(model->harness(), u"stub2"_s, 120);
        QVERIFY(past);
        QCOMPARE(past->segmentId, rest.id);
        QVERIFY(past->fromNodeA);
        QCOMPARE(past->offsetMm, 70.0);

        // A second splice exactly where sp2 sits (50 mm from the ECU) goes side by side
        // with it: n7 and its stub go away and n6 holds sp3 too.
        const auto same = loomPositionForStub(model->harness(), u"stub2"_s, 50);
        QVERIFY(same);
        QCOMPARE(same->atNodeId, u"n6"_s);
        QVERIFY(view.placeSpliceAlongLoom(u"stub2"_s, 50));
        QVERIFY(!model->find<LayoutNode>(u"n7"_s));
        QVERIFY(!model->find<LayoutSegment>(u"stub2"_s));
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->sharedSpliceIds, std::vector<QString>{u"sp3"_s});
        QCOMPARE(model->harness().layoutSegments.size(), size_t(5)); // s1 s2 s3 s4 + n6-n2
        verifyLoadable(model->harness());
        // w5 sp3 -> sensor routes like w4: 250 + 450 = 700 mm (not 50 + 50 + 250 + 450).
        const calc::LayoutGraph joinedGraph(model->harness());
        QCOMPARE(calc::wireLength(model->harness(), joinedGraph, *model->find<Wire>(u"w5"_s)).baseMm, 700.0);
        // Deleting sp2 leaves sp3 holding the point.
        document.undoStack()->push(removeItems(model, {}, {u"sp2"_s}, {}, {}).release());
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->refId, u"sp3"_s);
        QVERIFY(model->find<LayoutNode>(u"n6"_s)->sharedSpliceIds.empty());
        verifyLoadable(model->harness());
        document.undoStack()->undo();
        document.undoStack()->undo();
        QVERIFY(model->find<LayoutNode>(u"n7"_s));

        // One undo step takes sp2 back out to its stub.
        model->reset(h);
        QVERIFY(view.placeSpliceAlongLoom(u"stub"_s, 50));
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void stubOffForkFollowsSpliceWires()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (node n6) drawn as a stub off the branch n2, which has three looms:
        // s1 (to the ECU, 300 mm), s2 (to the sensor, 450 mm), s3 (to splice n4, 120 mm).
        // w4 ecu:3 -> sp2 and w5 sp2 -> sensor:2 route through the stub and n2.
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, SpliceRef{u"sp2"_s}));
        h.wires.push_back(wire(u"w5"_s, SpliceRef{u"sp2"_s}, PinRef{u"sensor"_s, u"sensor2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(300, -100), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n2"_s, u"n6"_s, 0, {}, {}, SleevingCoverage::Full, std::nullopt, {}});

        // s1 carries one of sp2's wires (w4), s2 one (w5): a tie, so no loom is chosen.
        document.model()->reset(h);
        QVERIFY(!loomPositionForStub(document.model()->harness(), u"stub"_s, 50));

        // w6 sp2 -> sensor:1 makes s2 carry two: 50 mm into s2 from n2 (s2 runs n2 -> n3).
        h.wires.push_back(wire(u"w6"_s, SpliceRef{u"sp2"_s}, PinRef{u"sensor"_s, u"sensor1"_s}));
        document.model()->reset(h);
        QTest::qWait(1);
        HarnessModel *model = document.model();
        const auto position = loomPositionForStub(model->harness(), u"stub"_s, 50);
        QVERIFY(position);
        QCOMPARE(position->segmentId, u"s2"_s);
        QVERIFY(position->fromNodeA);
        QCOMPARE(position->offsetMm, 50.0);

        // Placing it: s2 becomes n2 - n6 (50 mm) and n6 - n3 takes the rest, 450 - 50 = 400.
        QVERIFY(view.placeSpliceAlongLoom(u"stub"_s, 50));
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutSegment>(u"s2"_s)->nodeB, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s2"_s)->lengthMm, 50.0);
        const LayoutSegment &rest = model->harness().layoutSegments.back();
        QCOMPARE(rest.nodeA, u"n6"_s);
        QCOMPARE(rest.nodeB, u"n3"_s);
        QCOMPARE(rest.lengthMm, 400.0);
        verifyLoadable(model->harness());
        // w5 sp2 -> sensor: 400 mm of route (n6 - n3).
        const calc::LayoutGraph graph(model->harness());
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w5"_s)).baseMm, 400.0);
        // w4 ecu -> sp2: 300 + 50 = 350 mm.
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s)).baseMm, 350.0);
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void typingStubLengthToForkPutsSpliceAtFork()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (node n6) on a stub off the ECU (n1); s1 (n1 - n2) is 300 mm and n2 is
        // the fork to the sensor (s2) and splice sp (s3). w4 sp2 -> sensor:2.
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, SpliceRef{u"sp2"_s}, PinRef{u"sensor"_s, u"sensor2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(100, 100), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n1"_s, u"n6"_s, 0, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        HarnessModel *model = document.model();

        // 300 mm from the ECU is exactly the fork n2.
        const auto atFork = loomPositionForStub(model->harness(), u"stub"_s, 300);
        QVERIFY(atFork);
        QCOMPARE(atFork->atNodeId, u"n2"_s);
        QVERIFY(atFork->segmentId.isEmpty());

        // Placing it: n2 and the stub go away, n6 sits at n2's spot and takes s1, s2, s3.
        QVERIFY(view.placeSpliceAlongLoom(u"stub"_s, 300));
        QVERIFY(!model->find<LayoutNode>(u"n2"_s));
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->position, QPointF(300, 0));
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->nodeB, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s2"_s)->nodeA, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s3"_s)->nodeA, u"n6"_s);
        QCOMPARE(model->harness().layoutSegments.size(), size_t(4));
        verifyLoadable(model->harness());
        const calc::LayoutGraph graph(model->harness());
        // w4 sp2 -> sensor: s2 only, 450 mm (not 300 + 450 back through the ECU).
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s)).baseMm, 450.0);
        // w1 ecu -> sensor still 300 + 450 = 750 mm, through the splice point.
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w1"_s)).baseMm, 750.0);
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void droppingStubSpliceOnForkPutsItThere()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (n6) on a 50 mm stub off the sensor (n3); w4 ecu:3 -> sp2.
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, SpliceRef{u"sp2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(600, -150), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n3"_s, u"n6"_s, 50, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(450, 0);
        HarnessModel *model = document.model();
        // Drag n6 onto the fork n2 (300, 0).
        const QPoint from = view.mapFromScene(QPointF(600, -150));
        const QPoint to = view.mapFromScene(QPointF(300, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(0, 10));
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QVERIFY(!model->find<LayoutNode>(u"n2"_s));
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->position, QPointF(300, 0));
        QCOMPARE(model->harness().layoutSegments.size(), size_t(4));
        verifyLoadable(model->harness());
        // w4 ecu -> sp2: s1 only, 300 mm (not 300 + 450 + 50 out to the sensor and back).
        const calc::LayoutGraph graph(model->harness());
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s)).baseMm, 300.0);
        // One undo step for the drag and the placing.
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void droppingStubSpliceOnFarSegmentPutsItInside()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (n6) on a 50 mm stub off the sensor (n3), dropped onto s1 (ECU n1 -
        // fork n2, 300 mm), which does not start at the sensor.
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, SpliceRef{u"sp2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(600, -150), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n3"_s, u"n6"_s, 50, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(300, 0);
        HarnessModel *model = document.model();
        const QPoint from = view.mapFromScene(QPointF(600, -150));
        const QPoint to = view.mapFromScene(QPointF(150, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(0, 10));
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        // Stub gone, s1 split in the middle: n1 - n6 150, n6 - n2 300 - 150 = 150.
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->nodeB, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->lengthMm, 150.0);
        const LayoutSegment &rest = model->harness().layoutSegments.back();
        QCOMPARE(rest.nodeA, u"n6"_s);
        QCOMPARE(rest.nodeB, u"n2"_s);
        QCOMPARE(rest.lengthMm, 150.0);
        verifyLoadable(model->harness());
        // w4 ecu -> sp2: 150 mm.
        const calc::LayoutGraph graph(model->harness());
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s)).baseMm, 150.0);
        // The distance editor opens, since the stub's length is not a distance along s1.
        QTest::qWait(1);
        QVERIFY(visibleEditor(view.viewport()));
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void droppingStubSpliceOnSpliceSitsSideBySide()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (n6) on a stub off the sensor (n3), dropped onto splice sp's point n4
        // (300, 200), at the end of s3 (fork n2 - n4, 120 mm).
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, SpliceRef{u"sp2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(600, 200), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n3"_s, u"n6"_s, 50, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(450, 200);
        HarnessModel *model = document.model();
        const QPoint from = view.mapFromScene(QPointF(600, 200));
        const QPoint to = view.mapFromScene(QPointF(300, 200));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(-10, 0));
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QVERIFY(!model->find<LayoutNode>(u"n6"_s));
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutNode>(u"n4"_s)->sharedSpliceIds, std::vector<QString>{u"sp2"_s});
        verifyLoadable(model->harness());
        // w4 ecu -> sp2: 300 + 120 = 420 mm.
        const calc::LayoutGraph graph(model->harness());
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s)).baseMm, 420.0);
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void segmentDroppedNextToPointJoinsIt()
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
        HarnessModel *model = document.model();
        view.setTool(LayoutView::Tool::Segment);
        // From splice n4 (300, 200) to (497, 186), next to flying lead n5 (500, 200): 14.3 px
        // away, off its 11 px hit circle and its leads (y 195..205). It snaps to (500, 190),
        // one grid step (10) from n5, so the segment ends at n5 instead of adding a point.
        const QPoint from = view.mapFromScene(QPointF(300, 200));
        const QPoint to = view.mapFromScene(QPointF(497, 186));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(model->harness().layoutNodes.size(), size_t(5));
        QCOMPARE(model->harness().layoutSegments.back().nodeB, u"n5"_s);
    }

    void segmentDrawnOntoBundleBranchesOffIt()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        Harness h = sample();
        h.connectors.push_back(connector(u"lamp2"_s, 1));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Connector, u"lamp2"_s, QPointF(150, -150), {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(150, 0);
        HarnessModel *model = document.model();
        view.setTool(LayoutView::Tool::Segment);
        // From n6 (150, -150) onto the middle of s1 (ECU n1 (0,0) - fork n2 (300,0), 300 mm):
        // a new point at (150, 0) splits s1 into 150 + 150, and the drawn segment ends there.
        const QPoint from = view.mapFromScene(QPointF(150, -150));
        const QPoint to = view.mapFromScene(QPointF(150, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        QCOMPARE(model->harness().layoutNodes.size(), size_t(7));
        const LayoutNode &point = model->harness().layoutNodes.back();
        QCOMPARE(point.type, LayoutNodeType::Branch);
        QCOMPARE(point.position, QPointF(150, 0));
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->nodeB, point.id);
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->lengthMm, 150.0);
        const LayoutSegment &drawn = model->harness().layoutSegments.back();
        QCOMPARE(drawn.nodeA, u"n6"_s);
        QCOMPARE(drawn.nodeB, point.id);
        verifyLoadable(model->harness());
        // One undo step.
        QTest::keyClick(visibleEditor(view.viewport()), Qt::Key_Escape);
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
    }

    void spliceLeadDrawnOntoBundlePutsSpliceInside()
    {
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        // Splice sp2 (n6) on a 50 mm stub off the sensor n3; w4 ecu:3 -> sp2. Its lead is
        // drawn onto s1 (ECU n1 - fork n2, 300 mm) between the ECU and the fork.
        Harness h = sample();
        h.splices.push_back(Splice{u"sp2"_s, u"S2"_s, {}, {}});
        h.wires.push_back(wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, SpliceRef{u"sp2"_s}));
        h.layoutNodes.push_back(LayoutNode{u"n6"_s, LayoutNodeType::Splice, u"sp2"_s, QPointF(150, -150), {}});
        h.layoutSegments.push_back(
            LayoutSegment{u"stub"_s, u"n3"_s, u"n6"_s, 50, {}, {}, SleevingCoverage::Full, std::nullopt, {}});
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(150, 0);
        HarnessModel *model = document.model();
        view.setTool(LayoutView::Tool::Segment);
        const QPoint from = view.mapFromScene(QPointF(150, -150));
        const QPoint to = view.mapFromScene(QPointF(150, 0));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, to);
        // No new point or segment: the stub goes, n6 sits in s1 at (150, 0), 150 + 150.
        QCOMPARE(model->harness().layoutNodes.size(), size_t(6));
        QVERIFY(!model->find<LayoutSegment>(u"stub"_s));
        QCOMPARE(model->find<LayoutNode>(u"n6"_s)->position, QPointF(150, 0));
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->nodeB, u"n6"_s);
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->lengthMm, 150.0);
        verifyLoadable(model->harness());
        // w4 ecu -> sp2: 150 mm.
        const calc::LayoutGraph graph(model->harness());
        QCOMPARE(calc::wireLength(model->harness(), graph, *model->find<Wire>(u"w4"_s)).baseMm, 150.0);
        // Its distance along s1 is asked for: typing 2 in (50.8 mm) from the ECU gives
        // n1 - n6 50.8 and n6 - n2 300 - 50.8 = 249.2.
        QTest::qWait(1);
        QLineEdit *editor = visibleEditor(view.viewport());
        QVERIFY(editor);
        editor->setText(u"50.8"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(model->find<LayoutSegment>(u"s1"_s)->lengthMm, 50.8);
        QVERIFY(std::abs(model->harness().layoutSegments.back().lengthMm - 249.2) < 1e-9);
        document.undoStack()->undo();
        document.undoStack()->undo();
        QVERIFY(model->harness() == h);
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
        stack.push(removeItems(&model, {u"sensor"_s}, {u"sp"_s}, {}, {}).release());
        // Nodes keep their place but lose the reference. Flying lead fl stays (only its wire
        // w3 goes with the splice), so its node keeps it.
        QVERIFY(model.find<LayoutNode>(u"n3"_s)->refId.isEmpty());
        QVERIFY(model.find<LayoutNode>(u"n4"_s)->refId.isEmpty());
        QCOMPARE(model.find<LayoutNode>(u"n5"_s)->refId, u"fl"_s);
        verifyLoadable(model.harness());
        stack.undo();
        QVERIFY(model.harness() == h);
        // Deleting the flying lead deletes its wire and clears its node.
        stack.push(removeItems(&model, {}, {}, {u"fl"_s}, {}).release());
        QVERIFY(!model.find<FreeEnd>(u"fl"_s));
        QVERIFY(!model.find<Wire>(u"w3"_s));
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
        stack.push(setWireEnd(&model, u"w1"_s, true, PinRef{u"lamp"_s, u"lamp1"_s}).release());
        QCOMPARE(std::get<PinRef>(model.find<Wire>(u"w1"_s)->endB).connectorId, u"lamp"_s);
        // Pin -> flying lead fl (which now takes two wires).
        stack.push(setWireEnd(&model, u"w1"_s, true, FreeEndRef{u"fl"_s}).release());
        QCOMPARE(std::get<FreeEndRef>(model.find<Wire>(u"w1"_s)->endB).freeEndId, u"fl"_s);
        // Flying lead -> pin: the flying lead, its placement and its layout node stay.
        stack.push(setWireEnd(&model, u"w3"_s, true, PinRef{u"lamp"_s, u"lamp1"_s}).release());
        QVERIFY(model.find<FreeEnd>(u"fl"_s));
        QVERIFY(model.placement(u"fl"_s));
        QCOMPARE(model.find<LayoutNode>(u"n5"_s)->refId, u"fl"_s);
        verifyLoadable(model.harness());
        // No-ops: onto the same pin, or onto the wire's other end.
        QVERIFY(!setWireEnd(&model, u"w2"_s, false, PinRef{u"ecu"_s, u"ecu2"_s}));
        QVERIFY(!setWireEnd(&model, u"w2"_s, false, SpliceRef{u"sp"_s}));

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

        // Drag w3's end off flying lead fl onto sensor:2; fl stays.
        Selection w3Only;
        w3Only.wires.insert(u"w3"_s);
        selection.setSelection(w3Only);
        QVERIFY(view.schematicScene()->freeEndItem(u"fl"_s));
        ConnectorItem *sensor = view.schematicScene()->connectorItem(u"sensor"_s);
        const QPoint from =
            view.mapFromScene(view.schematicScene()->endAnchor(*document.model()->find<Wire>(u"w3"_s), true).point);
        const QPoint onPin = view.mapFromScene(sensor->mapToScene(sensor->circuitCellRect(1).center()));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view.viewport(), from + QPoint(20, 0));
        QTest::mouseMove(view.viewport(), onPin);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, onPin);
        QCOMPARE(std::get<PinRef>(document.model()->find<Wire>(u"w3"_s)->endB).pinId, u"sensor2"_s);
        QVERIFY(document.model()->find<FreeEnd>(u"fl"_s));
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

    // Writes layout.png for visual review when TBH_SCREENSHOT_DIR is set. Renders the
    // harness file named by TBH_SCREENSHOT_HARNESS, or the sample.
    void screenshot()
    {
        const QString dirPath = qEnvironmentVariable("TBH_SCREENSHOT_DIR");
        if (dirPath.isEmpty())
            QSKIP("TBH_SCREENSHOT_DIR not set");
        Harness h = sample();
        if (const QString file = qEnvironmentVariable("TBH_SCREENSHOT_HARNESS"); !file.isEmpty()) {
            QString error;
            auto loaded = io::readTbhFile(file, &error);
            QVERIFY2(loaded, qPrintable(error));
            h = *loaded;
        }
        Theme theme;
        theme.setMode(Theme::Mode::Dark);
        Document document;
        SelectionModel selection(document.model());
        LayoutView view(&document, &selection);
        view.resize(1400, 1000);
        view.show();
        document.model()->reset(h);
        QTest::qWait(20);
        view.zoomToFit();
        QTest::qWait(20);
        view.grab().save(dirPath + u"/layout.png"_s);
    }
};

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
