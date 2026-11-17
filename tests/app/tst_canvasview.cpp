// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"
#include "ui/CanvasView.h"

#include <QContextMenuEvent>
#include <QGraphicsScene>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace tbh::app;
using namespace Qt::StringLiterals;
namespace prefs = tbh::app::preferences;

namespace {

// Counts what reaches the subclass, as the schematic and layout views would see it.
class TestView : public CanvasView
{
public:
    int menus = 0;
    int leftPresses = 0;
    int doubleClicks = 0;
    QPoint lastRelease;

protected:
    void contextMenuEvent(QContextMenuEvent *) override { ++menus; }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            ++leftPresses;
        CanvasView::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        ++doubleClicks;
        CanvasView::mouseDoubleClickEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        lastRelease = event->position().toPoint();
        CanvasView::mouseReleaseEvent(event);
    }
};

} // namespace

class TestCanvasView : public QObject
{
    Q_OBJECT

private:
    QGraphicsScene *m_scene = nullptr;
    TestView *m_view = nullptr;
    QPointingDevice *m_touchScreen = QTest::createTouchDevice();

    QWidget *viewport() const { return m_view->viewport(); }
    int h() const { return m_view->horizontalScrollBar()->value(); }
    int v() const { return m_view->verticalScrollBar()->value(); }

    void mouse(QEvent::Type type, Qt::MouseButton button, QPoint pos)
    {
        const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button);
        QMouseEvent event(type, pos, viewport()->mapToGlobal(pos), type == QEvent::MouseMove ? Qt::NoButton : button,
                          buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport(), &event);
    }

    // Press at (100, 100), move to (60, 70), release there.
    void drag(Qt::MouseButton button)
    {
        mouse(QEvent::MouseButtonPress, button, {100, 100});
        mouse(QEvent::MouseMove, button, {60, 70});
        mouse(QEvent::MouseButtonRelease, button, {60, 70});
    }

    void wheel(QPoint pixelDelta, QPoint angleDelta, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWheelEvent event(QPointF(150, 150), viewport()->mapToGlobal(QPointF(150, 150)), pixelDelta, angleDelta,
                          Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(viewport(), &event);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_canvasview"_s);
    }

    void init()
    {
        QSettings().clear();
        m_scene = new QGraphicsScene(0, 0, 5000, 5000);
        m_view = new TestView;
        m_view->setScene(m_scene);
        m_view->resize(300, 300);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view));
        m_view->horizontalScrollBar()->setValue(1000);
        m_view->verticalScrollBar()->setValue(1000);
    }

    void cleanup()
    {
        delete m_view;
        delete m_scene;
    }

    void middleDragPansByDefault()
    {
        QCOMPARE(prefs::panInput(), prefs::PanInput::MiddleDrag);
        drag(Qt::MiddleButton);
        // Dragged by (-40, -30): the content follows the pointer, so the view scrolls by +40, +30.
        QCOMPARE(h(), 1040);
        QCOMPARE(v(), 1030);
        // Right-click menus come from the platform as before.
        QContextMenuEvent menu(QContextMenuEvent::Mouse, QPoint(10, 10), viewport()->mapToGlobal(QPoint(10, 10)));
        QCoreApplication::sendEvent(viewport(), &menu);
        QCOMPARE(m_view->menus, 1);
    }

    void rightDragPansAndRightClickOpensMenu()
    {
        prefs::setPanInput(prefs::PanInput::RightDrag);
        // The platform's own context menu event is held back...
        QContextMenuEvent platformMenu(QContextMenuEvent::Mouse, QPoint(100, 100), viewport()->mapToGlobal(QPoint(100, 100)));
        QCoreApplication::sendEvent(viewport(), &platformMenu);
        QCOMPARE(m_view->menus, 0);
        // ...a drag pans without a menu...
        drag(Qt::RightButton);
        QCOMPARE(h(), 1040);
        QCOMPARE(v(), 1030);
        QCOMPARE(m_view->menus, 0);
        // ...and a click (moving less than the drag distance) opens it without panning.
        mouse(QEvent::MouseButtonPress, Qt::RightButton, {100, 100});
        mouse(QEvent::MouseMove, Qt::RightButton, {101, 100});
        mouse(QEvent::MouseButtonRelease, Qt::RightButton, {101, 100});
        QCOMPARE(m_view->menus, 1);
        QCOMPARE(h(), 1040);
        // The keyboard menu key still works.
        QContextMenuEvent keyMenu(QContextMenuEvent::Keyboard, QPoint(10, 10), viewport()->mapToGlobal(QPoint(10, 10)));
        QCoreApplication::sendEvent(viewport(), &keyMenu);
        QCOMPARE(m_view->menus, 2);
    }

    void spaceLeftDragPans()
    {
        prefs::setPanInput(prefs::PanInput::SpaceDrag);
        m_view->setFocus();
        QTest::keyPress(m_view, Qt::Key_Space);
        QCOMPARE(viewport()->cursor().shape(), Qt::OpenHandCursor);
        drag(Qt::LeftButton);
        QCOMPARE(h(), 1040);
        QCOMPARE(v(), 1030);
        QCOMPARE(m_view->leftPresses, 0); // no selection or drawing started
        QTest::keyRelease(m_view, Qt::Key_Space);
        QCOMPARE(viewport()->cursor().shape(), Qt::ArrowCursor);
        // Without Space the left button selects/draws as usual.
        mouse(QEvent::MouseButtonPress, Qt::LeftButton, {100, 100});
        mouse(QEvent::MouseButtonRelease, Qt::LeftButton, {100, 100});
        QCOMPARE(m_view->leftPresses, 1);
        QCOMPARE(h(), 1040);
    }

    void spaceDoesNotPanWhenNotChosen()
    {
        m_view->setFocus();
        QTest::keyPress(m_view, Qt::Key_Space);
        drag(Qt::LeftButton);
        QCOMPARE(m_view->leftPresses, 1);
        QCOMPARE(h(), 1000);
        QTest::keyRelease(m_view, Qt::Key_Space);
    }

    void wheelZoomsByDefault()
    {
        wheel({}, {0, 120});
        // One notch: factor 1.15.
        QCOMPARE(m_view->transform().m11(), 1.15);
    }

    void wheelPansWhenChosen()
    {
        prefs::setWheelPans(true);
        // Touchpad: pixel delta (-20, -30) scrolls the view by +20, +30.
        wheel({-20, -30}, {-40, -60});
        QCOMPARE(h(), 1020);
        QCOMPARE(v(), 1030);
        QCOMPARE(m_view->transform().m11(), 1.0);
        // Mouse wheel (no pixel delta): one notch down (-120) scrolls 120 / 2 = 60 pixels.
        wheel({}, {0, -120});
        QCOMPARE(v(), 1090);
        // Ctrl+wheel zooms.
        wheel({}, {0, 120}, Qt::ControlModifier);
        QCOMPARE(m_view->transform().m11(), 1.15);
    }

    void pinchZooms()
    {
        QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2,
                                  QPointF(150, 150), QPointF(150, 150), viewport()->mapToGlobal(QPointF(150, 150)),
                                  0.25, QPointF());
        QCoreApplication::sendEvent(viewport(), &pinch);
        // Value 0.25: factor 1 + 0.25.
        QCOMPARE(m_view->transform().m11(), 1.25);
    }

    void touchTapClicksAndDoubleTapDoubleClicks()
    {
        QTest::touchEvent(viewport(), m_touchScreen).press(0, {100, 100}, viewport());
        QCOMPARE(m_view->leftPresses, 0); // held back: a second finger could still pinch
        QTest::touchEvent(viewport(), m_touchScreen).release(0, {100, 100}, viewport());
        QCOMPARE(m_view->leftPresses, 1);
        QCOMPARE(m_view->lastRelease, QPoint(100, 100));
        QTest::touchEvent(viewport(), m_touchScreen).press(0, {102, 101}, viewport());
        QTest::touchEvent(viewport(), m_touchScreen).release(0, {102, 101}, viewport());
        QCOMPARE(m_view->doubleClicks, 1);
        QCOMPARE(h(), 1000);
        QCOMPARE(m_view->transform().m11(), 1.0);
    }

    void touchDragActsLikeLeftDrag()
    {
        QTest::touchEvent(viewport(), m_touchScreen).press(0, {100, 100}, viewport());
        QTest::touchEvent(viewport(), m_touchScreen).move(0, {60, 70}, viewport());
        // Moved past the drag distance: the press is sent where the finger went down.
        QCOMPARE(m_view->leftPresses, 1);
        // A second finger during a drag does not turn it into a pinch.
        QTest::touchEvent(viewport(), m_touchScreen).move(0, {60, 70}, viewport()).press(1, {200, 200}, viewport());
        QTest::touchEvent(viewport(), m_touchScreen).move(0, {50, 60}, viewport()).move(1, {250, 250}, viewport());
        QCOMPARE(m_view->transform().m11(), 1.0);
        QTest::touchEvent(viewport(), m_touchScreen).release(0, {50, 60}, viewport()).release(1, {250, 250}, viewport());
        QCOMPARE(m_view->lastRelease, QPoint(50, 60));
        QCOMPARE(h(), 1000);
        QCOMPARE(v(), 1000);
    }

    void twoFingersPinchZoomAndPan()
    {
        QTest::touchEvent(viewport(), m_touchScreen).press(0, {100, 150}, viewport());
        QTest::touchEvent(viewport(), m_touchScreen).move(0, {100, 150}, viewport()).press(1, {200, 150}, viewport());
        // Fingers spread from 100 to 200 pixels apart around the same midpoint (150, 150):
        // factor 200 / 100 = 2. Scene point under (150, 150) was 1000 + 150 = 1150; at scale 2
        // it is at 2300 in view coordinates, so it stays under (150, 150) with scroll 2300 - 150.
        QTest::touchEvent(viewport(), m_touchScreen).move(0, {50, 150}, viewport()).move(1, {250, 150}, viewport());
        QCOMPARE(m_view->transform().m11(), 2.0);
        QCOMPARE(h(), 2150);
        QCOMPARE(v(), 2150);
        // Both fingers move by (-40, -30), still 200 apart: no zoom, content follows: +40, +30.
        QTest::touchEvent(viewport(), m_touchScreen).move(0, {10, 120}, viewport()).move(1, {210, 120}, viewport());
        QCOMPARE(m_view->transform().m11(), 2.0);
        QCOMPARE(h(), 2190);
        QCOMPARE(v(), 2180);
        QTest::touchEvent(viewport(), m_touchScreen).release(0, {10, 120}, viewport()).release(1, {210, 120}, viewport());
        QCOMPARE(m_view->leftPresses, 0); // no click, selection or drawing
        QCOMPARE(m_view->doubleClicks, 0);
    }
};

QTEST_MAIN(TestCanvasView)
#include "tst_canvasview.moc"
