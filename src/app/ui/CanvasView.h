// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsView>

#include <functional>

class QTouchEvent;

namespace tbh::app {

class InlineEdit;

inline constexpr qreal kCanvasGrid = 10;
// Nearest grid point.
QPointF snapToCanvasGrid(const QPointF &point);
// Background and grid lines shared by the editors' scenes.
void drawCanvasBackground(QPainter *painter, const QRectF &rect, const QPalette &palette);

// Shared canvas behaviour for the schematic and layout editors: wheel and pinch zoom around
// the cursor, panning (middle drag, plus right drag or Space + left drag and wheel panning as
// set in Preferences), zoom to fit, and a one-line text editor laid over the canvas.
// Panning is handled before the subclasses see the mouse events.
// Touchscreens: one finger acts as the left mouse button (tap = click, double tap = double
// click, drag = drag); two fingers pinch zoom and pan together.
class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget *parent = nullptr);

    void zoomIn();
    void zoomOut();
    // Fits the visible drawing in the view.
    void zoomToFit();
    // Fits the drawing once the view is shown at its real size (now, if it is showing),
    // and again whenever the view is resized, until the user zooms, pans or clicks in it.
    // For a newly opened harness or sheet.
    void fitWhenShown();

protected:
    // How the inline editor was closed: Enter = Down, Shift+Enter = Up, Tab = Next,
    // Shift+Tab = Previous, focus loss = None. Esc cancels without a commit.
    enum class Nav { None, Down, Up, Next, Previous };

    // `suggestions`, if any, are offered in a list while typing (all of them when the text
    // starts empty).
    void editText(const QRectF &sceneRect, const QString &text,
                  std::function<void(const QString &, Nav)> commit, const QStringList &suggestions = {});
    void closeEditor();
    // Lets subclasses use the wheel over an item (e.g. scroll rows). Return true if handled.
    virtual bool wheelOverItem(QWheelEvent *) { return false; }
    // Moves the selected items by `delta` as one undoable step. Return false if nothing
    // movable is selected (the arrow key then scrolls the view).
    virtual bool nudgeSelection(const QPointF &) { return false; }
    // Keyboard navigation: selects the next/previous selectable item in reading order.
    void selectAdjacentItem(bool next);
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool event(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitLater();
    void zoomBy(qreal factor);
    // Pan handling for viewportEvent(). Return true if the event was used for panning.
    bool panMouseEvent(QMouseEvent *event);
    // Touchscreen handling for viewportEvent(). Returns whether the event was used.
    bool handleTouch(QTouchEvent *event);
    // Sends a left-button mouse event at viewport position `pos`, as a touch emulates it.
    void sendTouchMouse(QEvent::Type type, QPointF pos);
    // Zooms by `factor` so the scene point under `from` ends up under `to` (viewport positions).
    void pinchTo(QPointF from, QPointF to, qreal factor);
    void setPanCursor(Qt::CursorShape shape);
    void restoreCursor();

    bool m_fitPending = false;
    // Button that started the current pan; Qt::NoButton when not panning.
    Qt::MouseButton m_panButton = Qt::NoButton;
    bool m_panMoved = false;
    QPoint m_panPress;
    QPoint m_panLast;
    bool m_spaceHeld = false;
    bool m_cursorSaved = false;
    QCursor m_savedCursor;
    InlineEdit *m_editor = nullptr;

    // One touch sequence (first finger down to last finger up). A single finger is held as
    // Pending until it moves or lifts, so a second finger can still make it a pinch without
    // having started a click or drag.
    enum class TouchMode { None, Pending, Mouse, Pinch };
    TouchMode m_touchMode = TouchMode::None;
    int m_touchId = -1; // first finger
    QPointF m_touchStart;
    QPointF m_touchLast;
    int m_pinchIds[2] = {-1, -1};
    qreal m_pinchDistance = 0;
    QPointF m_pinchMid;
    QElapsedTimer m_lastTap;
    QPointF m_lastTapPos;
};

} // namespace tbh::app
