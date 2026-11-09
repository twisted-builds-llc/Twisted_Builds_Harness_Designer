// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SchematicItems.h"

#include "schematic/SchematicScene.h"
#include "ui/WireColors.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

namespace tbh::app {

namespace {

QPalette paletteOf(const QGraphicsItem *item)
{
    return item->scene() ? item->scene()->palette() : QApplication::palette();
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

void drawSelectionOutline(QPainter *painter, const QRectF &rect, const QPalette &palette)
{
    painter->setPen(QPen(palette.color(QPalette::Highlight), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rect.adjusted(-2, -2, 2, 2), 5, 5);
}

} // namespace

// ---- PlacedItem -------------------------------------------------------------

PlacedItem::PlacedItem(QString key)
    : m_key(std::move(key))
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
}

QVariant PlacedItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    auto *schematic = qobject_cast<SchematicScene *>(scene());
    if (change == ItemPositionChange && schematic && schematic->snapToGrid())
        return SchematicScene::snapped(value.toPointF());
    if (change == ItemPositionHasChanged && schematic)
        schematic->itemMoved(this);
    return QGraphicsItem::itemChange(change, value);
}

// ---- ConnectorItem ------------------------------------------------------------

ConnectorItem::ConnectorItem(const Connector &connector)
    : PlacedItem(connector.id)
    , m_connector(connector)
{
    setFlag(ItemUsesExtendedStyleOption);
    setZValue(1);
}

void ConnectorItem::setConnector(const Connector &connector)
{
    const bool geometry = connector.pins.size() != m_connector.pins.size();
    if (geometry)
        prepareGeometryChange();
    m_connector = connector;
    m_firstRow = std::clamp(m_firstRow, 0, std::max(0, rowCount() - visibleRowCount()));
    update();
}

void ConnectorItem::setDisplay(bool collapsed, int maxVisibleRows)
{
    if (collapsed == m_collapsed && maxVisibleRows == m_maxVisibleRows)
        return;
    prepareGeometryChange();
    m_collapsed = collapsed;
    m_maxVisibleRows = std::max(0, maxVisibleRows);
    m_firstRow = std::clamp(m_firstRow, 0, std::max(0, rowCount() - visibleRowCount()));
    update();
}

bool ConnectorItem::isScrollMode() const
{
    return !m_collapsed && m_maxVisibleRows > 0 && rowCount() > m_maxVisibleRows;
}

int ConnectorItem::visibleRowCount() const
{
    if (m_collapsed)
        return 0;
    return isScrollMode() ? m_maxVisibleRows : rowCount();
}

bool ConnectorItem::scrollRows(int rows)
{
    if (!isScrollMode())
        return false;
    const int first = std::clamp(m_firstRow + rows, 0, rowCount() - visibleRowCount());
    if (first == m_firstRow)
        return false;
    m_firstRow = first;
    update();
    return true;
}

void ConnectorItem::ensureRowVisible(int row)
{
    if (!isScrollMode())
        return;
    if (row < m_firstRow)
        scrollRows(row - m_firstRow);
    else if (row >= m_firstRow + visibleRowCount())
        scrollRows(row - (m_firstRow + visibleRowCount() - 1));
}

int ConnectorItem::rowOfPin(const QString &pinId) const
{
    for (int i = 0; i < rowCount(); ++i) {
        if (m_connector.pins[size_t(i)].id == pinId)
            return i;
    }
    return -1;
}

QRectF ConnectorItem::rowRect(int row) const
{
    const int first = isScrollMode() ? m_firstRow : 0;
    if (row < first || row >= first + visibleRowCount())
        return {};
    return QRectF(0, kHeaderHeight + (row - first) * kRowHeight, kWidth, kRowHeight);
}

QRectF ConnectorItem::labelCellRect(int row) const
{
    const QRectF r = rowRect(row);
    return r.isNull() ? r : QRectF(r.left(), r.top(), kLabelColumn, r.height());
}

QRectF ConnectorItem::circuitCellRect(int row) const
{
    const QRectF r = rowRect(row);
    return r.isNull() ? r : QRectF(r.left() + kLabelColumn, r.top(), r.width() - kLabelColumn, r.height());
}

QRectF ConnectorItem::collapseRect() const { return QRectF(4, 6, 18, 18); }
QRectF ConnectorItem::titleRect() const { return QRectF(24, 4, kWidth - 24 - 100, 22); }
QRectF ConnectorItem::minusRect() const { return QRectF(kWidth - 94, 12, 22, 22); }
QRectF ConnectorItem::countRect() const { return QRectF(kWidth - 70, 12, 40, 22); }
QRectF ConnectorItem::plusRect() const { return QRectF(kWidth - 28, 12, 22, 22); }

ConnectorItem::Hit ConnectorItem::hitTest(const QPointF &p) const
{
    if (p.y() < kHeaderHeight) {
        if (collapseRect().contains(p))
            return {Part::Collapse};
        if (minusRect().contains(p))
            return {Part::Minus};
        if (countRect().contains(p))
            return {Part::Count};
        if (plusRect().contains(p))
            return {Part::Plus};
        return {Part::Title};
    }
    const int first = isScrollMode() ? m_firstRow : 0;
    const int row = first + int((p.y() - kHeaderHeight) / kRowHeight);
    if (rowRect(row).isNull() || p.x() < -kStubLength || p.x() > kWidth + kStubLength)
        return {};
    if (p.x() < 0 || p.x() > kWidth)
        return {Part::PinStub, row};
    return {p.x() < kLabelColumn ? Part::PinLabel : Part::PinCircuit, row};
}

Anchor ConnectorItem::pinAnchor(int row, const QPointF &towards) const
{
    const QPointF towardsLocal = mapFromScene(towards);
    const bool right = towardsLocal.x() > kWidth / 2;
    qreal y;
    qreal x = right ? kWidth : 0;
    const QRectF r = rowRect(row);
    if (!r.isNull()) {
        y = r.center().y();
        x += right ? kStubLength : -kStubLength; // tip of the pin lead
    } else if (m_collapsed || visibleRowCount() == 0) {
        y = kHeaderHeight / 2;
    } else {
        // Scrolled out of view: attach at the top or bottom edge of the row area.
        y = row < m_firstRow ? kHeaderHeight + 2 : boundingRect().bottom() - 2;
    }
    return {mapToScene(QPointF(x, y)), right ? 1 : -1};
}

void ConnectorItem::setHoverRow(int row)
{
    if (row == m_hoverRow)
        return;
    m_hoverRow = row;
    update();
}

void ConnectorItem::setRelatedPins(bool active, const QSet<QString> &pinIds)
{
    if (active == m_relatedActive && pinIds == m_relatedPins)
        return;
    m_relatedActive = active;
    m_relatedPins = pinIds;
    update();
}

void ConnectorItem::setSearchText(const QString &text)
{
    if (text == m_search)
        return;
    m_search = text;
    update();
}

bool ConnectorItem::rowMatches(int row) const
{
    if (m_search.isEmpty())
        return false;
    const Pin &pin = m_connector.pins[size_t(row)];
    return pin.label.contains(m_search, Qt::CaseInsensitive)
           || pin.circuitText.contains(m_search, Qt::CaseInsensitive);
}

std::vector<int> ConnectorItem::searchMatches() const
{
    std::vector<int> rows;
    for (int i = 0; i < rowCount(); ++i) {
        if (rowMatches(i))
            rows.push_back(i);
    }
    return rows;
}

QRectF ConnectorItem::bodyRect() const
{
    return QRectF(0, 0, kWidth, kHeaderHeight + visibleRowCount() * kRowHeight);
}

QRectF ConnectorItem::boundingRect() const
{
    return bodyRect().adjusted(-kStubLength - 3, -3, kStubLength + 3, 3);
}

QPainterPath ConnectorItem::shape() const
{
    // The body plus the strips of pin leads on both sides (not the header corners).
    QPainterPath path;
    path.addRect(bodyRect().adjusted(-3, -3, 3, 3));
    const qreal rows = visibleRowCount() * kRowHeight;
    if (rows > 0) {
        path.addRect(QRectF(-kStubLength, kHeaderHeight, kStubLength, rows));
        path.addRect(QRectF(kWidth, kHeaderHeight, kStubLength, rows));
    }
    return path;
}

void ConnectorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor border = mix(base, text, 0.45);
    const QColor header = mix(pal.color(QPalette::Button), pal.color(QPalette::Highlight), 0.12);
    const QRectF body = bodyRect();
    const qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());

    painter->setPen(QPen(border, 1));
    painter->setBrush(base);
    painter->drawRoundedRect(body, 4, 4);

    // Header.
    QPainterPath headerPath;
    headerPath.addRoundedRect(QRectF(0, 0, kWidth, kHeaderHeight), 4, 4);
    painter->fillPath(headerPath, header);
    painter->setPen(QPen(border, 1));
    painter->drawLine(QPointF(0, kHeaderHeight), QPointF(kWidth, kHeaderHeight));

    if (lod >= 0.25) {
        QFont font = painter->font();
        font.setPixelSize(13);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(text);
        const QString title = m_connector.label.isEmpty() ? QStringLiteral("?") : m_connector.label;
        painter->drawText(titleRect(), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(title, Qt::ElideRight, int(titleRect().width())));
        font.setBold(false);
        font.setPixelSize(10);
        painter->setFont(font);
        painter->setPen(mix(text, base, 0.35));
        painter->drawText(QRectF(24, 26, kWidth - 124, 16), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(m_connector.partNumber, Qt::ElideRight,
                                                            int(kWidth - 124)));

        // Collapse arrow.
        painter->setPen(Qt::NoPen);
        painter->setBrush(text);
        const QRectF c = collapseRect().adjusted(5, 5, -5, -5);
        QPolygonF arrow;
        if (m_collapsed)
            arrow << c.topLeft() << QPointF(c.right(), c.center().y()) << c.bottomLeft();
        else
            arrow << c.topLeft() << c.topRight() << QPointF(c.center().x(), c.bottom());
        painter->drawPolygon(arrow);

        // [ - ]  N  [ + ]
        font.setPixelSize(12);
        painter->setFont(font);
        for (const QRectF &r : {minusRect(), plusRect()}) {
            painter->setPen(QPen(border, 1));
            painter->setBrush(pal.color(QPalette::Button));
            painter->drawRoundedRect(r, 3, 3);
        }
        painter->setPen(text);
        painter->drawText(minusRect(), Qt::AlignCenter, QStringLiteral("−"));
        painter->drawText(plusRect(), Qt::AlignCenter, QStringLiteral("+"));
        painter->setPen(QPen(border, 1));
        painter->setBrush(base);
        painter->drawRect(countRect());
        painter->setPen(text);
        painter->drawText(countRect(), Qt::AlignCenter, QString::number(rowCount()));
    }

    // Rows: only those inside the exposed rect.
    const int visible = visibleRowCount();
    if (visible > 0) {
        const int first = isScrollMode() ? m_firstRow : 0;
        const QRectF exposed = option->exposedRect;
        const int from = std::max(0, int((exposed.top() - kHeaderHeight) / kRowHeight));
        const int to = std::min(visible - 1, int((exposed.bottom() - kHeaderHeight) / kRowHeight));
        const QColor alternate = pal.color(QPalette::AlternateBase);
        const QColor match = mix(base, QColor(255, 200, 0), 0.45);
        const QColor hover = mix(base, pal.color(QPalette::Highlight), 0.3);

        if (lod < 0.2) {
            // Far zoom: rows are too small to read; draw a summary.
            painter->fillRect(QRectF(1, kHeaderHeight + 1, kWidth - 2, visible * kRowHeight - 2),
                              alternate);
        } else {
            QFont font = painter->font();
            font.setBold(false);
            font.setPixelSize(11);
            painter->setFont(font);
            const QFontMetrics metrics = painter->fontMetrics();
            const bool drawText = lod >= 0.35;
            for (int v = from; v <= to; ++v) {
                const int row = first + v;
                const QRectF r(0, kHeaderHeight + v * kRowHeight, kWidth, kRowHeight);
                QColor fill = (row % 2) ? alternate : base;
                if (rowMatches(row))
                    fill = match;
                if (row == m_hoverRow)
                    fill = hover;
                painter->fillRect(r.adjusted(1, 0, -1, 0), fill);
                // Pin leads: drag handles for drawing a wire.
                const qreal cy = r.center().y();
                painter->setPen(QPen(row == m_hoverRow ? pal.color(QPalette::Highlight) : border, 2,
                                     Qt::SolidLine, Qt::FlatCap));
                painter->drawLine(QPointF(-kStubLength, cy), QPointF(-1, cy));
                painter->drawLine(QPointF(kWidth + 1, cy), QPointF(kWidth + kStubLength, cy));
                if (!drawText)
                    continue;
                const Pin &pin = m_connector.pins[size_t(row)];
                const bool related = m_relatedActive && m_relatedPins.contains(pin.id);
                if (related) {
                    painter->fillRect(r.adjusted(1, 0, -1, 0), mix(fill, pal.color(QPalette::Highlight), 0.25));
                    painter->fillRect(QRectF(1, r.top(), 4, r.height()), pal.color(QPalette::Highlight));
                }
                const QColor rowText = (m_relatedActive && !related) ? mix(text, base, 0.6) : text;
                painter->setPen(rowText);
                painter->drawText(QRectF(6, r.top(), kLabelColumn - 10, kRowHeight),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  metrics.elidedText(pin.label, Qt::ElideRight, int(kLabelColumn - 10)));
                const QString circuit = pin.isCavityPlug && pin.circuitText.isEmpty()
                                            ? QApplication::translate("tbh::app::ConnectorItem", "(plug)")
                                            : pin.circuitText;
                painter->setPen(pin.isCavityPlug ? mix(rowText, base, 0.5) : rowText);
                painter->drawText(QRectF(kLabelColumn + 6, r.top(), kWidth - kLabelColumn - 12, kRowHeight),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  metrics.elidedText(circuit, Qt::ElideRight, int(kWidth - kLabelColumn - 12)));
            }
            painter->setPen(QPen(mix(base, text, 0.2), 1));
            painter->drawLine(QPointF(kLabelColumn, kHeaderHeight), QPointF(kLabelColumn, body.bottom()));
        }

        if (isScrollMode()) {
            // Scroll indicator on the right edge.
            const qreal trackTop = kHeaderHeight + 2;
            const qreal trackHeight = visible * kRowHeight - 4;
            const qreal thumbHeight = std::max<qreal>(12, trackHeight * visible / rowCount());
            const qreal thumbTop =
                trackTop + (trackHeight - thumbHeight) * first / std::max(1, rowCount() - visible);
            painter->setPen(Qt::NoPen);
            painter->setBrush(mix(base, text, 0.35));
            painter->drawRoundedRect(QRectF(kWidth - 6, thumbTop, 4, thumbHeight), 2, 2);
        }
    }

    if (isSelected())
        drawSelectionOutline(painter, body, pal);
}

// ---- SpliceItem ---------------------------------------------------------------

SpliceItem::SpliceItem(const Splice &splice)
    : PlacedItem(splice.id)
    , m_splice(splice)
{
    setZValue(2);
}

void SpliceItem::setSplice(const Splice &splice)
{
    m_splice = splice;
    update();
}

void SpliceItem::setHighlighted(bool on)
{
    if (on == m_highlighted)
        return;
    m_highlighted = on;
    update();
}

QRectF SpliceItem::boundingRect() const
{
    return QRectF(-60, -30, 120, 30 + kRadius + 4);
}

QRectF SpliceItem::stubRect(bool right)
{
    // Starts at the dot's edge; 10 px tall so it is easy to grab.
    return right ? QRectF(kRadius + 1, -5, kStubLength, 10) : QRectF(-kRadius - 1 - kStubLength, -5, kStubLength, 10);
}

bool SpliceItem::isStubAt(const QPointF &itemPos) const
{
    return (stubRect(false).contains(itemPos) || stubRect(true).contains(itemPos))
           && std::hypot(itemPos.x(), itemPos.y()) > kRadius + 1;
}

QPainterPath SpliceItem::shape() const
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
    path.addRect(QRectF(-40, -26, 80, 16));
    path.addRect(stubRect(false));
    path.addRect(stubRect(true));
    return path;
}

void SpliceItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    if (isSelected() || m_highlighted) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->drawEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
    }
    // Leads on both sides: drag handles for drawing a wire.
    painter->setPen(QPen(m_highlighted ? pal.color(QPalette::Highlight) : text, 2, Qt::SolidLine, Qt::FlatCap));
    for (bool right : {false, true}) {
        const QRectF r = stubRect(right);
        painter->drawLine(QPointF(r.left(), 0), QPointF(r.right(), 0));
    }
    painter->setPen(QPen(pal.color(QPalette::Base), 1.5));
    painter->setBrush(text);
    painter->drawEllipse(QPointF(0, 0), kRadius, kRadius);
    QFont font = painter->font();
    font.setPixelSize(11);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(text);
    painter->drawText(QRectF(-60, -28, 120, 18), Qt::AlignCenter, m_splice.label);
}

// ---- FreeEndItem --------------------------------------------------------------

FreeEndItem::FreeEndItem(const QString &wireId, bool endB, const FreeEnd &end)
    : PlacedItem(freeEndPlacementKey(wireId, endB))
    , m_wireId(wireId)
    , m_endB(endB)
    , m_end(end)
{
    setZValue(2);
}

void FreeEndItem::setEnd(const FreeEnd &end)
{
    m_end = end;
    update();
}

QRectF FreeEndItem::boundingRect() const
{
    return QRectF(-12, -12, 140, 24);
}

QPainterPath FreeEndItem::shape() const
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), 10, 10);
    return path;
}

void FreeEndItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    if (isSelected()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->drawEllipse(QPointF(0, 0), 10, 10);
    }
    painter->setPen(QPen(text, 2));
    painter->setBrush(Qt::NoBrush);
    switch (m_end.kind) {
    case FreeEndKind::FlyingLead:
        painter->drawLine(QPointF(-5, -5), QPointF(5, 5));
        painter->drawLine(QPointF(-5, 5), QPointF(5, -5));
        break;
    case FreeEndKind::RingTerminal:
        painter->drawEllipse(QPointF(0, 0), 7, 7);
        painter->drawEllipse(QPointF(0, 0), 2.5, 2.5);
        break;
    case FreeEndKind::Eyelet:
        painter->drawEllipse(QPointF(0, 0), 5, 5);
        break;
    case FreeEndKind::Bare:
        painter->drawLine(QPointF(0, -6), QPointF(0, 6));
        break;
    }
    if (!m_end.label.isEmpty()) {
        QFont font = painter->font();
        font.setPixelSize(11);
        painter->setFont(font);
        painter->setPen(text);
        painter->drawText(QRectF(12, -10, 116, 20), Qt::AlignLeft | Qt::AlignVCenter, m_end.label);
    }
}

// ---- WireItem -----------------------------------------------------------------

WireItem::WireItem(const Wire &wire)
    : m_wire(wire)
{
    setFlag(ItemIsSelectable);
    setZValue(0);
}

void WireItem::setWire(const Wire &wire)
{
    m_wire = wire;
    update();
}

QPainterPath WireItem::routePath(const Anchor &a, const Anchor &b)
{
    constexpr qreal stub = 16;
    const QPointF pa = a.point + QPointF(a.direction * stub, 0);
    const QPointF pb = b.point + QPointF(b.direction * stub, 0);
    const qreal midX = (pa.x() + pb.x()) / 2;
    QPainterPath path(a.point);
    path.lineTo(pa);
    path.lineTo(midX, pa.y());
    path.lineTo(midX, pb.y());
    path.lineTo(pb);
    path.lineTo(b.point);
    return path;
}

void WireItem::setRoute(const Anchor &a, const Anchor &b)
{
    prepareGeometryChange();
    m_path = routePath(a, b);
    QPainterPathStroker stroker;
    stroker.setWidth(10);
    m_shape = stroker.createStroke(m_path);
}

QRectF WireItem::boundingRect() const
{
    return m_path.boundingRect().adjusted(-6, -6, 6, 6);
}

QPainterPath WireItem::shape() const
{
    return m_shape;
}

void WireItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    painter->setBrush(Qt::NoBrush);
    if (isSelected()) {
        painter->setPen(QPen(pal.color(QPalette::Highlight), kPenWidth + 5, Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        painter->drawPath(m_path);
    }
    QColor base = wireColor(m_wire.baseColor);
    if (!base.isValid())
        base = mix(pal.color(QPalette::Text), pal.color(QPalette::Base), 0.4);
    // Thin outline keeps white/black wires visible on either theme.
    painter->setPen(QPen(mix(pal.color(QPalette::Text), pal.color(QPalette::Base), 0.5), kPenWidth + 1.5,
                         Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter->drawPath(m_path);
    painter->setPen(QPen(base, kPenWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter->drawPath(m_path);
    const QColor stripe = wireColor(m_wire.stripeColor);
    if (stripe.isValid()) {
        QPen pen(stripe, kPenWidth, Qt::CustomDashLine, Qt::FlatCap, Qt::MiterJoin);
        pen.setDashPattern({2.0, 2.0});
        painter->setPen(pen);
        painter->drawPath(m_path);
    }
}

} // namespace tbh::app
