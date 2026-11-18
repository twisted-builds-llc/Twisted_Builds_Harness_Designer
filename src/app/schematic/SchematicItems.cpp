// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SchematicItems.h"

#include "model/Queries.h"
#include "schematic/SchematicScene.h"
#include "ui/CanvasView.h"
#include "ui/Twist.h"
#include "ui/WireColors.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QCoreApplication>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

namespace tbh::app {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::app::SchematicItems)
};
} // namespace


namespace {

// Height of a schematic symbol hanging below a splice or free end (see paintSymbol).
constexpr qreal kSymbolHeight = 24;

// Hit area of the symbol hanging from `top`, so clicking it grabs the item.
QRectF symbolRect(qreal top)
{
    return QRectF(-11, top, 22, kSymbolHeight);
}

// A ground, chassis ground or power symbol hanging below the item from `top`: a short stem,
// then the symbol. Ground: three bars getting shorter. Chassis ground: a bar with three
// slanted ticks. Power: a circled plus, like a battery's positive post.
void paintSymbol(QPainter *painter, SchematicSymbol symbol, qreal top, const QColor &color)
{
    if (symbol == SchematicSymbol::None)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(color, 2, Qt::SolidLine, Qt::FlatCap));
    const qreal stemEnd = top + 9;
    painter->drawLine(QPointF(0, top), QPointF(0, stemEnd));
    switch (symbol) {
    case SchematicSymbol::Ground:
        for (int i = 0; i < 3; ++i) {
            const qreal half = 10 - 3.5 * i;
            const qreal y = stemEnd + 4 * i;
            painter->drawLine(QPointF(-half, y), QPointF(half, y));
        }
        break;
    case SchematicSymbol::ChassisGround:
        painter->drawLine(QPointF(-10, stemEnd), QPointF(10, stemEnd));
        for (qreal x : {-8.0, 0.0, 8.0})
            painter->drawLine(QPointF(x, stemEnd), QPointF(x - 5, stemEnd + 7));
        break;
    case SchematicSymbol::Power: {
        const QPointF center(0, stemEnd + 6.5);
        painter->setPen(QPen(color, 1.8));
        painter->drawEllipse(center, 6.5, 6.5);
        painter->drawLine(center - QPointF(3.5, 0), center + QPointF(3.5, 0));
        painter->drawLine(center - QPointF(0, 3.5), center + QPointF(0, 3.5));
        break;
    }
    case SchematicSymbol::None:
        break;
    }
    painter->restore();
}

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

    // A factory connector (already there, not built) has a dashed outline.
    painter->setPen(m_connector.factory ? QPen(text, 1.5, Qt::DashLine) : QPen(border, 1));
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
        QString subtitle = m_connector.partNumber;
        if (m_connector.factory)
            subtitle = subtitle.isEmpty() ? Tr::tr("Factory") : Tr::tr("Factory - %1").arg(subtitle);
        painter->drawText(QRectF(24, 26, kWidth - 124, 16), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(subtitle, Qt::ElideRight, int(kWidth - 124)));

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
    return QRectF(-60, -30, 120, 30 + kRadius + 4).united(symbolRect(kRadius + 1));
}

QRectF SpliceItem::stubRect(bool right)
{
    // Starts at the dot's edge; 10 px tall so it is easy to grab.
    return right ? QRectF(kRadius + 1, -5, kStubLength, 10) : QRectF(-kRadius - 1 - kStubLength, -5, kStubLength, 10);
}

Anchor SpliceItem::anchor(WireLead lead) const
{
    if (lead == WireLead::Auto)
        return {scenePos(), 0};
    const bool right = lead == WireLead::Right;
    const qreal x = kRadius + 1 + kStubLength;
    return {mapToScene(QPointF(right ? x : -x, 0)), right ? 1 : -1};
}

bool SpliceItem::isStubAt(const QPointF &itemPos) const
{
    return (stubRect(false).contains(itemPos) || stubRect(true).contains(itemPos))
           && std::hypot(itemPos.x(), itemPos.y()) > kRadius + 1;
}

WireLead SpliceItem::leadAt(const QPointF &itemPos) const
{
    if (!isStubAt(itemPos))
        return WireLead::Auto;
    return itemPos.x() > 0 ? WireLead::Right : WireLead::Left;
}

QPainterPath SpliceItem::shape() const
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
    path.addRect(QRectF(-40, -26, 80, 16));
    path.addRect(stubRect(false));
    path.addRect(stubRect(true));
    if (m_splice.symbol != SchematicSymbol::None)
        path.addRect(symbolRect(kRadius + 1));
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
    paintSymbol(painter, m_splice.symbol, kRadius, text);
    QFont font = painter->font();
    font.setPixelSize(11);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(text);
    painter->drawText(QRectF(-60, -28, 120, 18), Qt::AlignCenter, m_splice.label);
}

// ---- FreeEndItem --------------------------------------------------------------

FreeEndItem::FreeEndItem(const FreeEnd &end)
    : PlacedItem(end.id)
    , m_end(end)
{
    setZValue(2);
}

void FreeEndItem::setFreeEnd(const FreeEnd &end)
{
    m_end = end;
    update();
}

void FreeEndItem::setHighlighted(bool on)
{
    if (on == m_highlighted)
        return;
    m_highlighted = on;
    update();
}

Anchor FreeEndItem::anchor(const QPointF &towards, WireLead lead) const
{
    const bool right = lead == WireLead::Auto ? mapFromScene(towards).x() >= 0 : lead == WireLead::Right;
    const qreal x = kHalfWidth + kStubLength;
    return {mapToScene(QPointF(right ? x : -x, 0)), right ? 1 : -1};
}

QRectF FreeEndItem::stubRect(bool right)
{
    return right ? QRectF(kHalfWidth, -5, kStubLength, 10) : QRectF(-kHalfWidth - kStubLength, -5, kStubLength, 10);
}

bool FreeEndItem::isStubAt(const QPointF &itemPos) const
{
    return stubRect(false).contains(itemPos) || stubRect(true).contains(itemPos);
}

WireLead FreeEndItem::leadAt(const QPointF &itemPos) const
{
    if (!isStubAt(itemPos))
        return WireLead::Auto;
    return itemPos.x() > 0 ? WireLead::Right : WireLead::Left;
}

QRectF FreeEndItem::boundingRect() const
{
    return QRectF(-60, -30, 120, 30 + kHalfWidth + 4).united(symbolRect(kHalfWidth));
}

QPainterPath FreeEndItem::shape() const
{
    QPainterPath path;
    path.addRect(QRectF(-kHalfWidth - 2, -kHalfWidth - 2, 2 * kHalfWidth + 4, 2 * kHalfWidth + 4));
    path.addRect(QRectF(-40, -28, 80, 16)); // label
    path.addRect(stubRect(false));
    path.addRect(stubRect(true));
    if (m_end.symbol != SchematicSymbol::None)
        path.addRect(symbolRect(kHalfWidth));
    return path;
}

void FreeEndItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    const QColor copper(0xc8, 0x75, 0x33);
    painter->setRenderHint(QPainter::Antialiasing);
    if (isSelected() || m_highlighted) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->drawRoundedRect(QRectF(-kHalfWidth - 3, -kHalfWidth - 3, 2 * kHalfWidth + 6, 2 * kHalfWidth + 6), 4, 4);
    }
    // Leads on both sides: drag handles for drawing a wire.
    painter->setPen(QPen(m_highlighted ? pal.color(QPalette::Highlight) : text, 2, Qt::SolidLine, Qt::FlatCap));
    for (bool right : {false, true}) {
        const QRectF r = stubRect(right);
        painter->drawLine(QPointF(r.left(), 0), QPointF(r.right(), 0));
    }

    // The symbol, about 2 * kHalfWidth wide: insulation on the left, then what ends the wire.
    const QRectF barrel(-kHalfWidth, -3.5, 9, 7);
    painter->setPen(Qt::NoPen);
    painter->setBrush(text);
    switch (m_end.kind) {
    case FreeEndKind::FlyingLead:
        // Insulated wire with its strands fanned out.
        painter->drawRoundedRect(QRectF(-kHalfWidth, -4, 14, 8), 2, 2);
        painter->setPen(QPen(copper, 1.5, Qt::SolidLine, Qt::RoundCap));
        for (qreal y : {-5.0, 0.0, 5.0})
            painter->drawLine(QPointF(2, y * 0.4), QPointF(kHalfWidth, y));
        break;
    case FreeEndKind::Bare:
        // Insulation stops; one stripped conductor.
        painter->drawRoundedRect(QRectF(-kHalfWidth, -4, 12, 8), 2, 2);
        painter->setPen(QPen(copper, 3, Qt::SolidLine, Qt::FlatCap));
        painter->drawLine(QPointF(0, 0), QPointF(kHalfWidth, 0));
        break;
    case FreeEndKind::RingTerminal:
        // Crimp barrel, then the ring.
        painter->drawRect(barrel);
        painter->setPen(QPen(text, 2.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(kHalfWidth - 6.5, 0), 5.5, 5.5);
        break;
    case FreeEndKind::Eyelet:
        // A washer-like ring with its hole.
        painter->setPen(QPen(text, 3.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(0, 0), 7.5, 7.5);
        break;
    case FreeEndKind::Cut:
        // The wire broken by a gap with two slashes: --//--
        painter->setPen(QPen(text, 2, Qt::SolidLine, Qt::FlatCap));
        painter->drawLine(QPointF(-kHalfWidth, 0), QPointF(-5, 0));
        painter->drawLine(QPointF(5, 0), QPointF(kHalfWidth, 0));
        painter->setPen(QPen(text, 2, Qt::SolidLine, Qt::RoundCap));
        for (qreal x : {-3.5, 3.5})
            painter->drawLine(QPointF(x - 3, 7), QPointF(x + 3, -7));
        break;
    }
    // The eyelet ring reaches 9.25 below the center; the stem starts just under the symbol.
    paintSymbol(painter, m_end.symbol, kHalfWidth - 2, text);

    QFont font = painter->font();
    font.setPixelSize(11);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(text);
    painter->drawText(QRectF(-60, -30, 120, 16), Qt::AlignCenter,
                      m_end.label.isEmpty() ? freeEndKindName(m_end.kind) : m_end.label);
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

namespace {

constexpr qreal kRouteStub = 16; // straight run out of each anchor before the route turns

QPointF stubEnd(const Anchor &anchor)
{
    return anchor.point + QPointF(anchor.direction * kRouteStub, 0);
}

} // namespace

QPainterPath WireItem::roundedPath(const std::vector<QPointF> &points, qreal radius)
{
    // Drop repeated points: a zero-length leg has no direction to round from.
    std::vector<QPointF> p;
    for (const QPointF &point : points) {
        if (p.empty() || QLineF(p.back(), point).length() > 1e-6)
            p.push_back(point);
    }
    QPainterPath path(p.empty() ? QPointF() : p.front());
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        const QLineF in(p[i - 1], p[i]);
        const QLineF out(p[i], p[i + 1]);
        const QPointF dirIn = (p[i] - p[i - 1]) / in.length();
        const QPointF dirOut = (p[i + 1] - p[i]) / out.length();
        const qreal cross = dirIn.x() * dirOut.y() - dirIn.y() * dirOut.x();
        if (std::abs(cross) < 1e-6) { // straight on: no corner
            path.lineTo(p[i]);
            continue;
        }
        // Never use more than half of either leg, so neighboring corners do not overlap.
        const qreal r = std::min({radius, in.length() / 2, out.length() / 2});
        if (QLineF(path.currentPosition(), p[i] - dirIn * r).length() > 1e-6)
            path.lineTo(p[i] - dirIn * r);
        path.quadTo(p[i], p[i] + dirOut * r);
    }
    if (p.size() > 1)
        path.lineTo(p.back());
    return path;
}

QPainterPath WireItem::routePath(const Anchor &a, const Anchor &b, std::optional<qreal> runX)
{
    const QPointF pa = stubEnd(a);
    const QPointF pb = stubEnd(b);
    const qreal midX = runX.value_or((pa.x() + pb.x()) / 2);
    return roundedPath({a.point, pa, QPointF(midX, pa.y()), QPointF(midX, pb.y()), pb, b.point}, kCornerRadius);
}

void WireItem::setRoute(const Anchor &a, const Anchor &b, std::optional<qreal> runX)
{
    m_a = a;
    m_b = b;
    m_runX = runX;
    m_tag.clear();
    m_tagRect = {};
    prepareGeometryChange();
    m_path = routePath(a, b, runX);
    updateShape();
}

namespace {

constexpr qreal kOffSheetLead = 48; // lead from the anchor to the tag
constexpr qreal kTagHeight = 18;
constexpr qreal kTagPoint = 8;      // depth of the arrow point at the tag's far end

// Pixel size, like the other items' text: scene units, whatever the paint device.
QFont tagFont()
{
    QFont font;
    font.setPixelSize(11);
    return font;
}

} // namespace

void WireItem::setOffSheet(const Anchor &a, const QString &tag)
{
    m_a = a;
    m_b = {};
    m_runX.reset();
    m_tag = tag;
    const int direction = a.direction == 0 ? 1 : a.direction;
    const QPointF leadEnd = a.point + QPointF(direction * kOffSheetLead, 0);
    const qreal width = QFontMetricsF(tagFont()).horizontalAdvance(tag) + 12 + kTagPoint;
    m_tagRect = direction > 0 ? QRectF(leadEnd.x(), leadEnd.y() - kTagHeight / 2, width, kTagHeight)
                              : QRectF(leadEnd.x() - width, leadEnd.y() - kTagHeight / 2, width, kTagHeight);
    prepareGeometryChange();
    m_path = QPainterPath(a.point);
    m_path.lineTo(leadEnd);
    updateShape();
}

void WireItem::setShield(bool shield)
{
    if (m_shield == shield)
        return;
    m_shield = shield;
    update();
}

QPainterPath WireItem::leadsClip() const
{
    // Scene rects (this item is never moved) along the lead out of each end.
    constexpr qreal half = 8;
    QPainterPath clip;
    for (const Anchor &end : {m_a, m_b}) {
        if (end.direction == 0)
            continue;
        const qreal x2 = end.point.x() + end.direction * m_leadsOnly;
        clip.addRect(QRectF(QPointF(std::min(end.point.x(), x2), end.point.y() - half),
                            QPointF(std::max(end.point.x(), x2), end.point.y() + half)));
    }
    return clip;
}

void WireItem::setLeadsOnly(qreal length)
{
    if (length == m_leadsOnly)
        return;
    prepareGeometryChange();
    m_leadsOnly = length;
    updateShape();
}

void WireItem::updateShape()
{
    QPainterPathStroker stroker;
    stroker.setWidth(10);
    m_shape = stroker.createStroke(m_path);
    if (m_leadsOnly > 0)
        m_shape = m_shape.intersected(leadsClip());
    if (!m_tagRect.isNull())
        m_shape.addRect(m_tagRect);
}

void WireItem::setRunX(qreal x)
{
    if (!isOffSheet())
        setRoute(m_a, m_b, x);
}

qreal WireItem::runX() const
{
    return m_runX.value_or((stubEnd(m_a).x() + stubEnd(m_b).x()) / 2);
}

std::optional<qreal> WireItem::runDistance(const QPointF &scenePos) const
{
    const qreal top = std::min(m_a.point.y(), m_b.point.y());
    const qreal bottom = std::max(m_a.point.y(), m_b.point.y());
    if (isOffSheet() || m_leadsOnly > 0 || bottom - top < 1e-6 || scenePos.y() < top || scenePos.y() > bottom)
        return std::nullopt;
    return std::abs(scenePos.x() - runX());
}

QVariant WireItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged)
        setZValue(value.toBool() ? kSelectedZ : 0);
    return QGraphicsItem::itemChange(change, value);
}

QRectF WireItem::boundingRect() const
{
    return m_path.boundingRect().united(m_tagRect).adjusted(-6, -6, 6, 6);
}

QPainterPath WireItem::shape() const
{
    return m_shape;
}

void WireItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->save();
    if (m_leadsOnly > 0) // a cable draws the rest
        painter->setClipPath(leadsClip(), Qt::IntersectClip);
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
    // A factory wire (already there, not built) is drawn dashed and faded.
    painter->save();
    if (m_wire.factory)
        painter->setOpacity(0.6);
    auto wirePen = [&](const QColor &color, qreal width) {
        QPen pen(color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
        // Same dashes at every width: factory wires dashed, shields dotted.
        if (m_wire.factory)
            pen.setDashPattern({4 * kPenWidth / width, 2 * kPenWidth / width});
        else if (m_shield)
            pen.setDashPattern({1 * kPenWidth / width, 1 * kPenWidth / width});
        return pen;
    };
    // Thin outline keeps white/black wires visible on either theme.
    painter->setPen(wirePen(mix(pal.color(QPalette::Text), pal.color(QPalette::Base), 0.5), kPenWidth + 1.5));
    painter->drawPath(m_path);
    painter->setPen(wirePen(base, kPenWidth));
    painter->drawPath(m_path);
    const QColor stripe = wireColor(m_wire.stripeColor);
    if (stripe.isValid()) {
        // A tracer: one thin continuous stripe along the middle of the wire, as on real wire.
        painter->setPen(wirePen(stripe, kPenWidth * 0.4));
        painter->drawPath(m_path);
    }
    painter->restore();
    if (isSelected()) {
        // Grab handles on the ends: dragging one moves that end to another pin or lead.
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(pal.color(QPalette::Highlight), 1.5));
        painter->setBrush(pal.color(QPalette::Base));
        painter->drawEllipse(m_a.point, kEndHandleRadius, kEndHandleRadius);
        if (!isOffSheet())
            painter->drawEllipse(m_b.point, kEndHandleRadius, kEndHandleRadius);
    }
    if (isOffSheet()) {
        // A flag pointing away from the wire: the wire continues on another sheet.
        const QRectF r = m_tagRect;
        const bool right = r.left() >= m_a.point.x();
        QPolygonF flag;
        if (right)
            flag << r.topLeft() << QPointF(r.right() - kTagPoint, r.top()) << QPointF(r.right(), r.center().y())
                 << QPointF(r.right() - kTagPoint, r.bottom()) << r.bottomLeft();
        else
            flag << r.topRight() << QPointF(r.left() + kTagPoint, r.top()) << QPointF(r.left(), r.center().y())
                 << QPointF(r.left() + kTagPoint, r.bottom()) << r.bottomRight();
        painter->setPen(QPen(isSelected() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Text), 1.2));
        painter->setBrush(pal.color(QPalette::Base));
        painter->drawPolygon(flag);
        painter->setFont(tagFont());
        painter->setPen(pal.color(QPalette::Text));
        const QRectF text = right ? r.adjusted(6, 0, -kTagPoint, 0) : r.adjusted(kTagPoint, 0, -6, 0);
        painter->drawText(text, Qt::AlignVCenter | Qt::AlignHCenter, m_tag);
    }
    painter->restore();
}

// ---- CableItem ----------------------------------------------------------------

namespace {

constexpr qreal kShieldMargin = 7; // shield outline beyond the outer strands
constexpr qreal kLabelHeight = 14;
constexpr qreal kLabelInset = 36;  // label along the bundle from where the leads gather in
constexpr qreal kTagOffset = 30;   // tag along the lead from its end
// Length of one full twist as drawn: a loose twist, looser the more conductors there are
// so each strand stays easy to follow.
qreal twistPitch(size_t strands)
{
    return 36 + 10 * qreal(strands);
}
constexpr qreal kMinGather = 18;   // leads gather into the bundle over at least this length
constexpr qreal kTwistStep = 1.5;  // sampling along the bundle

QFont cableFont()
{
    QFont font;
    font.setPixelSize(10);
    font.setBold(true);
    return font;
}

// Smooth 0..1 step.
qreal smoothStep(qreal t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Leads spread `spread` apart gather into the bundle over this length: further apart, longer.
qreal gatherLength(qreal spread)
{
    return std::max(kMinGather, 0.5 * spread);
}

} // namespace

CableItem::CableItem(const Cable &cable)
    : m_cable(cable)
{
    setFlag(ItemIsSelectable);
    setZValue(1); // over the wires
}

void CableItem::setCable(const Cable &cable)
{
    m_cable = cable;
    prepareGeometryChange();
    updateShape();
}

QString CableItem::text() const
{
    if (!m_cable.label.isEmpty())
        return m_cable.label;
    // No label: say what it is.
    if (m_cable.twisted && m_cable.shielded())
        return QCoreApplication::translate("tbh::app::CableItem", "TW/SH");
    if (m_cable.twisted)
        return QCoreApplication::translate("tbh::app::CableItem", "TW");
    if (m_cable.shielded())
        return QCoreApplication::translate("tbh::app::CableItem", "SH");
    return QCoreApplication::translate("tbh::app::CableItem", "CBL");
}

void CableItem::follow(const std::vector<const WireItem *> &conductors, const std::vector<const WireItem *> &others)
{
    std::vector<const WireItem *> shown;
    for (const WireItem *w : conductors) {
        if (w && w->isVisible() && !w->isOffSheet())
            shown.push_back(w);
    }
    m_bundle.reset();
    m_tags.clear();
    // A bundle when every conductor runs between the same two item sides: end A is the end
    // nearest the first conductor's end A.
    std::vector<std::pair<Anchor, Anchor>> ends; // per shown conductor: end A, end B
    bool together = shown.size() >= 2;
    if (together) {
        const Anchor refA = shown.front()->anchorA();
        const Anchor refB = shown.front()->anchorB();
        auto sameSide = [](const Anchor &end, const Anchor &ref) {
            return end.direction != 0 && end.direction == ref.direction && std::abs(end.point.x() - ref.point.x()) < 0.5;
        };
        for (const WireItem *w : shown) {
            const Anchor &a = w->anchorA();
            const Anchor &b = w->anchorB();
            const bool aFirst = QLineF(a.point, refA.point).length() <= QLineF(b.point, refA.point).length();
            const Anchor &near = aFirst ? a : b;
            const Anchor &far = aFirst ? b : a;
            if (!sameSide(near, refA) || !sameSide(far, refB)) {
                together = false;
                break;
            }
            ends.emplace_back(near, far);
        }
    }
    if (!together) {
        for (const WireItem *w : shown) {
            for (const Anchor &end : {w->anchorA(), w->anchorB()}) {
                const int direction = end.direction == 0 ? 1 : end.direction;
                m_tags.push_back(end.point + QPointF(direction * kTagOffset, 0));
            }
        }
        prepareGeometryChange();
        updateShape();
        return;
    }

    Bundle bundle;
    const Anchor &a0 = ends.front().first;
    const Anchor &b0 = ends.front().second;
    qreal minA = a0.point.y(), maxA = minA, minB = b0.point.y(), maxB = minB, runSum = 0;
    for (size_t i = 0; i < shown.size(); ++i) {
        const Wire &wire = shown[i]->wire();
        const qreal yA = ends[i].first.point.y();
        const qreal yB = ends[i].second.point.y();
        bundle.lanes.push_back({wire.id, yA, yB, wire.baseColor, wire.stripeColor, wire.factory});
        minA = std::min(minA, yA);
        maxA = std::max(maxA, yA);
        minB = std::min(minB, yB);
        maxB = std::max(maxB, yB);
        runSum += shown[i]->runX();
    }
    const size_t n = bundle.lanes.size();
    bundle.radius = 2 + 2.2 * qreal(n); // loose: room to see each strand
    bundle.gatherA = gatherLength(maxA - minA);
    bundle.gatherB = gatherLength(maxB - minB);

    // Lines drawn by other wires, which the bundle should not lie on: horizontal legs
    // (y, x range) and vertical runs (x, y range).
    struct Line
    {
        qreal at, from, to;
    };
    std::vector<Line> legs, runs;
    for (const WireItem *w : others) {
        if (!w || !w->isVisible() || w->isOffSheet() || w->leadsOnly() > 0)
            continue;
        const QPointF a = w->anchorA().point;
        const QPointF b = w->anchorB().point;
        const qreal x = w->runX();
        legs.push_back({a.y(), std::min(a.x(), x), std::max(a.x(), x)});
        legs.push_back({b.y(), std::min(b.x(), x), std::max(b.x(), x)});
        runs.push_back({x, std::min(a.y(), b.y()), std::max(a.y(), b.y())});
    }
    const qreal clearance = bundle.radius + WireItem::kPenWidth + 2;
    auto blocked = [clearance](const std::vector<Line> &lines, qreal at, qreal from, qreal to) {
        if (from > to)
            std::swap(from, to);
        return std::any_of(lines.begin(), lines.end(), [&](const Line &l) {
            return std::abs(l.at - at) < clearance && l.from < to && l.to > from;
        });
    };

    // Centerline: out of each end past the leads and the gathering, then along one vertical
    // run where the conductors' runs are on average, kept clear of the gathering if it can be
    // and moved off other wires' runs.
    qreal run = runSum / qreal(n);
    const qreal xA = a0.point.x() + a0.direction * kLeadLength;
    const qreal xB = b0.point.x() + b0.direction * kLeadLength;
    // Level with the conductor pin nearest the middle of each end whose way to the run is
    // clear (else the nearest): a wire on a pin between the conductors stays visible.
    auto levelAt = [&](bool endA, qreal mid) {
        std::vector<qreal> ys;
        for (const Lane &lane : bundle.lanes)
            ys.push_back(endA ? lane.yA : lane.yB);
        std::stable_sort(ys.begin(), ys.end(),
                         [mid](qreal l, qreal r) { return std::abs(l - mid) < std::abs(r - mid) - 1e-6; });
        for (const qreal y : ys) {
            if (!blocked(legs, y, endA ? xA : xB, run))
                return y;
        }
        return ys.front();
    };
    const QPointF startA(xA, levelAt(true, (minA + maxA) / 2));
    const QPointF startB(xB, levelAt(false, (minB + maxB) / 2));
    const QPointF gatheredA = startA + QPointF(a0.direction * bundle.gatherA, 0);
    const QPointF gatheredB = startB + QPointF(b0.direction * bundle.gatherB, 0);
    qreal lo = -1e9, hi = 1e9;
    for (const auto &[at, direction] : {std::pair{gatheredA, a0.direction}, std::pair{gatheredB, b0.direction}}) {
        if (direction > 0)
            lo = std::max(lo, at.x() + WireItem::kCornerRadius);
        else
            hi = std::min(hi, at.x() - WireItem::kCornerRadius);
    }
    if (lo <= hi)
        run = std::clamp(run, lo, hi);
    if (std::abs(startA.y() - startB.y()) > 0.5 && blocked(runs, run, startA.y(), startB.y())) {
        // Try steps either side, nearest first, staying clear of the gathering.
        const qreal step = 2 * clearance;
        for (int k = 1; k <= 20; ++k) {
            const qreal x = run + (k % 2 ? 1 : -1) * step * ((k + 1) / 2);
            if ((lo > hi || (x >= lo && x <= hi)) && !blocked(runs, x, startA.y(), startB.y())) {
                run = x;
                break;
            }
        }
    }
    bundle.path = WireItem::roundedPath(
        {startA, gatheredA, QPointF(run, startA.y()), QPointF(run, startB.y()), gatheredB, startB},
        WireItem::kCornerRadius);

    // Strands: each leaves its lead, gathers into the bundle and twists (or, untwisted, runs
    // side by side) along it, a whole number of turns, then spreads out to its lead at end B.
    const qreal length = bundle.path.length();
    const qreal turns = std::max(1.0, std::round(length / twistPitch(n)));
    const int steps = std::max(2, int(std::ceil(length / kTwistStep)));
    std::vector<QPointF> centers;
    centers.reserve(steps + 1);
    for (int k = 0; k <= steps; ++k)
        centers.push_back(bundle.path.pointAtPercent(bundle.path.percentAtLength(length * k / steps)));
    bundle.strands.resize(n);
    for (int k = 0; k <= steps; ++k) {
        const qreal s = length * k / steps;
        const QPointF tangent = centers[std::min(k + 1, steps)] - centers[std::max(k - 1, 0)];
        const qreal tangentLength = std::hypot(tangent.x(), tangent.y());
        const QPointF normal = tangentLength > 1e-9 ? QPointF(-tangent.y(), tangent.x()) / tangentLength : QPointF(0, 1);
        for (size_t i = 0; i < n; ++i) {
            TwistPoint p;
            if (m_cable.twisted)
                p = twistAt(int(i), int(n), turns * s / length);
            else
                p = {n > 1 ? -1 + 2 * qreal(i) / qreal(n - 1) : 0, true};
            QPointF point = centers[k] + normal * bundle.radius * p.offset;
            // Out of and back into the leads, along the straight ends of the centerline.
            const qreal inA = smoothStep(s / bundle.gatherA);
            const qreal inB = smoothStep((length - s) / bundle.gatherB);
            if (inA < 1)
                point.setY(bundle.lanes[i].yA + (point.y() - bundle.lanes[i].yA) * inA);
            else if (inB < 1)
                point.setY(bundle.lanes[i].yB + (point.y() - bundle.lanes[i].yB) * inB);
            bundle.strands[i].points.push_back(point);
            bundle.strands[i].front.push_back(p.front);
        }
    }
    m_bundle = std::move(bundle);
    prepareGeometryChange();
    updateShape();
}

std::vector<QRectF> CableItem::labelRects() const
{
    std::vector<QRectF> rects;
    if (!m_bundle)
        return rects;
    const Bundle &b = *m_bundle;
    const qreal width = QFontMetricsF(cableFont()).horizontalAdvance(text()) + 4;
    const qreal length = b.path.length();
    std::vector<qreal> at;
    const qreal first = b.gatherA + kLabelInset;
    const qreal last = length - b.gatherB - kLabelInset;
    if (last - first > 2 * width)
        at = {first, last};
    else
        at = {length / 2};
    const qreal above = b.radius + (m_cable.shielded() ? kShieldMargin : 3);
    for (const qreal s : at) {
        const QPointF c = b.path.pointAtPercent(b.path.percentAtLength(s));
        rects.emplace_back(c.x() - width / 2, c.y() - above - kLabelHeight, width, kLabelHeight);
    }
    return rects;
}

QRectF CableItem::tagRect(const QPointF &at) const
{
    const qreal width = QFontMetricsF(cableFont()).horizontalAdvance(text()) + 8;
    return QRectF(at.x() - width / 2, at.y() - 7 - kLabelHeight, width, kLabelHeight);
}

void CableItem::updateShape()
{
    m_shape = QPainterPath();
    if (m_bundle) {
        QPainterPathStroker stroker;
        stroker.setWidth(2 * (m_bundle->radius + kShieldMargin));
        m_shape = stroker.createStroke(m_bundle->path);
        for (const QRectF &r : labelRects())
            m_shape.addRect(r);
    }
    for (const QPointF &tag : m_tags)
        m_shape.addRect(tagRect(tag));
    m_shape.setFillRule(Qt::WindingFill);
    m_bounds = m_shape.boundingRect().adjusted(-4, -4, 4, 4);
    if (m_bundle) {
        // Strands spread out to the leads beyond the stroke.
        for (const TwistStrand &strand : m_bundle->strands) {
            m_bounds = m_bounds.united(QPolygonF(QList<QPointF>(strand.points.begin(), strand.points.end()))
                                           .boundingRect()
                                           .adjusted(-6, -6, 6, 6));
        }
    }
}

QRectF CableItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath CableItem::shape() const
{
    return m_shape;
}

void CableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor ink = isSelected() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Text);
    const QColor outline = mix(pal.color(QPalette::Text), pal.color(QPalette::Base), 0.5);
    const QColor gap = canvasBackgroundColor(pal);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setFont(cableFont());
    if (m_bundle) {
        const Bundle &b = *m_bundle;
        if (isSelected()) {
            painter->setPen(QPen(pal.color(QPalette::Highlight), 2 * b.radius + WireItem::kPenWidth + 5,
                                 Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(b.path);
        }
        drawTwist(b.strands, [&](size_t i, const QPolygonF &run) {
            const Lane &lane = b.lanes[i];
            QColor color = wireColor(lane.baseColor);
            if (!color.isValid())
                color = mix(pal.color(QPalette::Text), pal.color(QPalette::Base), 0.4);
            painter->save();
            if (lane.factory)
                painter->setOpacity(0.6);
            // A gap around each run: where it crosses over another, that one breaks.
            if (m_cable.twisted) {
                painter->setPen(QPen(gap, WireItem::kPenWidth + 3.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
                painter->drawPolyline(run);
            }
            painter->setPen(QPen(outline, WireItem::kPenWidth + 1.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            painter->drawPolyline(run);
            painter->setPen(QPen(color, WireItem::kPenWidth, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            painter->drawPolyline(run);
            const QColor stripe = wireColor(lane.stripeColor);
            if (stripe.isValid()) {
                painter->setPen(QPen(stripe, WireItem::kPenWidth * 0.4, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
                painter->drawPolyline(run);
            }
            painter->restore();
        });
        if (m_cable.shielded()) {
            // The shield wraps the bundle.
            QPainterPathStroker stroker;
            stroker.setWidth(2 * (b.radius + kShieldMargin));
            stroker.setCapStyle(Qt::FlatCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            painter->setPen(QPen(ink, 1.4, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(stroker.createStroke(b.path).simplified());
        }
        painter->setPen(ink);
        for (const QRectF &r : labelRects())
            painter->drawText(r, Qt::AlignHCenter | Qt::AlignBottom, text());
    }
    for (const QPointF &tag : m_tags) {
        const QRectF r = tagRect(tag);
        painter->setPen(QPen(ink, 1.2, m_cable.shielded() ? Qt::DashLine : Qt::SolidLine));
        painter->setBrush(pal.color(QPalette::Base));
        painter->drawRoundedRect(r, 3, 3);
        painter->setPen(ink);
        painter->drawText(r, Qt::AlignCenter, text());
    }
}

} // namespace tbh::app
