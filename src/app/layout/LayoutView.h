// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Queries.h"
#include "ui/CanvasView.h"

#include <QSet>
#include <QUndoCommand>

#include <map>
#include <memory>

class QGraphicsLineItem;
class QListWidget;
class QListWidgetItem;

namespace tbh::app {

class Document;
class LayoutNodeItem;
class LayoutSegmentItem;
class LayoutScene;
class SelectionModel;

// Interactive layout editor. Select tool: move nodes and notes, select, double-click a
// segment to type its length (or a note to edit it), drag a selected segment's middle
// handle to curve it, drag a node's lead (the short dash on either side) to draw a segment.
// Segment tool: drag from anywhere on a node. A segment goes to another node, to empty
// space, which adds a branch point, or onto a bundle, which adds a branch point inside it;
// its length editor opens at once. Drawn from a splice's lead onto a bundle, the splice
// itself goes inside the bundle (as when dropped there). Dropping a branch
// point onto another point (or a point onto a branch point) joins the two. Dropping a
// splice that has no segments, or only a stub off a loom point, onto a segment puts it
// inside that bundle (the segment is split there, the stub removed); double-click it to
// type its distance along the bundle. Dropped onto a bend or fork it takes that point's
// place; dropped onto another splice it sits side by side with it.
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
    // Adds a node standing for each ref near the view center, each a little offset from the
    // last so none hide under another. One undo step; the new nodes are selected.
    void placeRefs(const std::vector<LayoutRef> &refs);
    void deleteSelection();
    void selectAll();
    void editSegmentLength(const QString &segmentId);
    // A splice drawn as a stub off a point on a loom goes inside that loom, `distanceMm`
    // from the point (loomPositionForStub()); the stub is removed. One undo step. False,
    // with nothing changed, when the stub does not qualify.
    bool placeSpliceAlongLoom(const QString &stubId, double distanceMm);
    // Puts splice node `spliceNodeId` on loom point `pointId`: a bend or fork, which the
    // splice node replaces (it moves there and takes the point's segments), or another
    // splice's point, which then holds this splice too (its own node goes away). The splice
    // node must have no segments, or only a stub off a loom point (stubOfSplice()), which is
    // removed. One undo step, with `before` (e.g. the drag that dropped it) first. False,
    // with nothing changed and `before` kept, when the splice or point does not qualify.
    bool placeSpliceOnPoint(const QString &spliceNodeId, const QString &pointId,
                            std::unique_ptr<QUndoCommand> *before = nullptr);
    // For a splice inside a bundle (on exactly two segments): types its distance from the
    // far end of its first segment; the bundle's total length stays the same.
    void editPositionAlongRun(const QString &nodeId);

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
    // Segment drawn under `scenePos`, or empty. Segments in `skip`, or with an end in it,
    // are passed over.
    QString segmentAt(const QPointF &scenePos, const QSet<QString> &skip) const;
    // Puts splice node `spliceNodeId` inside segment `segmentId` at the point nearest
    // `scenePos` (the segment is split there). The splice must have no segments, or only a
    // stub off a loom point (stubOfSplice()), which is removed; when the segment starts where
    // the stub did, the stub's length is the splice's distance along it, otherwise the
    // distance is asked for. One undo step, with `before` first. False, with nothing changed
    // and `before` kept, when the splice does not qualify.
    bool placeSpliceInSegment(const QString &spliceNodeId, const QString &segmentId, const QPointF &scenePos,
                              std::unique_ptr<QUndoCommand> *before = nullptr);

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
    void placeSelected();
    void placeAll();
    void placeItems(const QList<QListWidgetItem *> &items);

    Document *m_document;
    LayoutView *m_view;
    QListWidget *m_unplaced;
    bool m_refreshPending = false;
};

} // namespace tbh::app
