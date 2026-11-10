// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout/LayoutView.h"

#include "Document.h"
#include "SelectionModel.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "layout/LayoutScene.h"
#include "ui/Format.h"

#include <QGraphicsLineItem>
#include <QLineF>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QContextMenuEvent>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace tbh::app {

LayoutView::LayoutView(Document *document, SelectionModel *selection, QWidget *parent)
    : CanvasView(parent)
    , m_document(document)
    , m_selection(selection)
    , m_scene(new LayoutScene(document->model(), selection, this))
{
    setScene(m_scene);
    setAccessibleName(tr("Layout editor"));
    setAccessibleDescription(tr("Branch points and segments of the physical harness. Press ] or [ to move "
                                "between items, arrow keys to move points, Delete to delete."));
    centerOn(0, 0);
    connect(document->model(), &HarnessModel::harnessReset, this, [this] {
        closeEditor();
        cancelSegment();
        m_placeCount = 0;
        QTimer::singleShot(0, this, &LayoutView::zoomToFit);
    });
}

void LayoutView::setTool(Tool tool)
{
    m_tool = tool;
    cancelSegment();
    viewport()->setCursor(tool == Tool::Segment ? Qt::CrossCursor : Qt::ArrowCursor);
}

void LayoutView::setSnapToGrid(bool on)
{
    m_scene->setSnapToGrid(on);
}

LayoutNodeItem *LayoutView::nodeAt(const QPoint &viewPos) const
{
    for (QGraphicsItem *item : items(viewPos)) {
        if (item->type() == LayoutNodeItemType)
            return static_cast<LayoutNodeItem *>(item);
    }
    return nullptr;
}

LayoutNodeItem *LayoutView::nodeNear(const QPointF &scenePos, const QSet<QString> &exclude,
                                     qreal toleranceGrid, qreal tolerancePixels) const
{
    const qreal tolerance = std::max(toleranceGrid * kCanvasGrid, tolerancePixels / std::max(transform().m11(), 1e-6));
    LayoutNodeItem *best = nullptr;
    qreal bestDistance = tolerance;
    for (const LayoutNode &node : m_document->model()->harness().layoutNodes) {
        if (exclude.contains(node.id))
            continue;
        LayoutNodeItem *item = m_scene->nodeItem(node.id);
        if (!item)
            continue;
        const qreal distance = QLineF(item->pos(), scenePos).length();
        if (distance <= bestDistance) {
            best = item;
            bestDistance = distance;
        }
    }
    return best;
}

LayoutNodeItem *LayoutView::nodeLeadAt(const QPoint &viewPos) const
{
    LayoutNodeItem *node = nodeAt(viewPos);
    return node && node->isStubAt(node->mapFromScene(mapToScene(viewPos))) ? node : nullptr;
}

void LayoutView::setHoveredLead(LayoutNodeItem *node)
{
    const QString id = node ? node->id() : QString();
    if (id == m_hoveredLead)
        return;
    if (LayoutNodeItem *old = m_scene->nodeItem(m_hoveredLead))
        old->setStubHovered(false);
    m_hoveredLead = id;
    if (node)
        node->setStubHovered(true);
    if (m_tool == Tool::Select)
        viewport()->setCursor(node ? Qt::CrossCursor : Qt::ArrowCursor);
}

QPointF LayoutView::nextPlacePosition()
{
    const QPointF center = mapToScene(viewport()->rect().center());
    const QPointF offset(30 * (m_placeCount % 8), 30 * (m_placeCount % 8));
    ++m_placeCount;
    return snapToCanvasGrid(center + offset);
}

void LayoutView::addBranchPoint()
{
    LayoutNode node{newId(), LayoutNodeType::Branch, {}, nextPlacePosition(), {}};
    const QString id = node.id;
    m_document->undoStack()->push(
        new AddEntityCommand<LayoutNode>(m_document->model(), std::move(node), {}, tr("Add branch point")));
    Selection s;
    s.layoutNodes.insert(id);
    m_selection->setSelection(s);
}

void LayoutView::placeRef(const LayoutRef &ref)
{
    LayoutNode node{newId(), ref.nodeType, ref.refId, nextPlacePosition(), {}};
    const QString id = node.id;
    m_document->undoStack()->push(
        new AddEntityCommand<LayoutNode>(m_document->model(), std::move(node), {}, tr("Place in layout")));
    Selection s;
    s.layoutNodes.insert(id);
    m_selection->setSelection(s);
}

void LayoutView::addNote()
{
    const QPointF position = nextPlacePosition();
    editText(QRectF(position, QSizeF(NoteItem::kWidth, 18)), {}, [this, position](const QString &value, Nav) {
        const QString text = value.trimmed();
        if (text.isEmpty())
            return; // nothing typed: no note
        Note note{newId(), NoteCanvas::Layout, text, position, {}};
        const QString id = note.id;
        m_document->undoStack()->push(
            new AddEntityCommand<Note>(m_document->model(), std::move(note), {}, tr("Add note")));
        Selection s;
        s.notes.insert(id);
        m_selection->setSelection(s);
    });
}

void LayoutView::editNote(const QString &noteId)
{
    NoteItem *item = m_scene->noteItem(noteId);
    if (!item)
        return;
    const QRectF rect(item->pos(), QSizeF(NoteItem::kWidth, 18));
    editText(rect, item->note().text, [this, noteId](const QString &value, Nav) {
        if (auto command = setNoteText(m_document->model(), noteId, value)) // empty text deletes
            m_document->undoStack()->push(command.release());
    });
}

void LayoutView::straightenSegment(const QString &segmentId)
{
    HarnessModel *model = m_document->model();
    const LayoutSegment *current = model->find<LayoutSegment>(segmentId);
    if (!current || !current->bend)
        return;
    LayoutSegment updated = *current;
    updated.bend.reset();
    m_document->undoStack()->push(new UpdateEntityCommand<LayoutSegment>(model, updated, tr("Straighten segment")));
}

void LayoutView::deleteSelection()
{
    const Selection &s = m_selection->selection();
    if (s.layoutNodes.isEmpty() && s.layoutSegments.isEmpty() && s.notes.isEmpty())
        return;
    m_document->undoStack()->push(
        removeLayoutItems(m_document->model(), s.layoutNodes, s.layoutSegments, s.notes).release());
}

void LayoutView::selectAll()
{
    for (QGraphicsItem *item : m_scene->items()) {
        if (item->flags() & QGraphicsItem::ItemIsSelectable)
            item->setSelected(true);
    }
}

void LayoutView::editSegmentLength(const QString &segmentId)
{
    LayoutSegmentItem *item = m_scene->segmentItem(segmentId);
    if (!item)
        return;
    const Units units = m_document->model()->harness().info.units;
    const LayoutSegment &segment = item->segment();
    const QString text = segment.lengthMm > 0 ? formatLength(segment.lengthMm, units, false) : QString();
    editText(QRectF(item->midpoint() - QPointF(46, 17), QSizeF(92, 18)), text,
             [this, segmentId, units](const QString &value, Nav) {
                 HarnessModel *model = m_document->model();
                 const LayoutSegment *current = model->find<LayoutSegment>(segmentId);
                 if (!current)
                     return;
                 if (value.trimmed().isEmpty() && current->lengthMm <= 0)
                     return;
                 const auto mm = parseLength(value, units);
                 if (!mm) {
                     emit statusMessage(tr("\"%1\" is not a length in %2.").arg(value, unitsSymbol(units)));
                     return;
                 }
                 // A length typed on a splice's stub off a loom puts the splice in the loom.
                 if (placeSpliceAlongLoom(segmentId, *mm))
                     return;
                 if (!spliceOfStub(model->harness(), segmentId).isEmpty())
                     emit statusMessage(tr("The splice stays on its own branch: it goes inside the loom only from "
                                           "a point with one loom, at a distance inside it."));
                 LayoutSegment updated = *current;
                 // Keep the stored value if the shown (rounded) text was not changed.
                 if (value == formatLength(current->lengthMm, units, false))
                     return;
                 updated.lengthMm = *mm;
                 m_document->undoStack()->push(
                     new UpdateEntityCommand<LayoutSegment>(model, updated, tr("Set segment length")));
             });
}

bool LayoutView::placeSpliceAlongLoom(const QString &stubId, double distanceMm)
{
    HarnessModel *model = m_document->model();
    const auto position = loomPositionForStub(model->harness(), stubId, distanceMm);
    const LayoutSegment *stub = model->find<LayoutSegment>(stubId);
    if (!position || !stub)
        return false;
    const QString spliceNodeId = spliceOfStub(model->harness(), stubId);
    if (!position->atNodeId.isEmpty()) {
        // Side by side with the splices already at that point: this splice's own point and
        // stub go away, and the point holds its splice too.
        const LayoutNode *spliceNode = model->find<LayoutNode>(spliceNodeId);
        LayoutNode joined = *model->find<LayoutNode>(position->atNodeId);
        joined.sharedSpliceIds.push_back(spliceNode->refId);
        joined.sharedSpliceIds.insert(joined.sharedSpliceIds.end(), spliceNode->sharedSpliceIds.begin(),
                                      spliceNode->sharedSpliceIds.end());
        QUndoStack *stack = m_document->undoStack();
        stack->beginMacro(tr("Place splice in loom"));
        stack->push(removeLayoutItems(model, {spliceNodeId}, {stubId}, {}).release());
        stack->push(new UpdateEntityCommand<LayoutNode>(model, joined, {}));
        stack->endMacro();
        return true;
    }
    const LayoutSegment *loom = model->find<LayoutSegment>(position->segmentId);
    LayoutSegmentItem *loomItem = m_scene->segmentItem(position->segmentId);
    if (!loom || !loomItem)
        return false;
    // The drawing is not to scale: keep the splice clear of the loom's ends.
    const double fromA = position->fromNodeA ? position->offsetMm / loom->lengthMm
                                             : 1 - position->offsetMm / loom->lengthMm;
    const SegmentSplit split = splitAt(loomItem->endA(), loomItem->endB(), loom->bend, std::clamp(fromA, 0.2, 0.8));
    const QString loomId = loom->id;
    const bool fromNodeA = position->fromNodeA;
    const double offsetMm = position->offsetMm;
    QUndoStack *stack = m_document->undoStack();
    stack->beginMacro(tr("Place splice in loom"));
    LayoutNode moved = *model->find<LayoutNode>(spliceNodeId);
    moved.position = split.point;
    stack->push(new UpdateEntityCommand<LayoutNode>(model, moved, {}));
    stack->push(removeLayoutItems(model, {}, {stubId}, {}).release());
    QString newSegmentId;
    stack->push(splitSegmentAt(model, loomId, spliceNodeId, split.fraction, split.bendA, split.bendB, &newSegmentId)
                    .release());
    if (auto distance = setDistanceAlongRun(model, spliceNodeId, fromNodeA ? loomId : newSegmentId, offsetMm))
        stack->push(distance.release());
    stack->endMacro();
    return true;
}

LayoutSegmentItem *LayoutView::bendHandleAt(const QPoint &viewPos) const
{
    const QPointF scenePos = mapToScene(viewPos);
    const qreal tolerance = 3.0 / std::max(transform().m11(), 1e-6);
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (item->type() == LayoutSegmentItemType) {
            auto *segment = static_cast<LayoutSegmentItem *>(item);
            if (segment->isBendHandleAt(scenePos, tolerance))
                return segment;
        }
    }
    return nullptr;
}

void LayoutView::recordPressPositions()
{
    m_pressPositions.clear();
    m_notePressPositions.clear();
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (item->type() == LayoutNodeItemType)
            m_pressPositions[static_cast<LayoutNodeItem *>(item)->id()] = item->pos();
        else if (item->type() == NoteItemType)
            m_notePressPositions[static_cast<NoteItem *>(item)->id()] = item->pos();
    }
}

void LayoutView::mousePressEvent(QMouseEvent *event)
{
    closeEditor();
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select) {
        if (LayoutSegmentItem *segment = bendHandleAt(event->pos())) {
            m_bendSegment = segment->id();
            return;
        }
    }
    if (event->button() == Qt::LeftButton) {
        LayoutNodeItem *node = m_tool == Tool::Segment ? nodeAt(event->pos()) : nodeLeadAt(event->pos());
        if (node) {
            m_segmentStart = node->id();
            m_preview = new QGraphicsLineItem(QLineF(node->pos(), node->pos()));
            m_preview->setPen(QPen(palette().color(QPalette::Highlight), 3, Qt::DashLine));
            m_preview->setZValue(10);
            m_scene->addItem(m_preview);
            return;
        }
    }
    CanvasView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton)
        recordPressPositions();
}

void LayoutView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_bendSegment.isEmpty()) {
        if (const LayoutSegment *current = m_document->model()->find<LayoutSegment>(m_bendSegment)) {
            LayoutSegment preview = *current;
            preview.bend = bendFor(preview, mapToScene(event->pos()));
            m_scene->previewSegment(preview);
        }
        return;
    }
    if (m_preview) {
        QPointF end = mapToScene(event->pos());
        LayoutNodeItem *node = nodeAt(event->pos());
        if (!node)
            node = nodeNear(snapToCanvasGrid(end), {m_segmentStart});
        if (node)
            end = node->pos();
        m_preview->setLine(QLineF(m_preview->line().p1(), end));
        return;
    }
    if (event->buttons() == Qt::NoButton && !isPanning()) {
        setHoveredLead(nodeLeadAt(event->pos()));
        if (m_hoveredLead.isEmpty() && m_tool == Tool::Select)
            viewport()->setCursor(bendHandleAt(event->pos()) ? Qt::SizeAllCursor : Qt::ArrowCursor);
    }
    CanvasView::mouseMoveEvent(event);
}

void LayoutView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_bendSegment.isEmpty() && event->button() == Qt::LeftButton) {
        const QString id = std::exchange(m_bendSegment, QString());
        HarnessModel *model = m_document->model();
        if (const LayoutSegment *current = model->find<LayoutSegment>(id)) {
            LayoutSegment updated = *current;
            updated.bend = bendFor(updated, mapToScene(event->pos()));
            if (updated.bend != current->bend)
                m_document->undoStack()->push(new UpdateEntityCommand<LayoutSegment>(
                    model, updated, updated.bend ? tr("Bend segment") : tr("Straighten segment")));
            else
                m_scene->previewSegment(*current);
        }
        return;
    }
    if (m_preview && event->button() == Qt::LeftButton) {
        finishSegment(event->pos());
        return;
    }
    CanvasView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton)
        commitMoves();
}

void LayoutView::finishSegment(const QPoint &viewPos)
{
    const QString from = m_segmentStart;
    const QPointF scenePos = mapToScene(viewPos);
    const QPointF startPos = m_preview->line().p1();
    cancelSegment();
    HarnessModel *model = m_document->model();
    LayoutNodeItem *target = nodeAt(viewPos);
    if (!target) // dropped next to a point: join it rather than stack a new one on it
        target = nodeNear(snapToCanvasGrid(scenePos), {from});
    if (target && target->id() == from)
        return;
    if (!target && QLineF(startPos, scenePos).length() < 15)
        return; // a click, not a drag

    auto macro = std::make_unique<QUndoCommand>(tr("Add segment"));
    QString to;
    if (target) {
        to = target->id();
    } else {
        LayoutNode branch{newId(), LayoutNodeType::Branch, {}, snapToCanvasGrid(scenePos), {}};
        to = branch.id;
        new AddEntityCommand<LayoutNode>(model, std::move(branch), {}, {}, macro.get());
    }
    LayoutSegment segment{newId(), from, to, 0.0, {}, {}, SleevingCoverage::Full, std::nullopt, {}};
    const QString segmentId = segment.id;
    new AddEntityCommand<LayoutSegment>(model, std::move(segment), {}, {}, macro.get());
    m_document->undoStack()->push(macro.release());
    Selection s;
    s.layoutSegments.insert(segmentId);
    m_selection->setSelection(s);
    // Lengths are entered, not measured: ask for it right away.
    QTimer::singleShot(0, this, [this, segmentId] { editSegmentLength(segmentId); });
}

std::optional<QPointF> LayoutView::bendFor(const LayoutSegment &segment, const QPointF &scenePos) const
{
    LayoutSegmentItem *item = m_scene->segmentItem(segment.id);
    if (!item)
        return segment.bend;
    return bendThroughMidpoint(item->endA(), item->endB(), scenePos);
}

void LayoutView::cancelBend()
{
    const QString id = std::exchange(m_bendSegment, QString());
    if (const LayoutSegment *current = m_document->model()->find<LayoutSegment>(id))
        m_scene->previewSegment(*current);
}

void LayoutView::cancelSegment()
{
    delete m_preview;
    m_preview = nullptr;
    m_segmentStart.clear();
}

void LayoutView::commitMoves()
{
    HarnessModel *model = m_document->model();
    auto macro = std::make_unique<QUndoCommand>(tr("Move"));
    QSet<QString> moved;
    for (const auto &[id, before] : m_pressPositions) {
        LayoutNodeItem *item = m_scene->nodeItem(id);
        const LayoutNode *node = model->find<LayoutNode>(id);
        if (!item || !node || item->pos() == before)
            continue;
        LayoutNode updated = *node;
        updated.position = item->pos();
        new UpdateEntityCommand<LayoutNode>(model, updated, {}, macro.get());
        moved.insert(id);
    }
    m_pressPositions.clear();
    for (const auto &[id, before] : m_notePressPositions) {
        NoteItem *item = m_scene->noteItem(id);
        const Note *note = model->find<Note>(id);
        if (!item || !note || item->pos() == before)
            continue;
        Note updated = *note;
        updated.position = item->pos();
        new UpdateEntityCommand<Note>(model, updated, {}, macro.get());
    }
    m_notePressPositions.clear();
    if (macro->childCount() == 0)
        return;
    // A point dropped onto another joins it: the branch point of the two goes away.
    std::vector<std::pair<QString, QString>> joins; // branch id, into id
    if (moved.size() == 1) {
        const QString id = *moved.begin();
        if (LayoutNodeItem *other = nodeNear(m_scene->nodeItem(id)->pos(), moved, 0.4, 5)) {
            if (model->find<LayoutNode>(id)->type == LayoutNodeType::Branch)
                joins.emplace_back(id, other->id());
            else if (other->node().type == LayoutNodeType::Branch)
                joins.emplace_back(other->id(), id);
        }
    }
    QUndoStack *stack = m_document->undoStack();
    if (joins.empty() && moved.size() == 1) {
        // A splice dropped onto a bundle goes inside it. A stub it was drawn on is replaced:
        // the stub's length becomes the splice's distance along the bundle from the same end.
        const QString id = *moved.begin();
        QString stubId;
        const QString segmentId = segmentUnderSplice(id, &stubId);
        if (!segmentId.isEmpty()) {
            LayoutSegmentItem *segmentItem = m_scene->segmentItem(segmentId);
            const SegmentSplit split = splitNear(segmentItem->endA(), segmentItem->endB(),
                                                 segmentItem->segment().bend, m_scene->nodeItem(id)->pos());
            const bool fromNodeA = [&] {
                if (stubId.isEmpty())
                    return true;
                const LayoutSegment *stub = model->find<LayoutSegment>(stubId);
                const QString far = stub->nodeA == id ? stub->nodeB : stub->nodeA;
                return far == model->find<LayoutSegment>(segmentId)->nodeA;
            }();
            const double stubLengthMm = stubId.isEmpty() ? 0 : model->find<LayoutSegment>(stubId)->lengthMm;
            stack->beginMacro(tr("Place splice in bundle"));
            stack->push(macro.release());
            LayoutNode updated = *model->find<LayoutNode>(id);
            updated.position = split.point; // onto the line itself
            stack->push(new UpdateEntityCommand<LayoutNode>(model, updated, {}));
            if (!stubId.isEmpty())
                stack->push(removeLayoutItems(model, {}, {stubId}, {}).release());
            QString newSegmentId;
            stack->push(splitSegmentAt(model, segmentId, id, split.fraction, split.bendA, split.bendB,
                                       &newSegmentId).release());
            bool placed = false;
            if (stubLengthMm > 0) {
                const QString fromId = fromNodeA ? segmentId : newSegmentId;
                if (auto distance = setDistanceAlongRun(model, id, fromId, stubLengthMm))
                    stack->push(distance.release());
                placed = model->find<LayoutSegment>(fromId)->lengthMm == stubLengthMm;
            }
            stack->endMacro();
            if (!placed)
                QTimer::singleShot(0, this, [this, id] { editPositionAlongRun(id); });
            return;
        }
    }
    if (joins.empty()) {
        stack->push(macro.release());
        return;
    }
    stack->beginMacro(tr("Join points"));
    stack->push(macro.release());
    for (const auto &[branch, into] : joins) {
        if (auto join = mergeBranchInto(model, branch, into))
            stack->push(join.release());
    }
    stack->endMacro();
}

QString LayoutView::segmentUnderSplice(const QString &nodeId, QString *stubId) const
{
    const Harness &h = m_document->model()->harness();
    const LayoutNode *node = m_document->model()->find<LayoutNode>(nodeId);
    LayoutNodeItem *item = m_scene->nodeItem(nodeId);
    if (!node || !item || node->type != LayoutNodeType::Splice)
        return {};
    const LayoutSegment *stub = nullptr;
    for (const LayoutSegment &s : h.layoutSegments) {
        if (s.nodeA != nodeId && s.nodeB != nodeId)
            continue;
        if (stub)
            return {}; // already inside a run, or a branch of its own
        stub = &s;
    }
    const QString stubFar = stub ? (stub->nodeA == nodeId ? stub->nodeB : stub->nodeA) : QString();
    // The bundle's own shape (line plus caption) under the splice's center.
    for (QGraphicsItem *under : m_scene->items(item->pos())) {
        if (under->type() != LayoutSegmentItemType)
            continue;
        const auto *segmentItem = static_cast<LayoutSegmentItem *>(under);
        if (!stub)
            return segmentItem->id();
        if (segmentItem->id() == stub->id)
            continue;
        const LayoutSegment &target = segmentItem->segment();
        if (target.nodeA != stubFar && target.nodeB != stubFar)
            return {};
        *stubId = stub->id;
        return segmentItem->id();
    }
    return {};
}

void LayoutView::editPositionAlongRun(const QString &nodeId)
{
    HarnessModel *model = m_document->model();
    const LayoutNode *node = model->find<LayoutNode>(nodeId);
    if (!node || node->type != LayoutNodeType::Splice)
        return;
    std::vector<const LayoutSegment *> run;
    for (const LayoutSegment &s : model->harness().layoutSegments) {
        if (s.nodeA == nodeId || s.nodeB == nodeId)
            run.push_back(&s);
    }
    if (run.size() != 2)
        return;
    // Measure from a connector end when there is one (e.g. "2 in from the ECU").
    const auto farOf = [&](const LayoutSegment *s) {
        return model->find<LayoutNode>(s->nodeA == nodeId ? s->nodeB : s->nodeA);
    };
    if (farOf(run[0])->type != LayoutNodeType::Connector && farOf(run[1])->type == LayoutNodeType::Connector)
        std::swap(run[0], run[1]);
    const LayoutSegment &from = *run[0];
    LayoutSegmentItem *fromItem = m_scene->segmentItem(from.id);
    LayoutNodeItem *farItem = m_scene->nodeItem(from.nodeA == nodeId ? from.nodeB : from.nodeA);
    LayoutNodeItem *spliceItem = m_scene->nodeItem(nodeId);
    if (!fromItem || !farItem || !spliceItem)
        return;
    const QString farLabel = farItem->label().isEmpty() ? tr("the branch point") : farItem->label();
    emit statusMessage(tr("Distance from %1 to splice %2 (the bundle keeps its total length).")
                           .arg(farLabel, spliceItem->label()));
    const Units units = model->harness().info.units;
    const QString segmentId = from.id;
    const QString text = from.lengthMm > 0 ? formatLength(from.lengthMm, units, false) : QString();
    editText(QRectF(fromItem->midpoint() - QPointF(46, 17), QSizeF(92, 18)), text,
             [this, nodeId, segmentId, units, text](const QString &value, Nav) {
                 if (value.trimmed().isEmpty() || value == text)
                     return;
                 const auto mm = parseLength(value, units);
                 if (!mm) {
                     emit statusMessage(tr("\"%1\" is not a length in %2.").arg(value, unitsSymbol(units)));
                     return;
                 }
                 if (auto command = setDistanceAlongRun(m_document->model(), nodeId, segmentId, *mm))
                     m_document->undoStack()->push(command.release());
                 else
                     emit statusMessage(tr("The splice must sit inside the bundle: enter a distance shorter than its total length."));
             });
}

bool LayoutView::nudgeSelection(const QPointF &delta)
{
    recordPressPositions();
    if (m_pressPositions.empty() && m_notePressPositions.empty())
        return false;
    for (const auto &[id, pos] : m_pressPositions)
        m_scene->nodeItem(id)->setPos(pos + delta);
    for (const auto &[id, pos] : m_notePressPositions)
        m_scene->noteItem(id)->setPos(pos + delta);
    commitMoves();
    return true;
}

void LayoutView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        for (QGraphicsItem *item : items(event->pos())) {
            if (item->type() == NoteItemType) {
                editNote(static_cast<NoteItem *>(item)->id());
                return;
            }
            if (item->type() == LayoutSegmentItemType) {
                editSegmentLength(static_cast<LayoutSegmentItem *>(item)->id());
                return;
            }
            if (item->type() == LayoutNodeItemType) {
                const auto *node = static_cast<LayoutNodeItem *>(item);
                if (node->node().type == LayoutNodeType::Splice && node->legCount() == 2) {
                    editPositionAlongRun(node->id());
                    return;
                }
                break;
            }
        }
    }
    CanvasView::mouseDoubleClickEvent(event);
}

void LayoutView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_bendSegment.isEmpty())
            cancelBend();
        else if (m_preview)
            cancelSegment();
        else
            m_scene->clearSelection();
        return;
    }
    CanvasView::keyPressEvent(event);
}

void LayoutView::contextMenuEvent(QContextMenuEvent *event)
{
    for (QGraphicsItem *item : items(event->pos())) {
        QMenu menu(this);
        if (item->type() == NoteItemType) {
            const QString id = static_cast<NoteItem *>(item)->id();
            menu.addAction(tr("Edit Note"), this, [this, id] { editNote(id); });
        } else if (item->type() == LayoutSegmentItemType) {
            const auto *segment = static_cast<LayoutSegmentItem *>(item);
            const QString id = segment->id();
            menu.addAction(tr("Edit Length"), this, [this, id] { editSegmentLength(id); });
            QAction *straighten = menu.addAction(tr("Straighten Segment"), this, [this, id] { straightenSegment(id); });
            straighten->setEnabled(segment->segment().bend.has_value());
        } else if (item->type() == LayoutNodeItemType) {
            const auto *node = static_cast<LayoutNodeItem *>(item);
            if (node->node().type != LayoutNodeType::Splice || node->legCount() != 2)
                break;
            const QString id = node->id();
            menu.addAction(tr("Position Along Bundle..."), this, [this, id] { editPositionAlongRun(id); });
        } else {
            continue;
        }
        menu.exec(event->globalPos());
        return;
    }
    CanvasView::contextMenuEvent(event);
}

// ---- LayoutEditor ---------------------------------------------------------------

LayoutEditor::LayoutEditor(Document *document, SelectionModel *selection, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_view(new LayoutView(document, selection, this))
    , m_unplaced(new QListWidget(this))
{
    auto *side = new QWidget(this);
    auto *title = new QLabel(tr("Not in the layout yet"), side);
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
    auto *hint = new QLabel(tr("Double-click to place. Then draw segments by dragging the short lead "
                               "on the side of a point, and type each branch length. To put a splice inside "
                               "a loom, draw a segment from the connector to the splice and type its "
                               "distance, or drag the splice onto the loom; double-click it to change "
                               "the distance."),
                            side);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    auto *place = new QPushButton(tr("Place"), side);
    auto *branch = new QPushButton(tr("Add Branch Point"), side);
    auto *note = new QPushButton(tr("Add Note"), side);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(6, 6, 6, 6);
    sideLayout->addWidget(title);
    sideLayout->addWidget(m_unplaced, 1);
    sideLayout->addWidget(place);
    sideLayout->addWidget(branch);
    sideLayout->addWidget(note);
    sideLayout->addWidget(hint);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(side);
    splitter->addWidget(m_view);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 900});
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    m_unplaced->setAccessibleName(tr("Items not in the layout yet"));
    connect(m_unplaced, &QListWidget::itemActivated, this, &LayoutEditor::placeCurrent);
    connect(place, &QPushButton::clicked, this, &LayoutEditor::placeCurrent);
    connect(branch, &QPushButton::clicked, m_view, &LayoutView::addBranchPoint);
    connect(note, &QPushButton::clicked, m_view, &LayoutView::addNote);

    HarnessModel *model = document->model();
    auto schedule = [this] {
        if (m_refreshPending)
            return;
        m_refreshPending = true;
        QTimer::singleShot(0, this, [this] {
            m_refreshPending = false;
            refreshUnplaced();
        });
    };
    connect(model, &HarnessModel::harnessReset, this, schedule);
    connect(model, &HarnessModel::entityAdded, this, schedule);
    connect(model, &HarnessModel::entityChanged, this, schedule);
    connect(model, &HarnessModel::entityRemoved, this, schedule);
    refreshUnplaced();
}

void LayoutEditor::refreshUnplaced()
{
    const Harness &h = m_document->model()->harness();
    QSet<QString> placed;
    for (const LayoutNode &n : h.layoutNodes) {
        placed.insert(n.refId);
        for (const QString &id : n.sharedSpliceIds)
            placed.insert(id);
    }
    const QString current = m_unplaced->currentItem() ? m_unplaced->currentItem()->data(Qt::UserRole).toString()
                                                      : QString();
    m_unplaced->clear();
    for (const LayoutRef &ref : layoutRefs(h)) {
        if (placed.contains(ref.refId))
            continue;
        QString kind;
        switch (ref.nodeType) {
        case LayoutNodeType::Connector:
            kind = tr("Connector");
            break;
        case LayoutNodeType::Splice:
            kind = tr("Splice");
            break;
        default:
            kind = tr("Wire end");
            break;
        }
        auto *item = new QListWidgetItem(QStringLiteral("%1: %2").arg(kind, ref.label), m_unplaced);
        item->setData(Qt::UserRole, ref.refId);
        if (ref.refId == current)
            m_unplaced->setCurrentItem(item);
    }
}

void LayoutEditor::placeCurrent()
{
    QListWidgetItem *item = m_unplaced->currentItem();
    if (!item)
        return;
    const QString refId = item->data(Qt::UserRole).toString();
    for (const LayoutRef &ref : layoutRefs(m_document->model()->harness())) {
        if (ref.refId == refId) {
            m_view->placeRef(ref);
            return;
        }
    }
}

} // namespace tbh::app
