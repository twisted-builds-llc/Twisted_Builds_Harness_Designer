// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QGraphicsView>

#include <functional>

namespace tbh::app {

class InlineEdit;

inline constexpr qreal kCanvasGrid = 10;
// Nearest grid point.
QPointF snapToCanvasGrid(const QPointF &point);
// Background and grid lines shared by the editors' scenes.
void drawCanvasBackground(QPainter *painter, const QRectF &rect, const QPalette &palette);

// Shared canvas behaviour for the schematic and layout editors: wheel zoom around the
// cursor, middle-button panning, zoom to fit, and a one-line text editor laid over the
// canvas.
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

    void editText(const QRectF &sceneRect, const QString &text,
                  std::function<void(const QString &, Nav)> commit);
    void closeEditor();
    bool isPanning() const { return m_panning; }
    // Lets subclasses use the wheel over an item (e.g. scroll rows). Return true if handled.
    virtual bool wheelOverItem(QWheelEvent *) { return false; }
    // Moves the selected items by `delta` as one undoable step. Return false if nothing
    // movable is selected (the arrow key then scrolls the view).
    virtual bool nudgeSelection(const QPointF &) { return false; }
    // Keyboard navigation: selects the next/previous selectable item in reading order.
    void selectAdjacentItem(bool next);
    void keyPressEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool event(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitLater();

    bool m_fitPending = false;
    bool m_panning = false;
    QPoint m_panLast;
    InlineEdit *m_editor = nullptr;
};

} // namespace tbh::app
