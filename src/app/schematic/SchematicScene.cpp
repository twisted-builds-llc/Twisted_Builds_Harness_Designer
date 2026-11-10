// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SchematicScene.h"

#include "SelectionModel.h"
#include "ui/CanvasView.h"

#include <QPainter>
#include <QPalette>

#include <cmath>

namespace tbh::app {

SchematicScene::SchematicScene(HarnessModel *model, SelectionModel *selection, QObject *parent)
    : QGraphicsScene(parent)
    , m_model(model)
    , m_selection(selection)
{
    // Unbounded-feeling canvas; grows with items.
    setSceneRect(-5000, -5000, 10000, 10000);
    setItemIndexMethod(QGraphicsScene::BspTreeIndex);

    connect(model, &HarnessModel::harnessReset, this, &SchematicScene::rebuild);
    connect(model, &HarnessModel::entityAdded, this, &SchematicScene::onAdded);
    connect(model, &HarnessModel::entityChanged, this, &SchematicScene::onChanged);
    connect(model, &HarnessModel::entityRemoved, this, &SchematicScene::onRemoved);
    connect(model, &HarnessModel::placementChanged, this, &SchematicScene::onPlacementChanged);
    connect(this, &QGraphicsScene::selectionChanged, this, &SchematicScene::syncSelectionToModel);
    connect(selection, &SelectionModel::selectionChanged, this, &SchematicScene::syncSelectionFromModel);
    rebuild();
}

SchematicScene::~SchematicScene()
{
    // Deleting selected items emits selectionChanged; this object must not receive it
    // once its own destructor has run.
    const QSignalBlocker blocker(this);
    clear();
}

QPointF SchematicScene::snapped(const QPointF &point)
{
    return snapToCanvasGrid(point);
}

PlacedItem *SchematicScene::placedItem(const QString &key) const
{
    if (auto *c = m_connectors.value(key))
        return c;
    if (auto *s = m_splices.value(key))
        return s;
    return m_freeEnds.value(key);
}

void SchematicScene::rebuild()
{
    m_building = true;
    {
        const QSignalBlocker blocker(this);
        clear();
    }
    m_connectors.clear();
    m_splices.clear();
    m_wires.clear();
    m_freeEnds.clear();
    m_notes.clear();
    m_wiresByItem.clear();

    const Harness &h = m_model->harness();
    for (const Connector &c : h.connectors)
        addConnector(c);
    for (const Splice &s : h.splices)
        addSplice(s);
    for (const FreeEnd &f : h.freeEnds)
        addFreeEnd(f);
    for (const Wire &w : h.wires)
        addWire(w);
    for (const Note &n : h.notes)
        addNote(n);
    m_building = false;
    for (const Wire &w : h.wires)
        updateWire(w.id);
    syncSelectionFromModel();
}

void SchematicScene::addConnector(const Connector &connector)
{
    auto *item = new ConnectorItem(connector);
    item->setSearchText(m_search);
    addItem(item);
    m_connectors.insert(connector.id, item);
    applyPlacement(item);
}

void SchematicScene::addNote(const Note &note)
{
    if (note.canvas != NoteCanvas::Schematic)
        return;
    auto *item = new NoteItem(note, [this](const QPointF &p) { return m_snap ? snapped(p) : p; });
    addItem(item);
    m_notes.insert(note.id, item);
}

void SchematicScene::addSplice(const Splice &splice)
{
    auto *item = new SpliceItem(splice);
    addItem(item);
    m_splices.insert(splice.id, item);
    applyPlacement(item);
}

void SchematicScene::addFreeEnd(const FreeEnd &end)
{
    auto *item = new FreeEndItem(end);
    addItem(item);
    m_freeEnds.insert(end.id, item);
    applyPlacement(item);
}

void SchematicScene::addWire(const Wire &wire)
{
    auto *item = new WireItem(wire);
    addItem(item);
    m_wires.insert(wire.id, item);
    indexWire(wire);
    if (!m_building)
        updateWire(wire.id);
}

void SchematicScene::applyPlacement(PlacedItem *item)
{
    const bool wasBuilding = m_building;
    m_building = true; // wires are updated by the caller
    if (const SchematicPlacement *p = m_model->placement(item->placementKey())) {
        item->setPos(p->position);
        if (auto *connector = dynamic_cast<ConnectorItem *>(item))
            connector->setDisplay(p->collapsed, p->maxVisibleRows);
    } else {
        item->setPos(nextFreePosition());
    }
    m_building = wasBuilding;
}

QPointF SchematicScene::nextFreePosition() const
{
    qreal right = 0;
    qreal top = 0;
    bool any = false;
    for (auto *c : m_connectors) {
        const QRectF r = c->sceneBoundingRect();
        right = any ? std::max(right, r.right()) : r.right();
        top = any ? std::min(top, r.top()) : r.top();
        any = true;
    }
    for (const auto &items : {QList<PlacedItem *>(m_splices.cbegin(), m_splices.cend()),
                              QList<PlacedItem *>(m_freeEnds.cbegin(), m_freeEnds.cend())}) {
        for (PlacedItem *item : items) {
            const QRectF r = item->sceneBoundingRect();
            right = any ? std::max(right, r.right()) : r.right();
            any = true;
        }
    }
    return any ? snapped(QPointF(right + 80, top)) : QPointF(0, 0);
}

QStringList SchematicScene::endKeys(const Wire &wire)
{
    QStringList keys;
    for (const WireEnd *end : {&wire.endA, &wire.endB}) {
        if (const auto *pin = std::get_if<PinRef>(end))
            keys << pin->connectorId;
        else if (const auto *splice = std::get_if<SpliceRef>(end))
            keys << splice->spliceId;
        else
            keys << std::get<FreeEndRef>(*end).freeEndId;
    }
    return keys;
}

void SchematicScene::indexWire(const Wire &wire)
{
    for (const QString &key : endKeys(wire)) {
        if (!m_wiresByItem.contains(key, wire.id))
            m_wiresByItem.insert(key, wire.id);
    }
}

void SchematicScene::unindexWire(const QString &wireId)
{
    for (auto it = m_wiresByItem.begin(); it != m_wiresByItem.end();) {
        if (it.value() == wireId)
            it = m_wiresByItem.erase(it);
        else
            ++it;
    }
}

QPointF SchematicScene::endReference(const WireEnd &end) const
{
    if (const auto *pin = std::get_if<PinRef>(&end)) {
        if (auto *item = m_connectors.value(pin->connectorId))
            return item->sceneBoundingRect().center();
    } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
        if (auto *item = m_splices.value(splice->spliceId))
            return item->scenePos();
    } else if (auto *item = m_freeEnds.value(std::get<FreeEndRef>(end).freeEndId)) {
        return item->scenePos();
    }
    return {};
}

Anchor SchematicScene::endAnchor(const Wire &wire, bool endB) const
{
    const WireEnd &end = endB ? wire.endB : wire.endA;
    const WireEnd &other = endB ? wire.endA : wire.endB;
    if (const auto *pin = std::get_if<PinRef>(&end)) {
        if (auto *item = m_connectors.value(pin->connectorId))
            return item->pinAnchor(item->rowOfPin(pin->pinId), endReference(other));
    } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
        if (auto *item = m_splices.value(splice->spliceId))
            return item->anchor();
    } else if (auto *item = m_freeEnds.value(std::get<FreeEndRef>(end).freeEndId)) {
        return item->anchor(endReference(other));
    }
    return {};
}

void SchematicScene::updateWire(const QString &wireId)
{
    WireItem *item = m_wires.value(wireId);
    const Wire *wire = m_model->find<Wire>(wireId);
    if (!item || !wire)
        return;
    const SchematicPlacement *route = m_model->placement(wireId);
    item->setRoute(endAnchor(*wire, false), endAnchor(*wire, true),
                   route ? std::optional(route->position.x()) : std::nullopt);
}

void SchematicScene::updateWiresOf(const QString &key)
{
    const QList<QString> wires = m_wiresByItem.values(key);
    for (const QString &wireId : wires)
        updateWire(wireId);
}

void SchematicScene::itemMoved(PlacedItem *item)
{
    if (!m_building)
        updateWiresOf(item->placementKey());
}

void SchematicScene::onAdded(EntityKind kind, const QString &id)
{
    switch (kind) {
    case EntityKind::LayoutNode:
    case EntityKind::LayoutSegment:
    case EntityKind::BomExtra:
        break;
    case EntityKind::Note:
        addNote(*m_model->find<Note>(id));
        break;
    case EntityKind::Connector:
        addConnector(*m_model->find<Connector>(id));
        updateWiresOf(id);
        break;
    case EntityKind::Splice:
        addSplice(*m_model->find<Splice>(id));
        updateWiresOf(id);
        break;
    case EntityKind::FreeEnd:
        addFreeEnd(*m_model->find<FreeEnd>(id));
        updateWiresOf(id);
        break;
    case EntityKind::Wire:
        addWire(*m_model->find<Wire>(id));
        break;
    }
}

void SchematicScene::onChanged(EntityKind kind, const QString &id)
{
    switch (kind) {
    case EntityKind::LayoutNode:
    case EntityKind::LayoutSegment:
    case EntityKind::BomExtra:
        break;
    case EntityKind::Note:
        if (NoteItem *item = m_notes.value(id))
            item->setNote(*m_model->find<Note>(id));
        break;
    case EntityKind::Connector:
        if (auto *item = m_connectors.value(id)) {
            item->setConnector(*m_model->find<Connector>(id));
            updateWiresOf(id);
        }
        break;
    case EntityKind::Splice:
        if (auto *item = m_splices.value(id))
            item->setSplice(*m_model->find<Splice>(id));
        break;
    case EntityKind::FreeEnd:
        if (auto *item = m_freeEnds.value(id))
            item->setFreeEnd(*m_model->find<FreeEnd>(id));
        break;
    case EntityKind::Wire:
        if (auto *item = m_wires.value(id)) {
            const Wire &wire = *m_model->find<Wire>(id);
            item->setWire(wire);
            unindexWire(id);
            indexWire(wire);
            updateWire(id);
        }
        break;
    }
}

void SchematicScene::onRemoved(EntityKind kind, const QString &id)
{
    switch (kind) {
    case EntityKind::LayoutNode:
    case EntityKind::LayoutSegment:
    case EntityKind::BomExtra:
        break;
    case EntityKind::Note:
        delete m_notes.take(id);
        break;
    case EntityKind::Connector:
        delete m_connectors.take(id);
        break;
    case EntityKind::Splice:
        delete m_splices.take(id);
        break;
    case EntityKind::FreeEnd:
        delete m_freeEnds.take(id);
        break;
    case EntityKind::Wire:
        delete m_wires.take(id);
        unindexWire(id);
        break;
    }
}

void SchematicScene::onPlacementChanged(const QString &key)
{
    if (PlacedItem *item = placedItem(key)) {
        applyPlacement(item);
        updateWiresOf(key);
    } else if (m_wires.contains(key)) {
        updateWire(key); // a dragged wire route
    }
}

void SchematicScene::syncSelectionToModel()
{
    if (m_syncingSelection)
        return;
    Selection selection;
    for (QGraphicsItem *item : selectedItems()) {
        switch (item->type()) {
        case ConnectorItemType:
            selection.connectors.insert(static_cast<ConnectorItem *>(item)->id());
            break;
        case SpliceItemType:
            selection.splices.insert(static_cast<SpliceItem *>(item)->id());
            break;
        case WireItemType:
            selection.wires.insert(static_cast<WireItem *>(item)->id());
            break;
        case FreeEndItemType:
            selection.freeEnds.insert(static_cast<FreeEndItem *>(item)->id());
            break;
        case NoteItemType:
            selection.notes.insert(static_cast<NoteItem *>(item)->id());
            break;
        default:
            break;
        }
    }
    m_syncingSelection = true;
    m_selection->setSelection(selection);
    m_syncingSelection = false;
}

void SchematicScene::syncSelectionFromModel()
{
    if (m_syncingSelection)
        return;
    m_syncingSelection = true;
    const Selection &s = m_selection->selection();
    for (auto it = m_connectors.cbegin(); it != m_connectors.cend(); ++it)
        it.value()->setSelected(s.connectors.contains(it.key()));
    for (auto it = m_splices.cbegin(); it != m_splices.cend(); ++it)
        it.value()->setSelected(s.splices.contains(it.key()));
    for (auto it = m_wires.cbegin(); it != m_wires.cend(); ++it)
        it.value()->setSelected(s.wires.contains(it.key()));
    for (auto it = m_freeEnds.cbegin(); it != m_freeEnds.cend(); ++it)
        it.value()->setSelected(s.freeEnds.contains(it.key()));
    for (auto it = m_notes.cbegin(); it != m_notes.cend(); ++it)
        it.value()->setSelected(s.notes.contains(it.key()));
    m_syncingSelection = false;
}

void SchematicScene::setHighlight(const std::optional<calc::HighlightSet> &set)
{
    m_highlight = set;
    applyHighlight();
}

void SchematicScene::applyHighlight()
{
    constexpr qreal kDimmed = 0.18;
    const calc::HighlightSet *set = m_highlight ? &*m_highlight : nullptr;
    auto opacity = [set](const QSet<QString> &ids, const QString &id) {
        return !set || ids.contains(id) ? 1.0 : kDimmed;
    };
    for (auto it = m_connectors.cbegin(); it != m_connectors.cend(); ++it) {
        it.value()->setOpacity(set ? opacity(set->connectors, it.key()) : 1.0);
        QSet<QString> pins;
        if (set) {
            const QString prefix = it.key() + QLatin1Char('/');
            for (const QString &key : set->pins) {
                if (key.startsWith(prefix))
                    pins.insert(key.mid(prefix.size()));
            }
        }
        it.value()->setRelatedPins(set != nullptr, pins);
    }
    for (auto it = m_splices.cbegin(); it != m_splices.cend(); ++it)
        it.value()->setOpacity(set ? opacity(set->splices, it.key()) : 1.0);
    for (auto it = m_wires.cbegin(); it != m_wires.cend(); ++it)
        it.value()->setOpacity(set ? opacity(set->wires, it.key()) : 1.0);
    for (auto it = m_freeEnds.cbegin(); it != m_freeEnds.cend(); ++it)
        it.value()->setOpacity(set ? opacity(set->freeEnds, it.key()) : 1.0);
}

void SchematicScene::setSearchText(const QString &text)
{
    m_search = text;
    for (auto *item : std::as_const(m_connectors))
        item->setSearchText(text);
}

std::vector<std::pair<QString, int>> SchematicScene::searchMatches() const
{
    std::vector<std::pair<QString, int>> matches;
    for (const Connector &c : m_model->harness().connectors) {
        if (auto *item = m_connectors.value(c.id)) {
            for (int row : item->searchMatches())
                matches.emplace_back(c.id, row);
        }
    }
    return matches;
}

void SchematicScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    drawCanvasBackground(painter, rect, palette());
}

} // namespace tbh::app
