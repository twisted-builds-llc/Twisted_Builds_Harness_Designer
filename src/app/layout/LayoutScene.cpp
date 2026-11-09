// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout/LayoutScene.h"

#include "SelectionModel.h"
#include "calc/WireLength.h"
#include "model/Queries.h"
#include "ui/CanvasView.h"
#include "ui/Format.h"

#include <QTimer>

namespace tbh::app {

LayoutScene::LayoutScene(HarnessModel *model, SelectionModel *selection, QObject *parent)
    : QGraphicsScene(parent)
    , m_model(model)
    , m_selection(selection)
{
    setSceneRect(-5000, -5000, 10000, 10000);
    connect(model, &HarnessModel::harnessReset, this, &LayoutScene::rebuild);
    connect(model, &HarnessModel::entityAdded, this, &LayoutScene::onAdded);
    connect(model, &HarnessModel::entityChanged, this, &LayoutScene::onChanged);
    connect(model, &HarnessModel::entityRemoved, this, &LayoutScene::onRemoved);
    connect(model, &HarnessModel::infoChanged, this, &LayoutScene::scheduleRefresh); // units
    connect(this, &QGraphicsScene::selectionChanged, this, &LayoutScene::syncSelectionToModel);
    connect(selection, &SelectionModel::selectionChanged, this, &LayoutScene::syncSelectionFromModel);
    rebuild();
}

LayoutScene::~LayoutScene()
{
    const QSignalBlocker blocker(this);
    clear();
}

void LayoutScene::rebuild()
{
    {
        const QSignalBlocker blocker(this);
        clear();
    }
    m_nodes.clear();
    m_segments.clear();
    const Harness &h = m_model->harness();
    for (const LayoutNode &n : h.layoutNodes)
        addNode(n);
    for (const LayoutSegment &s : h.layoutSegments)
        addSegment(s);
    refreshCaptions();
}

void LayoutScene::addNode(const LayoutNode &node)
{
    auto *item = new LayoutNodeItem(node);
    addItem(item);
    m_nodes.insert(node.id, item);
}

void LayoutScene::addSegment(const LayoutSegment &segment)
{
    auto *item = new LayoutSegmentItem(segment);
    addItem(item);
    m_segments.insert(segment.id, item);
    updateSegmentEnds(item);
}

void LayoutScene::updateSegmentEnds(LayoutSegmentItem *item)
{
    LayoutNodeItem *a = m_nodes.value(item->segment().nodeA);
    LayoutNodeItem *b = m_nodes.value(item->segment().nodeB);
    if (a && b)
        item->setEnds(a->pos(), b->pos());
}

void LayoutScene::nodeMoved(LayoutNodeItem *item)
{
    for (LayoutSegmentItem *segment : std::as_const(m_segments)) {
        if (segment->segment().nodeA == item->id() || segment->segment().nodeB == item->id())
            updateSegmentEnds(segment);
    }
}

void LayoutScene::onAdded(EntityKind kind, const QString &id)
{
    if (kind == EntityKind::LayoutNode)
        addNode(*m_model->find<LayoutNode>(id));
    else if (kind == EntityKind::LayoutSegment)
        addSegment(*m_model->find<LayoutSegment>(id));
    scheduleRefresh();
}

void LayoutScene::onChanged(EntityKind kind, const QString &id)
{
    if (kind == EntityKind::LayoutNode) {
        if (LayoutNodeItem *item = m_nodes.value(id)) {
            item->setNode(*m_model->find<LayoutNode>(id), {}); // label follows in refreshCaptions()
            nodeMoved(item);
        }
    } else if (kind == EntityKind::LayoutSegment) {
        if (LayoutSegmentItem *item = m_segments.value(id)) {
            item->setSegment(*m_model->find<LayoutSegment>(id));
            updateSegmentEnds(item);
        }
    }
    scheduleRefresh();
}

void LayoutScene::onRemoved(EntityKind kind, const QString &id)
{
    if (kind == EntityKind::LayoutNode)
        delete m_nodes.take(id);
    else if (kind == EntityKind::LayoutSegment)
        delete m_segments.take(id);
    scheduleRefresh();
}

void LayoutScene::scheduleRefresh()
{
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        refreshCaptions();
    });
}

void LayoutScene::refreshCaptions()
{
    const Harness &h = m_model->harness();
    QHash<QString, QString> labels;
    for (const LayoutRef &ref : layoutRefs(h))
        labels.insert(ref.refId, ref.label);
    for (LayoutNodeItem *item : std::as_const(m_nodes))
        item->setNode(item->node(), labels.value(item->node().refId));

    const calc::LayoutGraph graph(h);
    const auto usage = calc::wiresBySegment(h, graph);
    for (LayoutSegmentItem *item : std::as_const(m_segments)) {
        const LayoutSegment &s = item->segment();
        const int count = int(usage.value(s.id).size());
        QString usageText = tr("%n wire(s)", nullptr, count);
        if (!s.label.isEmpty())
            usageText = s.label + QStringLiteral(" · ") + usageText;
        item->setCaption(s.lengthMm > 0 ? formatLength(s.lengthMm, h.info.units) : tr("no length"), usageText);
        item->update();
    }
}

void LayoutScene::syncSelectionToModel()
{
    if (m_syncingSelection)
        return;
    // A selection made here replaces the whole selection (including the schematic's).
    Selection selection;
    for (QGraphicsItem *item : selectedItems()) {
        if (item->type() == LayoutNodeItemType)
            selection.layoutNodes.insert(static_cast<LayoutNodeItem *>(item)->id());
        else if (item->type() == LayoutSegmentItemType)
            selection.layoutSegments.insert(static_cast<LayoutSegmentItem *>(item)->id());
    }
    m_syncingSelection = true;
    m_selection->setSelection(selection);
    m_syncingSelection = false;
}

void LayoutScene::syncSelectionFromModel()
{
    if (m_syncingSelection)
        return;
    m_syncingSelection = true;
    const Selection &s = m_selection->selection();
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it)
        it.value()->setSelected(s.layoutNodes.contains(it.key()));
    for (auto it = m_segments.cbegin(); it != m_segments.cend(); ++it)
        it.value()->setSelected(s.layoutSegments.contains(it.key()));
    m_syncingSelection = false;
}

void LayoutScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    drawCanvasBackground(painter, rect, palette());
}

} // namespace tbh::app
