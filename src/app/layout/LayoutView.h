// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Queries.h"
#include "ui/CanvasView.h"

#include <QSet>

#include <map>

class QGraphicsLineItem;
class QListWidget;

namespace tbh::app {

class Document;
class LayoutNodeItem;
class LayoutSegmentItem;
class LayoutScene;
class SelectionModel;

// Interactive layout editor. Select tool: move nodes and notes, select, double-click a
// segment to type its length (or a note to edit it), drag a selected segment's middle
// handle to curve it, drag a node's lead (the short dash on either side) to draw a segment.
// Segment tool: drag from anywhere on a node. A segment goes to another node, or to empty
// space, which adds a branch point; its length editor opens at once. Dropping a branch
// point onto another point (or a point onto a branch point) joins the two.
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
    // Opens a text editor near the view center; typing text and pressing Enter adds a note.
    void addNote();
    // Edits a note's text in place; clearing the text deletes the note.
    void editNote(const QString &noteId);
    void straightenSegment(const QString &segmentId);
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
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool nudgeSelection(const QPointF &delta) override;

private:
    LayoutNodeItem *nodeAt(const QPoint &viewPos) const;
    // Nearest node to `scenePos`, skipping `exclude`, within `toleranceGrid` grid steps or
    // `tolerancePixels` screen pixels (whichever is farther), or null.
    LayoutNodeItem *nodeNear(const QPointF &scenePos, const QSet<QString> &exclude, qreal toleranceGrid = 1,
                             qreal tolerancePixels = 8) const;
    // The node whose lead is under `viewPos`, or null.
    LayoutNodeItem *nodeLeadAt(const QPoint &viewPos) const;
    void setHoveredLead(LayoutNodeItem *node);
    QPointF nextPlacePosition();
    // Selected segment whose bend handle is under `viewPos`, or null.
    LayoutSegmentItem *bendHandleAt(const QPoint &viewPos) const;
    // Bend that puts the segment's midpoint at `scenePos`.
    std::optional<QPointF> bendFor(const LayoutSegment &segment, const QPointF &scenePos) const;
    void cancelBend();
    void recordPressPositions();
    void finishSegment(const QPoint &viewPos);
    void cancelSegment();
    void commitMoves();

    Document *m_document;
    SelectionModel *m_selection;
    LayoutScene *m_scene;
    Tool m_tool = Tool::Select;
    QString m_segmentStart; // node id while drawing a segment
    QGraphicsLineItem *m_preview = nullptr;
    QString m_hoveredLead; // node id whose lead is under the pointer
    QString m_bendSegment; // segment id while its bend handle is dragged
    std::map<QString, QPointF> m_pressPositions;     // selected nodes at mouse press
    std::map<QString, QPointF> m_notePressPositions; // selected notes at mouse press
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
