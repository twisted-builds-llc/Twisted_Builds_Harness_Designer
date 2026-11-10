// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SchematicScene.h"

#include "SelectionModel.h"
#include "model/Queries.h"
#include "ui/CanvasView.h"

#include <QPainter>
#include <QPalette>

#include <algorithm>
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
    connect(model, &HarnessModel::sheetsChanged, this, &SchematicScene::onSheetsChanged);
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
    m_autoPlaced.clear();

    const Harness &h = m_model->harness();
    const QString previousSheet = m_sheet;
    m_sheet = h.schematic.resolvedSheetId(m_sheet);
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
    if (m_sheet != previousSheet)
        emit currentSheetChanged(m_sheet);
}

QString SchematicScene::sheetOf(const QString &key) const
{
    return m_model->harness().schematic.sheetOf(key);
}

bool SchematicScene::isEndOnCurrentSheet(const Wire &wire, bool endB) const
{
    const WireSheets sheets = wireSheets(m_model->harness(), wire);
    return sheets.drawnOn(m_sheet) || (endB ? sheets.tagSheetB : sheets.tagSheetA) == m_sheet;
}

bool SchematicScene::isOnCurrentSheet(const QString &key) const
{
    return m_model->harness().schematic.isOnSheet(key, m_sheet);
}

void SchematicScene::setCurrentSheet(const QString &sheetId)
{
    const QString resolved = m_model->harness().schematic.resolvedSheetId(sheetId);
    if (resolved == m_sheet)
        return;
    m_sheet = resolved;
    applySheet();
    emit currentSheetChanged(m_sheet);
}

void SchematicScene::onSheetsChanged()
{
    const QString resolved = m_model->harness().schematic.resolvedSheetId(m_sheet);
    const bool changed = resolved != m_sheet;
    m_sheet = resolved;
    applySheet(); // also refreshes the sheet names in off-sheet tags
    if (changed)
        emit currentSheetChanged(m_sheet);
}

void SchematicScene::applySheet()
{
    // Items shown on several sheets sit at their position on this one.
    auto apply = [this](PlacedItem *item) { applyPlacement(item); };
    std::for_each(m_connectors.cbegin(), m_connectors.cend(), apply);
    std::for_each(m_splices.cbegin(), m_splices.cend(), apply);
    std::for_each(m_freeEnds.cbegin(), m_freeEnds.cend(), apply);
    for (NoteItem *note : std::as_const(m_notes))
        applyNoteSheet(note);
    for (const Wire &w : m_model->harness().wires)
        updateWire(w.id);
}

void SchematicScene::applyNoteSheet(NoteItem *item)
{
    item->setVisible(m_model->harness().schematic.resolvedSheetId(item->note().sheetId) == m_sheet);
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
    applyNoteSheet(item);
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
    const Schematic &schematic = m_model->harness().schematic;
    if (const SchematicPlacement *p = m_model->placement(item->placementKey())) {
        item->setPos(schematic.positionOn(item->placementKey(), m_sheet).value_or(p->position));
        if (auto *connector = dynamic_cast<ConnectorItem *>(item))
            connector->setDisplay(p->collapsed, p->maxVisibleRows);
    } else if (!m_autoPlaced.contains(item->placementKey())) {
        // Laid out once; switching sheets must not move it again.
        item->setPos(nextFreePosition(sheetOf(item->placementKey())));
        m_autoPlaced.insert(item->placementKey());
    }
    item->setVisible(isOnCurrentSheet(item->placementKey()));
    m_building = wasBuilding;
}

QPointF SchematicScene::nextFreePosition(const QString &sheetId) const
{
    qreal right = 0;
    qreal top = 0;
    bool any = false;
    const Schematic &schematic = m_model->harness().schematic;
    // Scene rect the item has on `sheetId` (its item position is the one on the current sheet).
    auto rectOn = [&](const PlacedItem *item) -> std::optional<QRectF> {
        if (!schematic.isOnSheet(item->placementKey(), sheetId))
            return std::nullopt;
        const QPointF pos = schematic.positionOn(item->placementKey(), sheetId).value_or(item->pos());
        return item->boundingRect().translated(pos);
    };
    for (auto *c : m_connectors) {
        const auto rect = rectOn(c);
        if (!rect)
            continue;
        const QRectF r = *rect;
        right = any ? std::max(right, r.right()) : r.right();
        top = any ? std::min(top, r.top()) : r.top();
        any = true;
    }
    for (const auto &items : {QList<PlacedItem *>(m_splices.cbegin(), m_splices.cend()),
                              QList<PlacedItem *>(m_freeEnds.cbegin(), m_freeEnds.cend())}) {
        for (PlacedItem *item : items) {
            const auto rect = rectOn(item);
            if (!rect)
                continue;
            const QRectF r = *rect;
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
    // The other end's position means nothing on another sheet: face right, towards the tag.
    const bool sameSheet = wireSheets(m_model->harness(), wire).drawnOn(m_sheet);
    const QPointF towards = sameSheet ? endReference(other) : endReference(end) + QPointF(1e5, 0);
    if (const auto *pin = std::get_if<PinRef>(&end)) {
        if (auto *item = m_connectors.value(pin->connectorId))
            return item->pinAnchor(item->rowOfPin(pin->pinId), towards);
    } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
        if (auto *item = m_splices.value(splice->spliceId))
            return item->anchor();
    } else if (auto *item = m_freeEnds.value(std::get<FreeEndRef>(end).freeEndId)) {
        return item->anchor(towards);
    }
    return {};
}

void SchematicScene::updateWire(const QString &wireId)
{
    WireItem *item = m_wires.value(wireId);
    const Wire *wire = m_model->find<Wire>(wireId);
    if (!item || !wire)
        return;
    const Harness &h = m_model->harness();
    const WireSheets sheets = wireSheets(h, *wire);
    if (sheets.drawnOn(m_sheet)) {
        item->setVisible(true);
        // A dragged route is kept per sheet, like item positions.
        const std::optional<QPointF> route = h.schematic.positionOn(wireId, m_sheet);
        item->setRoute(endAnchor(*wire, false), endAnchor(*wire, true),
                       route ? std::optional(route->x()) : std::nullopt);
        return;
    }
    const bool aHere = sheets.sheets.empty() && sheets.tagSheetA == m_sheet;
    const bool bHere = sheets.sheets.empty() && sheets.tagSheetB == m_sheet && !aHere;
    item->setVisible(aHere || bHere);
    if (aHere || bHere) {
        const WireEnd &far = aHere ? wire->endB : wire->endA;
        const int farSheet = h.schematic.sheetIndex(aHere ? sheets.tagSheetB : sheets.tagSheetA);
        item->setOffSheet(endAnchor(*wire, !aHere), QStringLiteral("%1 (%2)").arg(
                                                         describeWireEnd(h, far), sheetDisplayName(h.schematic, farSheet)));
    }
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
        if (NoteItem *item = m_notes.value(id)) {
            item->setNote(*m_model->find<Note>(id));
            applyNoteSheet(item);
        }
        break;
    case EntityKind::Connector:
        if (auto *item = m_connectors.value(id)) {
            item->setConnector(*m_model->find<Connector>(id));
            updateWiresOf(id);
        }
        break;
    case EntityKind::Splice:
        if (auto *item = m_splices.value(id)) {
            item->setSplice(*m_model->find<Splice>(id));
            updateWiresOf(id); // off-sheet tags name it
        }
        break;
    case EntityKind::FreeEnd:
        if (auto *item = m_freeEnds.value(id)) {
            item->setFreeEnd(*m_model->find<FreeEnd>(id));
            updateWiresOf(id);
        }
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
    } else if (const QString suffix = Schematic::wireSheetKey({}); key.endsWith(suffix)) {
        updateWire(key.chopped(suffix.size())); // the sheet the wire is drawn on
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
    followSelectionSheet(s);
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

void SchematicScene::followSelectionSheet(const Selection &s)
{
    // Sheets of the selected items, in model order; stay if any of them is shown here.
    const Harness &h = m_model->harness();
    std::vector<QString> sheets;
    auto addItem = [&](const QString &key) {
        const std::vector<QString> on = h.schematic.sheetsOf(key);
        sheets.insert(sheets.end(), on.begin(), on.end());
    };
    for (const Connector &c : h.connectors) {
        if (s.connectors.contains(c.id))
            addItem(c.id);
    }
    for (const Splice &sp : h.splices) {
        if (s.splices.contains(sp.id))
            addItem(sp.id);
    }
    for (const FreeEnd &f : h.freeEnds) {
        if (s.freeEnds.contains(f.id))
            addItem(f.id);
    }
    for (const Wire &w : h.wires) {
        if (s.wires.contains(w.id)) {
            const WireSheets on = wireSheets(h, w);
            sheets.insert(sheets.end(), on.sheets.begin(), on.sheets.end());
            for (const QString &sheet : {on.tagSheetA, on.tagSheetB}) {
                if (!sheet.isEmpty())
                    sheets.push_back(sheet);
            }
        }
    }
    for (const Note &n : h.notes) {
        if (n.canvas == NoteCanvas::Schematic && s.notes.contains(n.id))
            sheets.push_back(h.schematic.resolvedSheetId(n.sheetId));
    }
    if (sheets.empty() || std::find(sheets.begin(), sheets.end(), m_sheet) != sheets.end())
        return;
    setCurrentSheet(sheets.front());
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
