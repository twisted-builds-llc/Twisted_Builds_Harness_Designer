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
    m_wiresByItem.clear();

    const Harness &h = m_model->harness();
    for (const Connector &c : h.connectors)
        addConnector(c);
    for (const Splice &s : h.splices)
        addSplice(s);
    for (const Wire &w : h.wires)
        addWire(w);
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

void SchematicScene::addSplice(const Splice &splice)
{
    auto *item = new SpliceItem(splice);
    addItem(item);
    m_splices.insert(splice.id, item);
    applyPlacement(item);
}

void SchematicScene::addWire(const Wire &wire)
{
    auto *item = new WireItem(wire);
    addItem(item);
    m_wires.insert(wire.id, item);
    indexWire(wire);
    syncFreeEnds(wire);
    if (!m_building)
        updateWire(wire.id);
}

void SchematicScene::syncFreeEnds(const Wire &wire)
{
    for (bool endB : {false, true}) {
        const QString key = freeEndPlacementKey(wire.id, endB);
        const auto *free = std::get_if<FreeEnd>(endB ? &wire.endB : &wire.endA);
        FreeEndItem *item = m_freeEnds.value(key);
        if (free && !item) {
            item = new FreeEndItem(wire.id, endB, *free);
            addItem(item);
            m_freeEnds.insert(key, item);
            applyPlacement(item);
        } else if (free) {
            item->setEnd(*free);
        } else if (item) {
            m_freeEnds.remove(key);
            delete item;
        }
    }
}

void SchematicScene::applyPlacement(PlacedItem *item)
{
    const bool wasBuilding = m_building;
    m_building = true; // wires are updated by the caller
    if (const SchematicPlacement *p = m_model->placement(item->placementKey())) {
        item->setPos(p->position);
        if (auto *connector = dynamic_cast<ConnectorItem *>(item))
            connector->setDisplay(p->collapsed, p->maxVisibleRows);
    } else if (auto *freeEnd = dynamic_cast<FreeEndItem *>(item)) {
        const Wire *wire = m_model->find<Wire>(freeEnd->wireId());
        const WireEnd &other = freeEnd->isEndB() ? wire->endA : wire->endB;
        item->setPos(snapped(endReference(other, wire->id, !freeEnd->isEndB()) + QPointF(180, 0)));
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
    for (auto *s : m_splices) {
        const QRectF r = s->sceneBoundingRect();
        right = any ? std::max(right, r.right()) : r.right();
        any = true;
    }
    return any ? snapped(QPointF(right + 80, top)) : QPointF(0, 0);
}

QStringList SchematicScene::endKeys(const Wire &wire)
{
    QStringList keys;
    for (bool endB : {false, true}) {
        const WireEnd &end = endB ? wire.endB : wire.endA;
        if (const auto *pin = std::get_if<PinRef>(&end))
            keys << pin->connectorId;
        else if (const auto *splice = std::get_if<SpliceRef>(&end))
            keys << splice->spliceId;
        else
            keys << freeEndPlacementKey(wire.id, endB);
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

QPointF SchematicScene::endReference(const WireEnd &end, const QString &wireId, bool endB) const
{
    if (const auto *pin = std::get_if<PinRef>(&end)) {
        if (auto *item = m_connectors.value(pin->connectorId))
            return item->sceneBoundingRect().center();
    } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
        if (auto *item = m_splices.value(splice->spliceId))
            return item->scenePos();
    } else if (auto *item = m_freeEnds.value(freeEndPlacementKey(wireId, endB))) {
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
            return item->pinAnchor(item->rowOfPin(pin->pinId), endReference(other, wire.id, !endB));
    } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
        if (auto *item = m_splices.value(splice->spliceId))
            return item->anchor();
    } else if (auto *item = m_freeEnds.value(freeEndPlacementKey(wire.id, endB))) {
        return item->anchor();
    }
    return {};
}

void SchematicScene::updateWire(const QString &wireId)
{
    WireItem *item = m_wires.value(wireId);
    const Wire *wire = m_model->find<Wire>(wireId);
    if (!item || !wire)
        return;
    item->setRoute(endAnchor(*wire, false), endAnchor(*wire, true));
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
        break;
    case EntityKind::Connector:
        addConnector(*m_model->find<Connector>(id));
        updateWiresOf(id);
        break;
    case EntityKind::Splice:
        addSplice(*m_model->find<Splice>(id));
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
    case EntityKind::Wire:
        if (auto *item = m_wires.value(id)) {
            const Wire &wire = *m_model->find<Wire>(id);
            item->setWire(wire);
            unindexWire(id);
            indexWire(wire);
            syncFreeEnds(wire);
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
        break;
    case EntityKind::Connector:
        delete m_connectors.take(id);
        break;
    case EntityKind::Splice:
        delete m_splices.take(id);
        break;
    case EntityKind::Wire:
        delete m_wires.take(id);
        delete m_freeEnds.take(freeEndPlacementKey(id, false));
        delete m_freeEnds.take(freeEndPlacementKey(id, true));
        unindexWire(id);
        break;
    }
}

void SchematicScene::onPlacementChanged(const QString &key)
{
    if (PlacedItem *item = placedItem(key)) {
        applyPlacement(item);
        updateWiresOf(key);
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
            selection.wires.insert(static_cast<FreeEndItem *>(item)->wireId());
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
    for (auto *freeEnd : std::as_const(m_freeEnds)) {
        if (!s.wires.contains(freeEnd->wireId()))
            freeEnd->setSelected(false);
    }
    m_syncingSelection = false;
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
