// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SchematicView.h"

#include "Document.h"
#include "SelectionModel.h"
#include "calc/Highlight.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "io/HarnessJson.h"
#include "model/Fragment.h"
#include "model/Queries.h"
#include "schematic/SchematicScene.h"
#include "ui/ConnectorEdits.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace tbh::app {

namespace {

constexpr auto kFragmentMime = "application/x-twisted-harness-fragment";

} // namespace

SchematicView::SchematicView(Document *document, SelectionModel *selection, QWidget *parent)
    : CanvasView(parent)
    , m_document(document)
    , m_selection(selection)
    , m_scene(new SchematicScene(document->model(), selection, this))
{
    setScene(m_scene);
    setAccessibleName(tr("Schematic editor"));
    setAccessibleDescription(tr("Connectors, splices and wires. Press ] or [ to move between items, arrow "
                                "keys to move the selection, F2 to rename, Delete to delete."));
    centerOn(QPointF(400, 300));

    connect(document->model(), &HarnessModel::harnessReset, this, [this] {
        closeEditor();
        cancelWire();
        m_pasteCount = 0;
        QTimer::singleShot(0, this, &SchematicView::zoomToFit);
    });
}

void SchematicView::setTool(Tool tool)
{
    m_tool = tool;
    cancelWire();
    viewport()->setCursor(tool == Tool::Wire ? Qt::CrossCursor : Qt::ArrowCursor);
}

void SchematicView::setSnapToGrid(bool on)
{
    m_scene->setSnapToGrid(on);
}

// ---- mouse --------------------------------------------------------------------

SchematicView::DropTarget SchematicView::targetAt(const QPointF &scenePos) const
{
    DropTarget target;
    for (QGraphicsItem *item : m_scene->items(scenePos)) {
        if (item->type() == ConnectorItemType) {
            auto *connector = static_cast<ConnectorItem *>(item);
            const auto hit = connector->hitTest(connector->mapFromScene(scenePos));
            if (hit.row >= 0) {
                target.connector = connector;
                target.row = hit.row;
                target.end = PinRef{connector->id(), connector->connector().pins[size_t(hit.row)].id};
            }
            return target;
        }
        if (item->type() == SpliceItemType) {
            target.splice = static_cast<SpliceItem *>(item);
            target.end = SpliceRef{target.splice->id()};
            return target;
        }
        if (item->type() == FreeEndItemType) {
            target.freeEnd = static_cast<FreeEndItem *>(item);
            target.end = FreeEndRef{target.freeEnd->id()};
            return target;
        }
    }
    return target;
}

bool SchematicView::isLeadAt(const QPointF &scenePos) const
{
    for (QGraphicsItem *item : m_scene->items(scenePos)) {
        if (item->type() == ConnectorItemType) {
            auto *connector = static_cast<ConnectorItem *>(item);
            return connector->hitTest(connector->mapFromScene(scenePos)).part == ConnectorItem::Part::PinStub;
        }
        if (item->type() == SpliceItemType)
            return static_cast<SpliceItem *>(item)->isStubAt(item->mapFromScene(scenePos));
        if (item->type() == FreeEndItemType)
            return static_cast<FreeEndItem *>(item)->isStubAt(item->mapFromScene(scenePos));
    }
    return false;
}

void SchematicView::setHover(const DropTarget &target)
{
    if (m_hoverConnector && m_hoverConnector != target.connector)
        m_hoverConnector->setHoverRow(-1);
    if (m_hoverSplice && m_hoverSplice != target.splice)
        m_hoverSplice->setHighlighted(false);
    if (m_hoverFreeEnd && m_hoverFreeEnd != target.freeEnd)
        m_hoverFreeEnd->setHighlighted(false);
    m_hoverConnector = target.connector;
    m_hoverSplice = target.splice;
    m_hoverFreeEnd = target.freeEnd;
    if (m_hoverConnector)
        m_hoverConnector->setHoverRow(target.row);
    if (m_hoverSplice)
        m_hoverSplice->setHighlighted(true);
    if (m_hoverFreeEnd)
        m_hoverFreeEnd->setHighlighted(true);
}

void SchematicView::mousePressEvent(QMouseEvent *event)
{
    closeEditor();
    if (event->button() != Qt::LeftButton) {
        CanvasView::mousePressEvent(event);
        return;
    }

    const QPointF scenePos = mapToScene(event->pos());
    if (const auto endB = selectedWireEndAt(event->pos())) {
        m_reattachWire = *m_selection->selection().wires.begin();
        m_reattachEndB = *endB;
        m_pressPos = event->pos();
        m_preview = new QGraphicsPathItem;
        m_preview->setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
        m_preview->setZValue(10);
        m_scene->addItem(m_preview);
        return;
    }
    QGraphicsItem *item = itemAt(event->pos());
    if (item && item->type() == ConnectorItemType) {
        auto *connector = static_cast<ConnectorItem *>(item);
        const auto hit = connector->hitTest(connector->mapFromScene(scenePos));
        switch (hit.part) {
        case ConnectorItem::Part::Collapse:
            toggleCollapsed(connector);
            return;
        case ConnectorItem::Part::Minus:
            changePinCount(this, m_document, connector->id(), connector->rowCount() - 1);
            return;
        case ConnectorItem::Part::Plus:
            changePinCount(this, m_document, connector->id(), connector->rowCount() + 1);
            return;
        case ConnectorItem::Part::Count:
            editPinCount(connector->id());
            return;
        case ConnectorItem::Part::PinLabel:
        case ConnectorItem::Part::PinCircuit:
        case ConnectorItem::Part::PinStub:
            if (!(event->modifiers() & Qt::ControlModifier) && !connector->isSelected()) {
                m_scene->clearSelection();
                connector->setSelected(true);
            }
            beginWire(PinRef{connector->id(), connector->connector().pins[size_t(hit.row)].id}, event->pos());
            return;
        default:
            break;
        }
    } else if (item && item->type() == SpliceItemType
               && (m_tool == Tool::Wire || static_cast<SpliceItem *>(item)->isStubAt(item->mapFromScene(scenePos)))) {
        beginWire(SpliceRef{static_cast<SpliceItem *>(item)->id()}, event->pos());
        return;
    } else if (item && item->type() == FreeEndItemType
               && (m_tool == Tool::Wire || static_cast<FreeEndItem *>(item)->isStubAt(item->mapFromScene(scenePos)))) {
        beginWire(FreeEndRef{static_cast<FreeEndItem *>(item)->id()}, event->pos());
        return;
    }

    CanvasView::mousePressEvent(event);
    if (item && item->type() == WireItemType && m_tool == Tool::Select
        && !static_cast<WireItem *>(item)->isOffSheet()) {
        auto *wire = static_cast<WireItem *>(item);
        m_routeWire = wire->id();
        m_routeDragging = false;
        m_routePressX = scenePos.x();
        m_routeStartX = wire->runX();
        m_pressPos = event->pos();
        return;
    }
    rememberPressPositions();
}

void SchematicView::mouseMoveEvent(QMouseEvent *event)
{
    if (isPanning()) {
        CanvasView::mouseMoveEvent(event);
        return;
    }
    if (m_wirePending && !m_wireDrawing
        && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_wireDrawing = true;
        m_preview = new QGraphicsPathItem;
        QPen pen(palette().color(QPalette::Highlight), 2, Qt::DashLine);
        m_preview->setPen(pen);
        m_preview->setZValue(10);
        m_scene->addItem(m_preview);
    }
    if (m_wireDrawing) {
        updateWirePreview(mapToScene(event->pos()));
        return;
    }
    if (m_wirePending)
        return;
    if (!m_reattachWire.isEmpty()) {
        const QPointF scenePos = mapToScene(event->pos());
        const DropTarget target = targetAt(scenePos);
        setHover(target);
        const Wire *wire = m_document->model()->find<Wire>(m_reattachWire);
        Anchor end{scenePos, 0};
        const QPointF from = wire ? m_scene->endAnchor(*wire, !m_reattachEndB).point : scenePos;
        if (target.connector)
            end = target.connector->pinAnchor(target.row, from);
        else if (target.splice)
            end = target.splice->anchor();
        else if (target.freeEnd)
            end = target.freeEnd->anchor(from);
        if (wire)
            m_preview->setPath(WireItem::routePath(m_scene->endAnchor(*wire, !m_reattachEndB), end));
        return;
    }
    if (!m_routeWire.isEmpty() && (event->buttons() & Qt::LeftButton)) {
        if (!m_routeDragging
            && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
            m_routeDragging = true;
        if (m_routeDragging) {
            if (WireItem *wire = m_scene->wireItem(m_routeWire)) {
                qreal x = m_routeStartX + mapToScene(event->pos()).x() - m_routePressX;
                if (m_scene->snapToGrid())
                    x = SchematicScene::snapped(QPointF(x, 0)).x();
                wire->setRunX(x);
            }
        }
        return;
    }
    if (event->buttons() == Qt::NoButton && m_tool == Tool::Select) {
        const QPointF scenePos = mapToScene(event->pos());
        const QGraphicsItem *item = itemAt(event->pos());
        Qt::CursorShape cursor = Qt::ArrowCursor;
        if (isLeadAt(scenePos))
            cursor = Qt::CrossCursor;
        else if (item && item->type() == WireItemType)
            cursor = Qt::SizeHorCursor;
        viewport()->setCursor(cursor);
    }
    CanvasView::mouseMoveEvent(event);
}

void SchematicView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        CanvasView::mouseReleaseEvent(event);
        viewport()->setCursor(m_tool == Tool::Wire ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
    if (m_wirePending && event->button() == Qt::LeftButton) {
        if (m_wireDrawing) {
            finishWire(mapToScene(event->pos()));
        } else {
            // A plain click on a pin row selects that pin (for highlighting).
            const WireEnd start = m_wireStart;
            cancelWire();
            if (const auto *pin = std::get_if<PinRef>(&start)) {
                Selection selection;
                if (event->modifiers() & Qt::ControlModifier)
                    selection = m_selection->selection();
                selection.connectors.insert(pin->connectorId);
                selection.pins.insert(calc::pinKey(pin->connectorId, pin->pinId));
                m_selection->setSelection(selection);
            }
        }
        return;
    }
    if (!m_reattachWire.isEmpty() && event->button() == Qt::LeftButton) {
        if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            cancelWire(); // a click on the end, not a drag
        else
            finishReattach(mapToScene(event->pos()));
        return;
    }
    if (!m_routeWire.isEmpty() && event->button() == Qt::LeftButton) {
        CanvasView::mouseReleaseEvent(event);
        finishRouteDrag();
        return;
    }
    CanvasView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton) {
        setHover({});
        commitMoves();
    }
}

void SchematicView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        CanvasView::mouseDoubleClickEvent(event);
        return;
    }
    cancelWire();
    const QPointF scenePos = mapToScene(event->pos());
    QGraphicsItem *item = itemAt(event->pos());
    if (!item)
        return;
    switch (item->type()) {
    case ConnectorItemType: {
        auto *connector = static_cast<ConnectorItem *>(item);
        const auto hit = connector->hitTest(connector->mapFromScene(scenePos));
        if (hit.part == ConnectorItem::Part::PinLabel)
            editPinCell(connector->id(), hit.row, 0);
        else if (hit.part == ConnectorItem::Part::PinCircuit)
            editPinCell(connector->id(), hit.row, 1);
        else if (hit.part == ConnectorItem::Part::Title)
            editConnectorTitle(connector->id());
        return;
    }
    case WireItemType:
        if (auto *wire = static_cast<WireItem *>(item); wire->tagRect().contains(scenePos)) {
            goToOtherSheet(wire->id());
            return;
        }
        [[fallthrough]];
    case SpliceItemType:
    case FreeEndItemType:
        m_scene->clearSelection();
        item->setSelected(true);
        renameSelection();
        return;
    case NoteItemType:
        editNote(static_cast<NoteItem *>(item)->id());
        return;
    default:
        return;
    }
}

bool SchematicView::wheelOverItem(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
        return false;
    QGraphicsItem *item = itemAt(event->position().toPoint());
    if (!item || item->type() != ConnectorItemType)
        return false;
    auto *connector = static_cast<ConnectorItem *>(item);
    if (!connector->isScrollMode())
        return false;
    const int rows = -event->angleDelta().y() / 40; // 3 rows per notch
    if (rows != 0 && connector->scrollRows(rows))
        m_scene->itemMoved(connector); // re-anchor wires
    return true;
}

void SchematicView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_wirePending || !m_reattachWire.isEmpty()) {
            cancelWire();
        } else if (!m_routeWire.isEmpty()) {
            cancelRouteDrag();
        } else {
            m_scene->clearSelection();
            find({});
        }
        return;
    }
    CanvasView::keyPressEvent(event);
}

// ---- wire drawing ---------------------------------------------------------------

void SchematicView::beginWire(const WireEnd &start, const QPoint &pressPos)
{
    m_wirePending = true;
    m_wireDrawing = false;
    m_wireStart = start;
    m_pressPos = pressPos;
}

Anchor SchematicView::startAnchor(const QPointF &towards) const
{
    if (const auto *pin = std::get_if<PinRef>(&m_wireStart)) {
        if (auto *item = m_scene->connectorItem(pin->connectorId))
            return item->pinAnchor(item->rowOfPin(pin->pinId), towards);
    } else if (const auto *splice = std::get_if<SpliceRef>(&m_wireStart)) {
        if (auto *item = m_scene->spliceItem(splice->spliceId))
            return item->anchor();
    } else if (auto *item = m_scene->freeEndItem(std::get<FreeEndRef>(m_wireStart).freeEndId)) {
        return item->anchor(towards);
    }
    return {};
}

void SchematicView::updateWirePreview(const QPointF &scenePos)
{
    const DropTarget target = targetAt(scenePos);
    setHover(target);
    Anchor end{scenePos, 0};
    if (target.connector)
        end = target.connector->pinAnchor(target.row, startAnchor(scenePos).point);
    else if (target.splice)
        end = target.splice->anchor();
    else if (target.freeEnd)
        end = target.freeEnd->anchor(startAnchor(scenePos).point);
    m_preview->setPath(WireItem::routePath(startAnchor(end.point), end));
}

void SchematicView::finishWire(const QPointF &scenePos)
{
    const DropTarget target = targetAt(scenePos);
    const WireEnd start = m_wireStart;
    cancelWire();
    if (target.end && *target.end == start)
        return; // dropped back on the start

    Wire wire = newWire(start);
    const QString id = wire.id;
    QUndoStack *stack = m_document->undoStack();
    stack->beginMacro(tr("Add wire"));
    // Dropped on empty space: the wire ends in a new flying lead there.
    wire.endB = target.end ? *target.end : WireEnd(addFreeEndAt(FreeEndKind::FlyingLead, scenePos));
    // addWire() also names the wire and copies circuit text to an empty pin at the other end.
    SchematicPlacement drawnOn; // the sheet it is drawn on (its ends may be on others too)
    drawnOn.sheetId = m_scene->currentSheet();
    const QString wireSheet = Schematic::wireSheetKey(wire.id);
    stack->push(addWire(m_document->model(), std::move(wire), {{wireSheet, drawnOn}}, tr("Add wire")).release());
    stack->endMacro();
    Selection selection;
    selection.wires.insert(id);
    m_selection->setSelection(selection);
}

Wire SchematicView::newWire(const WireEnd &start) const
{
    const WireStyle &style = m_document->wireStyle();
    Wire wire;
    wire.id = newId();
    wire.endA = start;
    wire.baseColor = style.baseColor;
    wire.stripeColor = style.stripeColor;
    wire.gauge = style.gauge;
    wire.wireTypeId = style.wireTypeId;
    return wire;
}

void SchematicView::cancelWire()
{
    m_reattachWire.clear();
    m_wirePending = false;
    m_wireDrawing = false;
    delete m_preview;
    m_preview = nullptr;
    setHover({});
}

// ---- re-attach --------------------------------------------------------------------

std::optional<bool> SchematicView::selectedWireEndAt(const QPoint &viewPos) const
{
    const Selection &s = m_selection->selection();
    if (s.wires.size() != 1 || !s.connectors.isEmpty() || !s.splices.isEmpty() || !s.freeEnds.isEmpty())
        return std::nullopt;
    const Wire *wire = m_document->model()->find<Wire>(*s.wires.begin());
    if (!wire)
        return std::nullopt;
    constexpr int kGrabRadius = 8; // pixels
    for (bool endB : {false, true}) {
        if (!m_scene->isEndOnCurrentSheet(*wire, endB))
            continue; // that end is on another sheet
        const QPoint anchor = mapFromScene(m_scene->endAnchor(*wire, endB).point);
        if ((anchor - viewPos).manhattanLength() <= kGrabRadius)
            return endB;
    }
    return std::nullopt;
}

void SchematicView::finishReattach(const QPointF &scenePos)
{
    const QString wireId = m_reattachWire;
    const bool endB = m_reattachEndB;
    cancelWire();
    const DropTarget target = targetAt(scenePos);
    QUndoStack *stack = m_document->undoStack();
    if (target.end) {
        if (auto command = setWireEnd(m_document->model(), wireId, endB, *target.end))
            stack->push(command.release());
    } else {
        // Dropped on empty space: the end moves to a new flying lead there.
        stack->beginMacro(tr("Re-attach wire"));
        const FreeEndRef end = addFreeEndAt(FreeEndKind::FlyingLead, scenePos);
        stack->push(setWireEnd(m_document->model(), wireId, endB, end).release());
        stack->endMacro();
    }
    Selection selection;
    selection.wires.insert(wireId);
    m_selection->setSelection(selection);
}

// ---- moving -----------------------------------------------------------------------

std::vector<PlacedItem *> SchematicView::selectedPlacedItems() const
{
    std::vector<PlacedItem *> items;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        const int type = item->type();
        if (type == ConnectorItemType || type == SpliceItemType || type == FreeEndItemType)
            items.push_back(static_cast<PlacedItem *>(item));
    }
    return items;
}

void SchematicView::finishRouteDrag()
{
    const QString wireId = std::exchange(m_routeWire, {});
    const bool dragged = std::exchange(m_routeDragging, false);
    WireItem *wire = m_scene->wireItem(wireId);
    if (!dragged || !wire || wire->runX() == m_routeStartX)
        return;
    // Kept per sheet: the wire may be shown on several.
    m_document->undoStack()->push(new SetPlacementsCommand(
        m_document->model(), {{wireId, placementAt(wireId, QPointF(wire->runX(), 0))}}, tr("Move wire")));
}

void SchematicView::cancelRouteDrag()
{
    if (m_routeDragging)
        m_scene->updateWire(m_routeWire); // back to the stored route
    m_routeWire.clear();
    m_routeDragging = false;
}

void SchematicView::rememberPressPositions()
{
    m_pressPositions.clear();
    m_notePressPositions.clear();
    for (PlacedItem *item : selectedPlacedItems())
        m_pressPositions[item->placementKey()] = item->pos();
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (item->type() == NoteItemType)
            m_notePressPositions[static_cast<NoteItem *>(item)->id()] = item->pos();
    }
}

SchematicPlacement SchematicView::placementAt(const QString &key, const QPointF &position) const
{
    const Schematic &schematic = m_document->model()->harness().schematic;
    const SchematicPlacement *old = m_document->model()->placement(key);
    SchematicPlacement placement = old ? *old : SchematicPlacement{};
    if (!old)
        placement.sheetId = m_scene->currentSheet(); // a new placement starts on this sheet
    placement.setPositionOn(schematic.resolvedSheetId(placement.sheetId), m_scene->currentSheet(), position);
    return placement;
}

void SchematicView::commitMoves()
{
    std::map<QString, std::optional<SchematicPlacement>> changes;
    for (const auto &[key, before] : m_pressPositions) {
        PlacedItem *item = m_scene->placedItem(key);
        if (!item || item->pos() == before)
            continue;
        changes[key] = placementAt(key, item->pos());
    }
    m_pressPositions.clear();
    HarnessModel *model = m_document->model();
    auto macro = std::make_unique<QUndoCommand>(tr("Move"));
    if (!changes.empty())
        new SetPlacementsCommand(model, changes, {}, macro.get());
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
    if (macro->childCount() > 0)
        m_document->undoStack()->push(macro.release());
}

bool SchematicView::nudgeSelection(const QPointF &delta)
{
    rememberPressPositions();
    if (m_pressPositions.empty() && m_notePressPositions.empty())
        return false;
    for (PlacedItem *item : selectedPlacedItems())
        item->setPos(item->pos() + delta);
    for (const auto &[id, pos] : m_notePressPositions)
        m_scene->noteItem(id)->setPos(pos + delta);
    commitMoves();
    return true;
}

void SchematicView::toggleCollapsed(ConnectorItem *item)
{
    setConnectorDisplay(item->id(), !item->isCollapsed(), item->maxVisibleRows());
}

void SchematicView::setConnectorDisplay(const QString &connectorId, bool collapsed, int maxVisibleRows)
{
    ConnectorItem *item = m_scene->connectorItem(connectorId);
    if (!item || (item->isCollapsed() == collapsed && item->maxVisibleRows() == maxVisibleRows))
        return;
    SchematicPlacement placement = placementAt(connectorId, item->pos());
    placement.collapsed = collapsed;
    placement.maxVisibleRows = maxVisibleRows;
    QString text = tr("Change connector display");
    if (collapsed != item->isCollapsed())
        text = collapsed ? tr("Collapse connector") : tr("Expand connector");
    m_document->undoStack()->push(
        new SetPlacementsCommand(m_document->model(), {{connectorId, placement}}, text));
}

// ---- inline editing ---------------------------------------------------------------

void SchematicView::editPinCell(const QString &connectorId, int row, int column)
{
    ConnectorItem *item = m_scene->connectorItem(connectorId);
    if (!item || row < 0 || row >= item->rowCount())
        return;
    if (item->isCollapsed())
        return;
    item->ensureRowVisible(row);
    m_scene->itemMoved(item);
    ensureVisible(item->mapRectToScene(item->rowRect(row)), 40, 40);
    const Pin &pin = item->connector().pins[size_t(row)];
    const QRectF cell = column == 0 ? item->labelCellRect(row) : item->circuitCellRect(row);
    const QString pinId = pin.id;
    editText(item->mapRectToScene(cell), column == 0 ? pin.label : pin.circuitText,
             [this, connectorId, pinId, row, column](const QString &value, Nav nav) {
                 HarnessModel *model = m_document->model();
                 const Connector *current = model->find<Connector>(connectorId);
                 if (!current)
                     return;
                 Connector updated = *current;
                 for (Pin &p : updated.pins) {
                     if (p.id != pinId)
                         continue;
                     QString &field = column == 0 ? p.label : p.circuitText;
                     if (field != value) {
                         field = value;
                         m_document->undoStack()->push(
                             updateConnector(model, updated,
                                             column == 0 ? tr("Edit pin label") : tr("Edit circuit text"))
                                 .release());
                     }
                     break;
                 }
                 int nextRow = row;
                 int nextColumn = column;
                 switch (nav) {
                 case Nav::None:
                     return;
                 case Nav::Down:
                     ++nextRow;
                     break;
                 case Nav::Up:
                     --nextRow;
                     break;
                 case Nav::Next:
                     nextColumn = column == 0 ? 1 : 0;
                     nextRow += column == 0 ? 0 : 1;
                     break;
                 case Nav::Previous:
                     nextColumn = column == 0 ? 1 : 0;
                     nextRow -= column == 0 ? 1 : 0;
                     break;
                 }
                 QTimer::singleShot(0, this, [this, connectorId, nextRow, nextColumn] {
                     editPinCell(connectorId, nextRow, nextColumn);
                 });
             });
}

void SchematicView::editConnectorTitle(const QString &connectorId)
{
    ConnectorItem *item = m_scene->connectorItem(connectorId);
    if (!item)
        return;
    editText(item->mapRectToScene(item->titleRect()), item->connector().label,
             [this, connectorId](const QString &value, Nav) {
                 HarnessModel *model = m_document->model();
                 const Connector *current = model->find<Connector>(connectorId);
                 if (!current || current->label == value)
                     return;
                 Connector updated = *current;
                 updated.label = value;
                 m_document->undoStack()->push(
                     new UpdateEntityCommand<Connector>(model, updated, tr("Rename connector")));
             });
}

void SchematicView::editPinCount(const QString &connectorId)
{
    ConnectorItem *item = m_scene->connectorItem(connectorId);
    if (!item)
        return;
    editText(item->mapRectToScene(item->countRect()), QString::number(item->rowCount()),
             [this, connectorId](const QString &value, Nav) {
                 bool ok = false;
                 const int count = value.trimmed().toInt(&ok);
                 if (!ok || count < kMinPins || count > kMaxPins) {
                     emit statusMessage(tr("Pin count must be a whole number from %1 to %2.")
                                            .arg(kMinPins)
                                            .arg(kMaxPins));
                     return;
                 }
                 // Defer: a confirmation dialog must not open inside the editor's focus-out.
                 QTimer::singleShot(0, this, [this, connectorId, count] {
                     changePinCount(this, m_document, connectorId, count);
                 });
             });
}

void SchematicView::renameSelection()
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.size() != 1)
        return;
    QGraphicsItem *item = selected.first();
    HarnessModel *model = m_document->model();
    switch (item->type()) {
    case ConnectorItemType:
        editConnectorTitle(static_cast<ConnectorItem *>(item)->id());
        return;
    case SpliceItemType: {
        const QString id = static_cast<SpliceItem *>(item)->id();
        editText(item->mapRectToScene(QRectF(-60, -28, 120, 18)), model->find<Splice>(id)->label,
                 [this, id](const QString &value, Nav) {
                     HarnessModel *m = m_document->model();
                     Splice s = *m->find<Splice>(id);
                     if (s.label == value)
                         return;
                     s.label = value;
                     m_document->undoStack()->push(new UpdateEntityCommand<Splice>(m, s, tr("Rename splice")));
                 });
        return;
    }
    case FreeEndItemType: {
        const QString id = static_cast<FreeEndItem *>(item)->id();
        editText(item->mapRectToScene(QRectF(-60, -30, 120, 18)), model->find<FreeEnd>(id)->label,
                 [this, id](const QString &value, Nav) {
                     HarnessModel *m = m_document->model();
                     FreeEnd f = *m->find<FreeEnd>(id);
                     f.label = value;
                     if (auto command = updateFreeEnd(m, f, tr("Rename free end")))
                         m_document->undoStack()->push(command.release());
                 });
        return;
    }
    case WireItemType: {
        const QString wireId = static_cast<WireItem *>(item)->id();
        const QPointF center = item->boundingRect().center();
        editText(QRectF(center - QPointF(90, 10), QSizeF(180, 20)), model->find<Wire>(wireId)->name,
                 [this, wireId](const QString &value, Nav) {
                     HarnessModel *m = m_document->model();
                     Wire w = *m->find<Wire>(wireId);
                     if (w.name == value)
                         return;
                     w.name = value;
                     m_document->undoStack()->push(new UpdateEntityCommand<Wire>(m, w, tr("Rename wire")));
                 });
        return;
    }
    default:
        return;
    }
}

// ---- actions ------------------------------------------------------------------------

QString SchematicView::uniqueLabel(const QString &prefix, bool splice) const
{
    QSet<QString> used;
    const Harness &h = m_document->model()->harness();
    if (splice) {
        for (const Splice &s : h.splices)
            used.insert(s.label);
    } else {
        for (const Connector &c : h.connectors)
            used.insert(c.label);
    }
    for (int n = 1;; ++n) {
        const QString label = prefix + QString::number(n);
        if (!used.contains(label))
            return label;
    }
}

QPointF SchematicView::viewCenterSnapped() const
{
    return SchematicScene::snapped(mapToScene(viewport()->rect().center()));
}

QPointF SchematicView::unoccupiedPosition(QPointF position) const
{
    // Step diagonally past any connector or splice already sitting exactly here, so a new item
    // added repeatedly at the view center is visibly offset instead of stacked out of sight.
    const Harness &h = m_document->model()->harness();
    QSet<QString> ids;
    for (const Connector &c : h.connectors)
        ids.insert(c.id);
    for (const Splice &s : h.splices)
        ids.insert(s.id);
    for (const FreeEnd &f : h.freeEnds)
        ids.insert(f.id);
    const QString sheet = m_scene->currentSheet();
    auto occupied = [&](const QPointF &p) {
        for (const QString &key : std::as_const(ids)) {
            if (const auto pos = h.schematic.positionOn(key, sheet); pos && (*pos - p).manhattanLength() < 1)
                return true;
        }
        return false;
    };
    const QPointF step(2 * kCanvasGrid, 2 * kCanvasGrid);
    while (occupied(position))
        position += step;
    return position;
}

void SchematicView::addConnector()
{
    Connector connector;
    connector.id = newId();
    connector.label = uniqueLabel(QStringLiteral("J"), false);
    connector.pins = resizedPins({}, 1);
    SchematicPlacement placement;
    placement.position =
        unoccupiedPosition(SchematicScene::snapped(viewCenterSnapped() - QPointF(ConnectorItem::kWidth / 2, 40)));
    placement.sheetId = m_scene->currentSheet();
    const QString id = connector.id;
    m_document->undoStack()->push(new AddEntityCommand<Connector>(
        m_document->model(), std::move(connector), {{id, placement}}, tr("Add connector")));
    Selection selection;
    selection.connectors.insert(id);
    m_selection->setSelection(selection);
}

void SchematicView::addSplice()
{
    Splice splice;
    splice.id = newId();
    splice.label = uniqueLabel(QStringLiteral("S"), true);
    SchematicPlacement placement;
    placement.position = unoccupiedPosition(viewCenterSnapped());
    placement.sheetId = m_scene->currentSheet();
    const QString id = splice.id;
    m_document->undoStack()->push(new AddEntityCommand<Splice>(
        m_document->model(), std::move(splice), {{id, placement}}, tr("Add splice")));
    Selection selection;
    selection.splices.insert(id);
    m_selection->setSelection(selection);
}

FreeEndRef SchematicView::addFreeEndAt(FreeEndKind kind, const QPointF &scenePos)
{
    FreeEnd end;
    end.id = newId();
    end.kind = kind;
    SchematicPlacement placement;
    placement.position = SchematicScene::snapped(scenePos);
    placement.sheetId = m_scene->currentSheet();
    const QString id = end.id;
    m_document->undoStack()->push(new AddEntityCommand<FreeEnd>(m_document->model(), std::move(end),
                                                                {{id, placement}}, tr("Add free end")));
    return FreeEndRef{id};
}

void SchematicView::addFreeEnd(FreeEndKind kind)
{
    FreeEnd end;
    end.id = newId();
    end.kind = kind;
    SchematicPlacement placement;
    placement.position = unoccupiedPosition(viewCenterSnapped());
    placement.sheetId = m_scene->currentSheet();
    const QString id = end.id;
    m_document->undoStack()->push(
        new AddEntityCommand<FreeEnd>(m_document->model(), std::move(end), {{id, placement}},
                                      kind == FreeEndKind::Eyelet ? tr("Add eyelet") : tr("Add flying lead")));
    Selection selection;
    selection.freeEnds.insert(id);
    m_selection->setSelection(selection);
}

void SchematicView::addNote()
{
    const QPointF position = viewCenterSnapped();
    editText(QRectF(position, QSizeF(NoteItem::kWidth, 18)), {}, [this, position](const QString &value, Nav) {
        const QString text = value.trimmed();
        if (text.isEmpty())
            return; // nothing typed: no note
        Note note{newId(), NoteCanvas::Schematic, text, position, m_scene->currentSheet(), {}};
        const QString id = note.id;
        m_document->undoStack()->push(
            new AddEntityCommand<Note>(m_document->model(), std::move(note), {}, tr("Add note")));
        Selection selection;
        selection.notes.insert(id);
        m_selection->setSelection(selection);
    });
}

void SchematicView::editNote(const QString &noteId)
{
    NoteItem *item = m_scene->noteItem(noteId);
    if (!item)
        return;
    editText(QRectF(item->pos(), QSizeF(NoteItem::kWidth, 18)), item->note().text,
             [this, noteId](const QString &value, Nav) {
                 if (auto command = setNoteText(m_document->model(), noteId, value)) // empty text deletes
                     m_document->undoStack()->push(command.release());
             });
}

void SchematicView::deleteSelection()
{
    // Items and wires also shown on other sheets only leave this one: deleting them would
    // delete them (and the items' wiring) on every sheet.
    Selection s = m_selection->selection();
    const Harness &h = m_document->model()->harness();
    const Schematic &schematic = h.schematic;
    QSet<QString> shared;
    for (QSet<QString> *ids : {&s.connectors, &s.splices, &s.freeEnds}) {
        for (auto it = ids->begin(); it != ids->end();) {
            if (m_scene->isOnCurrentSheet(*it) && schematic.sheetsOf(*it).size() > 1) {
                shared.insert(*it);
                it = ids->erase(it);
            } else {
                ++it;
            }
        }
    }
    QSet<QString> sharedWires;
    for (auto it = s.wires.begin(); it != s.wires.end();) {
        const Wire *wire = m_document->model()->find<Wire>(*it);
        const WireSheets sheets = wire ? wireSheets(h, *wire) : WireSheets{};
        if (sheets.drawnOn(m_scene->currentSheet()) && sheets.sheets.size() > 1) {
            sharedWires.insert(*it);
            it = s.wires.erase(it);
        } else {
            ++it;
        }
    }
    QUndoStack *stack = m_document->undoStack();
    stack->beginMacro(tr("Delete"));
    if (auto command = removeFromSheet(m_document->model(), shared, sharedWires, m_scene->currentSheet())) {
        stack->push(command.release());
        emit statusMessage(tr("Removed from this sheet only: still shown on other sheets."));
    }
    if (!s.connectors.isEmpty() || !s.splices.isEmpty() || !s.freeEnds.isEmpty() || !s.wires.isEmpty()
        || !s.notes.isEmpty())
        deleteItems(this, m_document, s);
    stack->endMacro();
}

void SchematicView::selectAll()
{
    for (QGraphicsItem *item : m_scene->items()) {
        if (item->flags() & QGraphicsItem::ItemIsSelectable)
            item->setSelected(true);
    }
}

QByteArray SchematicView::selectionFragment() const
{
    const Selection &s = m_selection->selection();
    if (s.connectors.isEmpty() && s.splices.isEmpty() && s.freeEnds.isEmpty())
        return {};
    const Harness &h = m_document->model()->harness();
    Harness fragment = extractFragment(h, s.connectors, s.splices, s.freeEnds);
    // Positions as on this sheet, which is where they were copied from.
    for (auto &[key, placement] : fragment.schematic.placements) {
        if (const auto pos = h.schematic.positionOn(key, m_scene->currentSheet()))
            placement.position = *pos;
        placement.alsoOn.clear();
        placement.sheetId.clear();
    }
    // Mark the selected wires: pasted onto another sheet with the same items, those wires
    // are shown there too (see pasteFragment()).
    for (const Wire &w : fragment.wires) {
        if (s.wires.contains(w.id)) {
            SchematicPlacement marker;
            marker.sheetId = fragment.schematic.sheets.front().id;
            fragment.schematic.placements[Schematic::wireSheetKey(w.id)] = marker;
        }
    }
    return io::writeZip(io::serializeHarness(fragment), nullptr).value_or(QByteArray());
}

void SchematicView::copySelection()
{
    const QByteArray bytes = selectionFragment();
    if (bytes.isEmpty()) {
        emit statusMessage(tr("Select connectors, splices, flying leads or eyelets to copy."));
        return;
    }
    auto *mime = new QMimeData;
    mime->setData(QLatin1StringView(kFragmentMime), bytes);
    QApplication::clipboard()->setMimeData(mime);
    m_pasteCount = 0;
}

void SchematicView::pasteFragmentBytes(const QByteArray &bytes, QPointF offset)
{
    QString error;
    const auto entries = io::readZip(bytes, &error);
    auto fragment = entries ? io::deserializeHarness(*entries, &error) : std::nullopt;
    if (!fragment) {
        emit statusMessage(tr("Could not paste: %1").arg(error));
        return;
    }
    QSet<QString> newIds;
    m_document->undoStack()->push(
        pasteFragment(m_document->model(), std::move(*fragment), offset, &newIds, m_scene->currentSheet())
            .release());
    Selection selection;
    for (const QString &id : newIds) {
        if (m_document->model()->find<Connector>(id))
            selection.connectors.insert(id);
        else if (m_document->model()->find<Splice>(id))
            selection.splices.insert(id);
        else
            selection.freeEnds.insert(id);
    }
    m_selection->setSelection(selection);
}

void SchematicView::paste()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QLatin1StringView(kFragmentMime)))
        return;
    ++m_pasteCount;
    pasteFragmentBytes(mime->data(QLatin1StringView(kFragmentMime)), QPointF(40, 40) * m_pasteCount);
}

void SchematicView::duplicateSelection()
{
    const QByteArray bytes = selectionFragment();
    if (!bytes.isEmpty())
        pasteFragmentBytes(bytes, QPointF(40, 40));
}

void SchematicView::align(Qt::Alignment alignment)
{
    const std::vector<PlacedItem *> items = selectedPlacedItems();
    if (items.size() < 2)
        return;
    // Connectors align by their box; point-like items (splices, free ends) by their position.
    auto rectOf = [](PlacedItem *item) {
        if (item->type() == ConnectorItemType)
            return QRectF(item->pos(), static_cast<ConnectorItem *>(item)->bodyRect().size());
        return QRectF(item->pos(), QSizeF(0, 0));
    };
    QRectF bounds;
    for (PlacedItem *item : items)
        bounds = bounds.isNull() ? rectOf(item) : bounds.united(rectOf(item));
    std::map<QString, std::optional<SchematicPlacement>> changes;
    for (PlacedItem *item : items) {
        const QRectF r = rectOf(item);
        QPointF delta;
        if (alignment & Qt::AlignLeft)
            delta.setX(bounds.left() - r.left());
        else if (alignment & Qt::AlignRight)
            delta.setX(bounds.right() - r.right());
        else if (alignment & Qt::AlignHCenter)
            delta.setX(bounds.center().x() - r.center().x());
        if (alignment & Qt::AlignTop)
            delta.setY(bounds.top() - r.top());
        else if (alignment & Qt::AlignBottom)
            delta.setY(bounds.bottom() - r.bottom());
        else if (alignment & Qt::AlignVCenter)
            delta.setY(bounds.center().y() - r.center().y());
        changes[item->placementKey()] = placementAt(item->placementKey(), item->pos() + delta);
    }
    m_document->undoStack()->push(new SetPlacementsCommand(m_document->model(), changes, tr("Align")));
}

void SchematicView::distribute(Qt::Orientation orientation)
{
    std::vector<PlacedItem *> items = selectedPlacedItems();
    if (items.size() < 3)
        return;
    auto coord = [orientation](PlacedItem *item) {
        const QPointF c = item->sceneBoundingRect().center();
        return orientation == Qt::Horizontal ? c.x() : c.y();
    };
    std::sort(items.begin(), items.end(), [&](PlacedItem *a, PlacedItem *b) { return coord(a) < coord(b); });
    const qreal first = coord(items.front());
    const qreal step = (coord(items.back()) - first) / qreal(items.size() - 1);
    std::map<QString, std::optional<SchematicPlacement>> changes;
    for (size_t i = 0; i < items.size(); ++i) {
        const qreal delta = first + step * qreal(i) - coord(items[i]);
        changes[items[i]->placementKey()] = placementAt(
            items[i]->placementKey(),
            items[i]->pos() + (orientation == Qt::Horizontal ? QPointF(delta, 0) : QPointF(0, delta)));
    }
    m_document->undoStack()->push(new SetPlacementsCommand(m_document->model(), changes, tr("Distribute")));
}

int SchematicView::find(const QString &text)
{
    if (text != m_searchText) {
        m_searchText = text;
        m_searchIndex = -1;
        m_scene->setSearchText(text);
    }
    if (text.isEmpty())
        return 0;
    const auto matches = m_scene->searchMatches();
    if (matches.empty()) {
        emit statusMessage(tr("No pins match \"%1\".").arg(text));
        return 0;
    }
    m_searchIndex = (m_searchIndex + 1) % int(matches.size());
    const auto &[connectorId, row] = matches[size_t(m_searchIndex)];
    ConnectorItem *item = m_scene->connectorItem(connectorId);
    if (item->isCollapsed())
        toggleCollapsed(item);
    item->ensureRowVisible(row);
    m_scene->itemMoved(item);
    centerOn(item->mapRectToScene(item->rowRect(row)).center());
    Selection selection;
    selection.connectors.insert(connectorId);
    m_selection->setSelection(selection);
    emit statusMessage(tr("Match %1 of %2: %3 : %4")
                           .arg(m_searchIndex + 1)
                           .arg(matches.size())
                           .arg(item->connector().label, item->connector().pins[size_t(row)].label));
    return int(matches.size());
}

// ---- sheets ------------------------------------------------------------------------

void SchematicView::goToOtherSheet(const QString &wireId)
{
    const Wire *wire = m_document->model()->find<Wire>(wireId);
    if (!wire)
        return;
    const WireSheets sheets = wireSheets(m_document->model()->harness(), *wire);
    if (!sheets.sheets.empty())
        return; // drawn in full: no tag
    const bool aHere = sheets.tagSheetA == m_scene->currentSheet();
    const QString farId = wireEndItemId(aHere ? wire->endB : wire->endA);
    m_scene->setCurrentSheet(aHere ? sheets.tagSheetB : sheets.tagSheetA);
    Selection selection;
    selection.wires.insert(wireId);
    m_selection->setSelection(selection);
    if (PlacedItem *item = m_scene->placedItem(farId))
        centerOn(item);
}

void SchematicView::moveSelectionToSheet(const QString &sheetId)
{
    const Selection &s = m_selection->selection();
    QSet<QString> keys = s.connectors;
    keys.unite(s.splices).unite(s.freeEnds);
    auto command = tbh::app::moveToSheet(m_document->model(), keys, s.notes, m_scene->currentSheet(), sheetId);
    if (!command) {
        emit statusMessage(tr("Select connectors, splices, flying leads, eyelets or notes to move to another sheet."));
        return;
    }
    const Selection moved = s; // hiding the items clears the selection
    m_document->undoStack()->push(command.release());
    // Follow the items to their new sheet.
    m_scene->setCurrentSheet(sheetId);
    m_selection->setSelection(moved);
}

void SchematicView::showSelectionOnSheet(const QString &sheetId)
{
    const Selection &s = m_selection->selection();
    std::map<QString, QPointF> positions;
    for (const QSet<QString> *ids : {&s.connectors, &s.splices, &s.freeEnds}) {
        for (const QString &id : *ids) {
            if (PlacedItem *item = m_scene->placedItem(id))
                positions[id] = item->pos(); // same place as on this sheet
        }
    }
    auto command = showOnSheet(m_document->model(), positions, s.wires, sheetId);
    if (!command) {
        emit statusMessage(tr("Select connectors, splices, flying leads, eyelets or wires that are not on that sheet "
                              "yet (a wire also needs both its ends there)."));
        return;
    }
    const Selection shown = s;
    m_document->undoStack()->push(command.release());
    m_scene->setCurrentSheet(sheetId);
    m_selection->setSelection(shown);
}

void SchematicView::showExistingItem(const QString &id)
{
    const QPointF position = unoccupiedPosition(viewCenterSnapped());
    auto command = showOnSheet(m_document->model(), {{id, position}}, {}, m_scene->currentSheet());
    if (!command)
        return;
    m_document->undoStack()->push(command.release());
    Selection selection;
    if (m_document->model()->find<Connector>(id))
        selection.connectors.insert(id);
    else if (m_document->model()->find<Splice>(id))
        selection.splices.insert(id);
    else
        selection.freeEnds.insert(id);
    m_selection->setSelection(selection);
}

void SchematicView::addExistingItemActions(QMenu *menu)
{
    const Harness &h = m_document->model()->harness();
    const QString sheet = m_scene->currentSheet();
    auto add = [&](const QString &id, const QString &label) {
        if (!h.schematic.isOnSheet(id, sheet))
            menu->addAction(label, this, [this, id] { showExistingItem(id); });
    };
    for (const Connector &c : h.connectors)
        add(c.id, c.label.isEmpty() ? tr("(unnamed connector)") : c.label);
    menu->addSeparator();
    for (const Splice &sp : h.splices)
        add(sp.id, sp.label.isEmpty() ? tr("(unnamed splice)") : sp.label);
    menu->addSeparator();
    for (const FreeEnd &f : h.freeEnds)
        add(f.id, describeFreeEnd(f));
    bool any = false;
    for (QAction *action : menu->actions())
        any = any || !action->isSeparator();
    if (!any) {
        menu->clear();
        menu->addAction(tr("Everything is on this sheet already"))->setEnabled(false);
    }
}

void SchematicView::contextMenuEvent(QContextMenuEvent *event)
{
    QGraphicsItem *item = itemAt(event->pos());
    if (item && !item->isSelected()) {
        m_scene->clearSelection();
        item->setSelected(true);
    }
    const Selection &s = m_selection->selection();
    QMenu menu(this);
    if (!s.connectors.isEmpty() || !s.splices.isEmpty() || !s.freeEnds.isEmpty() || !s.notes.isEmpty()) {
        addMoveToSheetActions(menu.addMenu(tr("Move to Sheet")));
    }
    if (!s.connectors.isEmpty() || !s.splices.isEmpty() || !s.freeEnds.isEmpty() || !s.wires.isEmpty())
        addShowOnSheetActions(menu.addMenu(tr("Also Show on Sheet")));
    addExistingItemActions(menu.addMenu(tr("Add Existing Item to This Sheet")));
    menu.exec(event->globalPos());
}

void SchematicView::addMoveToSheetActions(QMenu *menu)
{
    const Schematic &schematic = m_document->model()->harness().schematic;
    for (int i = 0; i < int(schematic.sheets.size()); ++i) {
        const QString id = schematic.sheets[size_t(i)].id;
        QAction *action = menu->addAction(sheetDisplayName(schematic, i), this, [this, id] { moveSelectionToSheet(id); });
        action->setEnabled(id != m_scene->currentSheet());
    }
    menu->addSeparator();
    menu->addAction(tr("New Sheet"), this, [this] {
        QString id;
        m_document->undoStack()->beginMacro(tr("Move to new sheet"));
        m_document->undoStack()->push(addSheet(m_document->model(), &id).release());
        moveSelectionToSheet(id);
        m_document->undoStack()->endMacro();
    });
}

void SchematicView::addShowOnSheetActions(QMenu *menu)
{
    const Schematic &schematic = m_document->model()->harness().schematic;
    for (int i = 0; i < int(schematic.sheets.size()); ++i) {
        const QString id = schematic.sheets[size_t(i)].id;
        QAction *action = menu->addAction(sheetDisplayName(schematic, i), this, [this, id] { showSelectionOnSheet(id); });
        action->setEnabled(id != m_scene->currentSheet());
    }
    menu->addSeparator();
    menu->addAction(tr("New Sheet"), this, [this] {
        QString id;
        m_document->undoStack()->beginMacro(tr("Show on new sheet"));
        m_document->undoStack()->push(addSheet(m_document->model(), &id).release());
        showSelectionOnSheet(id);
        m_document->undoStack()->endMacro();
    });
}

} // namespace tbh::app
