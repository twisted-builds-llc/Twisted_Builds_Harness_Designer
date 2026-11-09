// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout/LayoutView.h"

#include "Document.h"
#include "SelectionModel.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "layout/LayoutScene.h"
#include "ui/Format.h"

#include <QGraphicsLineItem>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
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

void LayoutView::deleteSelection()
{
    const Selection &s = m_selection->selection();
    if (s.layoutNodes.isEmpty() && s.layoutSegments.isEmpty())
        return;
    m_document->undoStack()->push(
        removeLayoutItems(m_document->model(), s.layoutNodes, s.layoutSegments).release());
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
                 LayoutSegment updated = *current;
                 // Keep the stored value if the shown (rounded) text was not changed.
                 if (value == formatLength(current->lengthMm, units, false))
                     return;
                 updated.lengthMm = *mm;
                 m_document->undoStack()->push(
                     new UpdateEntityCommand<LayoutSegment>(model, updated, tr("Set segment length")));
             });
}

void LayoutView::mousePressEvent(QMouseEvent *event)
{
    closeEditor();
    if (event->button() == Qt::LeftButton && m_tool == Tool::Segment) {
        if (LayoutNodeItem *node = nodeAt(event->pos())) {
            m_segmentStart = node->id();
            m_preview = new QGraphicsLineItem(QLineF(node->pos(), node->pos()));
            m_preview->setPen(QPen(palette().color(QPalette::Highlight), 3, Qt::DashLine));
            m_preview->setZValue(10);
            m_scene->addItem(m_preview);
            return;
        }
    }
    CanvasView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        m_pressPositions.clear();
        for (QGraphicsItem *item : m_scene->selectedItems()) {
            if (item->type() == LayoutNodeItemType)
                m_pressPositions[static_cast<LayoutNodeItem *>(item)->id()] = item->pos();
        }
    }
}

void LayoutView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_preview) {
        QPointF end = mapToScene(event->pos());
        if (LayoutNodeItem *node = nodeAt(event->pos()))
            end = node->pos();
        m_preview->setLine(QLineF(m_preview->line().p1(), end));
        return;
    }
    CanvasView::mouseMoveEvent(event);
}

void LayoutView::mouseReleaseEvent(QMouseEvent *event)
{
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
    LayoutSegment segment{newId(), from, to, 0.0, {}, {}, SleevingCoverage::Full, {}};
    const QString segmentId = segment.id;
    new AddEntityCommand<LayoutSegment>(model, std::move(segment), {}, {}, macro.get());
    m_document->undoStack()->push(macro.release());
    Selection s;
    s.layoutSegments.insert(segmentId);
    m_selection->setSelection(s);
    // Lengths are entered, not measured: ask for it right away.
    QTimer::singleShot(0, this, [this, segmentId] { editSegmentLength(segmentId); });
}

void LayoutView::cancelSegment()
{
    delete m_preview;
    m_preview = nullptr;
    m_segmentStart.clear();
}

void LayoutView::commitMoves()
{
    auto macro = std::make_unique<QUndoCommand>(tr("Move"));
    for (const auto &[id, before] : m_pressPositions) {
        LayoutNodeItem *item = m_scene->nodeItem(id);
        const LayoutNode *node = m_document->model()->find<LayoutNode>(id);
        if (!item || !node || item->pos() == before)
            continue;
        LayoutNode updated = *node;
        updated.position = item->pos();
        new UpdateEntityCommand<LayoutNode>(m_document->model(), updated, {}, macro.get());
    }
    m_pressPositions.clear();
    if (macro->childCount() > 0)
        m_document->undoStack()->push(macro.release());
}

bool LayoutView::nudgeSelection(const QPointF &delta)
{
    m_pressPositions.clear();
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (item->type() == LayoutNodeItemType)
            m_pressPositions[static_cast<LayoutNodeItem *>(item)->id()] = item->pos();
    }
    if (m_pressPositions.empty())
        return false;
    for (const auto &[id, pos] : m_pressPositions)
        m_scene->nodeItem(id)->setPos(pos + delta);
    commitMoves();
    return true;
}

void LayoutView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        for (QGraphicsItem *item : items(event->pos())) {
            if (item->type() == LayoutSegmentItemType) {
                editSegmentLength(static_cast<LayoutSegmentItem *>(item)->id());
                return;
            }
        }
    }
    CanvasView::mouseDoubleClickEvent(event);
}

void LayoutView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_preview)
            cancelSegment();
        else
            m_scene->clearSelection();
        return;
    }
    CanvasView::keyPressEvent(event);
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
    auto *hint = new QLabel(tr("Double-click to place. Then draw segments with the Segment tool "
                               "and type each branch length."),
                            side);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    auto *place = new QPushButton(tr("Place"), side);
    auto *branch = new QPushButton(tr("Add Branch Point"), side);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(6, 6, 6, 6);
    sideLayout->addWidget(title);
    sideLayout->addWidget(m_unplaced, 1);
    sideLayout->addWidget(place);
    sideLayout->addWidget(branch);
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
    for (const LayoutNode &n : h.layoutNodes)
        placed.insert(n.refId);
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
