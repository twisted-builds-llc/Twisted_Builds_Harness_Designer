// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout/LayoutScene.h"

#include "SelectionModel.h"
#include "calc/Bundle.h"
#include "calc/WireLength.h"
#include "model/Queries.h"
#include "ui/CanvasView.h"
#include "ui/Format.h"
#include "ui/WireColors.h"

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

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
    connect(model, &HarnessModel::settingsChanged, this, &LayoutScene::scheduleRefresh); // packing factor
    connect(model, &HarnessModel::libraryChanged, this, &LayoutScene::scheduleRefresh);
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
    m_notes.clear();
    const Harness &h = m_model->harness();
    for (const LayoutNode &n : h.layoutNodes)
        addNode(n);
    for (const LayoutSegment &s : h.layoutSegments)
        addSegment(s);
    for (const Note &n : h.notes)
        addNote(n);
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

void LayoutScene::addNote(const Note &note)
{
    if (note.canvas != NoteCanvas::Layout)
        return;
    auto *item = new NoteItem(note, [this](const QPointF &p) { return m_snap ? snapToCanvasGrid(p) : p; });
    addItem(item);
    m_notes.insert(note.id, item);
}

void LayoutScene::previewSegment(const LayoutSegment &segment)
{
    if (LayoutSegmentItem *item = m_segments.value(segment.id)) {
        item->setSegment(segment);
        updateSegmentEnds(item);
    }
}

void LayoutScene::updateSegmentEnds(LayoutSegmentItem *item)
{
    LayoutNodeItem *a = m_nodes.value(item->segment().nodeA);
    LayoutNodeItem *b = m_nodes.value(item->segment().nodeB);
    if (a && b)
        item->setEnds(a->pos(), b->pos());
    updateLegs(a);
    updateLegs(b);
}

void LayoutScene::updateLegs(LayoutNodeItem *node)
{
    if (!node)
        return;
    std::vector<LayoutNodeItem::Leg> legs;
    for (const LayoutSegmentItem *segment : std::as_const(m_segments)) {
        const LayoutSegment &s = segment->segment();
        const QString other = s.nodeA == node->id() ? s.nodeB : s.nodeB == node->id() ? s.nodeA : QString();
        const LayoutNodeItem *far = other.isEmpty() ? nullptr : m_nodes.value(other);
        if (!far)
            continue;
        // A curved segment leaves towards its control point (the curve's end tangent).
        const QPointF toward = s.bend ? bendControlPoint(segment->endA(), segment->endB(), s.bend) : far->pos();
        QPointF d = toward - node->pos();
        if (std::hypot(d.x(), d.y()) < 1e-6)
            d = far->pos() - node->pos();
        const qreal length = std::hypot(d.x(), d.y());
        legs.push_back({length > 1e-6 ? d / length : QPointF(1, 0), segment->lineWidth()});
    }
    // Model order of segments is not stable in a hash; sort so equal sets compare equal.
    std::sort(legs.begin(), legs.end(), [](const LayoutNodeItem::Leg &l, const LayoutNodeItem::Leg &r) {
        return std::atan2(l.direction.y(), l.direction.x()) < std::atan2(r.direction.y(), r.direction.x());
    });
    node->setLegs(std::move(legs));
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
    else if (kind == EntityKind::Note)
        addNote(*m_model->find<Note>(id));
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
            const LayoutSegment old = item->segment();
            item->setSegment(*m_model->find<LayoutSegment>(id));
            updateSegmentEnds(item);
            updateLegs(m_nodes.value(old.nodeA)); // in case the segment was re-attached
            updateLegs(m_nodes.value(old.nodeB));
        }
    } else if (kind == EntityKind::Note) {
        if (NoteItem *item = m_notes.value(id))
            item->setNote(*m_model->find<Note>(id));
        return; // notes do not affect captions
    }
    scheduleRefresh();
}

void LayoutScene::onRemoved(EntityKind kind, const QString &id)
{
    if (kind == EntityKind::Note) {
        delete m_notes.take(id);
        return;
    }
    if (kind == EntityKind::LayoutNode)
        delete m_nodes.take(id);
    else if (kind == EntityKind::LayoutSegment) {
        std::unique_ptr<LayoutSegmentItem> item(m_segments.take(id));
        if (item) {
            updateLegs(m_nodes.value(item->segment().nodeA));
            updateLegs(m_nodes.value(item->segment().nodeB));
        }
    }
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
    for (LayoutNodeItem *item : std::as_const(m_nodes)) {
        // Splices side by side at one point show together, e.g. "S1, S2".
        QStringList names{labels.value(item->node().refId)};
        for (const QString &id : item->node().sharedSpliceIds)
            names.append(labels.value(id));
        item->setNode(item->node(), names.join(tr(", ")));
    }

    const calc::LayoutGraph graph(h);
    const auto usage = calc::wiresBySegment(h, graph);
    const Library &library = m_model->library();
    QHash<QString, const Wire *> wiresById;
    for (const Wire &w : h.wires)
        wiresById.insert(w.id, &w);
    for (LayoutSegmentItem *item : std::as_const(m_segments)) {
        const LayoutSegment &s = item->segment();
        const auto wires = usage.value(s.id); // in model order: stripes line up across segments
        QString lengthText = s.lengthMm > 0 ? formatLength(s.lengthMm, h.info.units) : tr("no length");
        if (const Sleeving *sleeving = library.sleeving(s.sleevingId))
            lengthText += QStringLiteral(" \u00b7 ") + sleeving->sizeLabel;
        QStringList details;
        if (!s.label.isEmpty())
            details << s.label;
        QStringList tip{lengthText, tr("%n wire(s)", nullptr, int(wires.size()))};
        if (!wires.empty()) {
            const calc::BundleDiameter bundle = calc::bundleDiameter(h, library, wires);
            // "~" marks a diameter that uses estimated wire ODs.
            const QString diameter = QStringLiteral("\u00d8 %1%2")
                                         .arg(bundle.estimatedWireIds.empty() ? QString() : QStringLiteral("~"),
                                              formatLength(bundle.diameterMm, h.info.units));
            tip << diameter;
            if (m_showDiameters)
                details << diameter;
        }
        if (!s.label.isEmpty())
            tip.prepend(s.label);
        item->setCaption(lengthText, details.join(QStringLiteral(" \u00b7 ")));
        item->setToolTip(tip.join(QStringLiteral(" \u00b7 ")));
        std::vector<LayoutSegmentItem::WireStripe> stripes;
        stripes.reserve(wires.size());
        for (const QString &id : wires) {
            const Wire *w = wiresById.value(id);
            stripes.push_back({w ? wireColor(w->baseColor) : QColor(), w ? wireColor(w->stripeColor) : QColor()});
        }
        item->setWires(std::move(stripes));
        item->update();
    }
    // Branch boots follow the segment widths.
    for (LayoutNodeItem *node : std::as_const(m_nodes))
        updateLegs(node);
    applyHighlight();
}

void LayoutScene::setHighlight(const std::optional<calc::HighlightSet> &set)
{
    m_highlight = set;
    applyHighlight();
}

void LayoutScene::applyHighlight()
{
    constexpr qreal kDimmed = 0.18;
    const calc::HighlightSet *set = m_highlight ? &*m_highlight : nullptr;
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it)
        it.value()->setOpacity(!set || set->layoutNodes.contains(it.key()) ? 1.0 : kDimmed);
    for (auto it = m_segments.cbegin(); it != m_segments.cend(); ++it)
        it.value()->setOpacity(!set || set->layoutSegments.contains(it.key()) ? 1.0 : kDimmed);
}

void LayoutScene::setShowBundleDiameters(bool on)
{
    m_showDiameters = on;
    refreshCaptions();
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
        else if (item->type() == NoteItemType)
            selection.notes.insert(static_cast<NoteItem *>(item)->id());
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
    for (auto it = m_notes.cbegin(); it != m_notes.cend(); ++it)
        it.value()->setSelected(s.notes.contains(it.key()));
    m_syncingSelection = false;
}

void LayoutScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    drawCanvasBackground(painter, rect, palette());
}

} // namespace tbh::app
