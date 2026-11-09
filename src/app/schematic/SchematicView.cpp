// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SchematicView.h"

#include "Document.h"
#include "SelectionModel.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "io/HarnessJson.h"
#include "model/Fragment.h"
#include "schematic/SchematicScene.h"
#include "ui/ConnectorEdits.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <cmath>

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
    }
    return target;
}

void SchematicView::setHover(const DropTarget &target)
{
    if (m_hoverConnector && m_hoverConnector != target.connector)
        m_hoverConnector->setHoverRow(-1);
    if (m_hoverSplice && m_hoverSplice != target.splice)
        m_hoverSplice->setHighlighted(false);
    m_hoverConnector = target.connector;
    m_hoverSplice = target.splice;
    if (m_hoverConnector)
        m_hoverConnector->setHoverRow(target.row);
    if (m_hoverSplice)
        m_hoverSplice->setHighlighted(true);
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
            if (!(event->modifiers() & Qt::ControlModifier) && !connector->isSelected()) {
                m_scene->clearSelection();
                connector->setSelected(true);
            }
            beginWire(PinRef{connector->id(), connector->connector().pins[size_t(hit.row)].id}, event->pos());
            return;
        default:
            break;
        }
    } else if (item && item->type() == SpliceItemType && m_tool == Tool::Wire) {
        beginWire(SpliceRef{static_cast<SpliceItem *>(item)->id()}, event->pos());
        return;
    }

    CanvasView::mousePressEvent(event);
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
        if (target.connector)
            end = target.connector->pinAnchor(target.row, scenePos);
        else if (target.splice)
            end = target.splice->anchor();
        if (wire)
            m_preview->setPath(WireItem::routePath(m_scene->endAnchor(*wire, !m_reattachEndB), end));
        return;
    }
    CanvasView::mouseMoveEvent(event);
    if ((event->buttons() & Qt::LeftButton) && !draggedFreeEnd().isEmpty())
        setHover(targetAt(mapToScene(event->pos())));
}

void SchematicView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        CanvasView::mouseReleaseEvent(event);
        viewport()->setCursor(m_tool == Tool::Wire ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
    if (m_wirePending && event->button() == Qt::LeftButton) {
        if (m_wireDrawing)
            finishWire(mapToScene(event->pos()));
        else
            cancelWire(); // a plain click on a pin row only selects the connector
        return;
    }
    if (!m_reattachWire.isEmpty() && event->button() == Qt::LeftButton) {
        if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            cancelWire(); // a click on the end, not a drag
        else
            finishReattach(mapToScene(event->pos()));
        return;
    }
    CanvasView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton) {
        setHover({});
        if (!dropFreeEnd(mapToScene(event->pos())))
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
    case SpliceItemType:
    case WireItemType:
    case FreeEndItemType:
        m_scene->clearSelection();
        item->setSelected(true);
        renameSelection();
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
    m_preview->setPath(WireItem::routePath(startAnchor(end.point), end));
}

void SchematicView::finishWire(const QPointF &scenePos)
{
    const DropTarget target = targetAt(scenePos);
    const WireEnd start = m_wireStart;
    cancelWire();
    if (target.end && *target.end == start)
        return; // dropped back on the start

    HarnessModel *model = m_document->model();
    const WireStyle &style = m_document->wireStyle();
    Wire wire;
    wire.id = newId();
    wire.endA = start;
    wire.baseColor = style.baseColor;
    wire.stripeColor = style.stripeColor;
    wire.gauge = style.gauge;
    wire.wireTypeId = style.wireTypeId;
    Placements placements;
    if (target.end) {
        wire.endB = *target.end;
    } else {
        wire.endB = FreeEnd{FreeEndKind::FlyingLead, {}, {}};
        SchematicPlacement placement;
        placement.position = SchematicScene::snapped(scenePos);
        placements[freeEndPlacementKey(wire.id, true)] = placement;
    }
    // Name the wire after the circuit text of the pins it connects.
    for (const WireEnd *end : {&wire.endA, &wire.endB}) {
        if (const auto *pin = std::get_if<PinRef>(end); pin && wire.name.isEmpty()) {
            if (const Connector *c = model->find<Connector>(pin->connectorId)) {
                for (const Pin &p : c->pins) {
                    if (p.id == pin->pinId)
                        wire.name = p.circuitText;
                }
            }
        }
    }
    const QString id = wire.id;
    m_document->undoStack()->push(
        new AddEntityCommand<Wire>(model, std::move(wire), std::move(placements), tr("Add wire")));
    Selection selection;
    selection.wires.insert(id);
    m_selection->setSelection(selection);
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
    if (s.wires.size() != 1 || !s.connectors.isEmpty() || !s.splices.isEmpty())
        return std::nullopt;
    const Wire *wire = m_document->model()->find<Wire>(*s.wires.begin());
    if (!wire)
        return std::nullopt;
    constexpr int kGrabRadius = 8; // pixels
    for (bool endB : {false, true}) {
        // Free ends are dragged as items (see dropFreeEnd).
        if (std::holds_alternative<FreeEnd>(endB ? wire->endB : wire->endA))
            continue;
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
    const WireEnd newEnd = target.end ? *target.end : WireEnd(FreeEnd{FreeEndKind::FlyingLead, {}, {}});
    auto command = setWireEnd(m_document->model(), wireId, endB, newEnd, SchematicScene::snapped(scenePos));
    if (command)
        m_document->undoStack()->push(command.release());
    Selection selection;
    selection.wires.insert(wireId);
    m_selection->setSelection(selection);
}

QString SchematicView::draggedFreeEnd() const
{
    if (m_pressPositions.size() != 1)
        return {};
    PlacedItem *item = m_scene->placedItem(m_pressPositions.begin()->first);
    return item && item->type() == FreeEndItemType ? item->placementKey() : QString();
}

bool SchematicView::dropFreeEnd(const QPointF &scenePos)
{
    const QString key = draggedFreeEnd();
    if (key.isEmpty())
        return false;
    const DropTarget target = targetAt(scenePos);
    if (!target.end)
        return false;
    auto *item = static_cast<FreeEndItem *>(m_scene->placedItem(key));
    const QString wireId = item->wireId();
    const bool endB = item->isEndB();
    const QPointF before = m_pressPositions.begin()->second;
    m_pressPositions.clear();
    auto command = setWireEnd(m_document->model(), wireId, endB, *target.end, {});
    if (!command) {
        item->setPos(before); // e.g. dropped on the wire's own other end
        return true;
    }
    m_document->undoStack()->push(command.release());
    return true;
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

void SchematicView::rememberPressPositions()
{
    m_pressPositions.clear();
    for (PlacedItem *item : selectedPlacedItems())
        m_pressPositions[item->placementKey()] = item->pos();
}

void SchematicView::commitMoves()
{
    std::map<QString, std::optional<SchematicPlacement>> changes;
    for (const auto &[key, before] : m_pressPositions) {
        PlacedItem *item = m_scene->placedItem(key);
        if (!item || item->pos() == before)
            continue;
        const SchematicPlacement *old = m_document->model()->placement(key);
        SchematicPlacement placement = old ? *old : SchematicPlacement{};
        placement.position = item->pos();
        changes[key] = placement;
    }
    m_pressPositions.clear();
    if (!changes.empty())
        m_document->undoStack()->push(new SetPlacementsCommand(m_document->model(), changes, tr("Move")));
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
    const SchematicPlacement *old = m_document->model()->placement(connectorId);
    SchematicPlacement placement = old ? *old : SchematicPlacement{};
    placement.position = item->pos();
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
                         m_document->undoStack()->push(new UpdateEntityCommand<Connector>(
                             model, updated, column == 0 ? tr("Edit pin label") : tr("Edit circuit text")));
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
        auto *freeEnd = static_cast<FreeEndItem *>(item);
        const QString wireId = freeEnd->wireId();
        const bool endB = freeEnd->isEndB();
        const Wire &wire = *model->find<Wire>(wireId);
        editText(item->mapRectToScene(QRectF(12, -10, 140, 20)),
                 std::get<FreeEnd>(endB ? wire.endB : wire.endA).label,
                 [this, wireId, endB](const QString &value, Nav) {
                     HarnessModel *m = m_document->model();
                     Wire w = *m->find<Wire>(wireId);
                     FreeEnd &end = std::get<FreeEnd>(endB ? w.endB : w.endA);
                     if (end.label == value)
                         return;
                     end.label = value;
                     m_document->undoStack()->push(new UpdateEntityCommand<Wire>(m, w, tr("Edit wire end")));
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

void SchematicView::addConnector()
{
    Connector connector;
    connector.id = newId();
    connector.label = uniqueLabel(QStringLiteral("J"), false);
    connector.pins = resizedPins({}, 1);
    SchematicPlacement placement;
    placement.position = SchematicScene::snapped(viewCenterSnapped() - QPointF(ConnectorItem::kWidth / 2, 40));
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
    placement.position = viewCenterSnapped();
    const QString id = splice.id;
    m_document->undoStack()->push(new AddEntityCommand<Splice>(
        m_document->model(), std::move(splice), {{id, placement}}, tr("Add splice")));
    Selection selection;
    selection.splices.insert(id);
    m_selection->setSelection(selection);
}

void SchematicView::deleteSelection()
{
    deleteItems(this, m_document, m_selection->selection());
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
    if (s.connectors.isEmpty() && s.splices.isEmpty())
        return {};
    Harness fragment = extractFragment(m_document->model()->harness(), s.connectors, s.splices);
    return io::writeZip(io::serializeHarness(fragment), nullptr).value_or(QByteArray());
}

void SchematicView::copySelection()
{
    const QByteArray bytes = selectionFragment();
    if (bytes.isEmpty()) {
        emit statusMessage(tr("Select connectors or splices to copy."));
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
        pasteFragment(m_document->model(), std::move(*fragment), offset, &newIds).release());
    Selection selection;
    for (const QString &id : newIds) {
        if (m_document->model()->find<Connector>(id))
            selection.connectors.insert(id);
        else
            selection.splices.insert(id);
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
            return QRectF(item->pos(), item->boundingRect().adjusted(3, 3, -3, -3).size());
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
        const SchematicPlacement *old = m_document->model()->placement(item->placementKey());
        SchematicPlacement placement = old ? *old : SchematicPlacement{};
        placement.position = item->pos() + delta;
        changes[item->placementKey()] = placement;
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
        const SchematicPlacement *old = m_document->model()->placement(items[i]->placementKey());
        SchematicPlacement placement = old ? *old : SchematicPlacement{};
        placement.position = items[i]->pos()
                             + (orientation == Qt::Horizontal ? QPointF(delta, 0) : QPointF(0, delta));
        changes[items[i]->placementKey()] = placement;
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

} // namespace tbh::app
