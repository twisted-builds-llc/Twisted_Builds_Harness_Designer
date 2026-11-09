// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Queries.h"
#include "ui/CanvasView.h"

#include <map>

class QGraphicsLineItem;
class QListWidget;

namespace tbh::app {

class Document;
class LayoutNodeItem;
class LayoutScene;
class SelectionModel;

// Interactive layout editor. Select tool: move nodes, select, double-click a segment to
// type its length. Segment tool: drag from a node to another node (or to empty space,
// which adds a branch point) to create a segment; its length editor opens at once.
class LayoutView : public CanvasView
{
    Q_OBJECT

public:
    enum class Tool { Select, Segment };

    LayoutView(Document *document, SelectionModel *selection, QWidget *parent = nullptr);

    LayoutScene *layoutScene() const { return m_scene; }
    void setTool(Tool tool);
    void setSnapToGrid(bool on);

    void addBranchPoint();
    // Adds a node standing for `ref` near the view center.
    void placeRef(const LayoutRef &ref);
    void deleteSelection();
    void selectAll();
    void editSegmentLength(const QString &segmentId);

signals:
    void statusMessage(const QString &message);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    LayoutNodeItem *nodeAt(const QPoint &viewPos) const;
    QPointF nextPlacePosition();
    void finishSegment(const QPoint &viewPos);
    void cancelSegment();
    void commitMoves();

    Document *m_document;
    SelectionModel *m_selection;
    LayoutScene *m_scene;
    Tool m_tool = Tool::Select;
    QString m_segmentStart; // node id while drawing a segment
    QGraphicsLineItem *m_preview = nullptr;
    std::map<QString, QPointF> m_pressPositions;
    int m_placeCount = 0;
};

// The Layout tab: a list of items not yet placed in the layout next to the canvas.
class LayoutEditor : public QWidget
{
    Q_OBJECT

public:
    LayoutEditor(Document *document, SelectionModel *selection, QWidget *parent = nullptr);
    LayoutView *view() const { return m_view; }

private:
    void refreshUnplaced();
    void placeCurrent();

    Document *m_document;
    LayoutView *m_view;
    QListWidget *m_unplaced;
    bool m_refreshPending = false;
};

} // namespace tbh::app
