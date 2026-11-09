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
    void zoomToFit();

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

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    bool m_panning = false;
    QPoint m_panLast;
    InlineEdit *m_editor = nullptr;
};

} // namespace tbh::app
