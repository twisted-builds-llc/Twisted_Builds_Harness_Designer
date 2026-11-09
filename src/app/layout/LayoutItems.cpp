// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout/LayoutItems.h"

#include "layout/LayoutScene.h"
#include "ui/CanvasView.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPathStroker>

namespace tbh::app {

namespace {

QPalette paletteOf(const QGraphicsItem *item)
{
    return item->scene() ? item->scene()->palette() : QApplication::palette();
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

constexpr qreal kBoxWidth = 110;
constexpr qreal kBoxHeight = 28;
constexpr qreal kRadius = 7;

} // namespace

// ---- LayoutNodeItem -----------------------------------------------------------

LayoutNodeItem::LayoutNodeItem(const LayoutNode &node)
    : m_node(node)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(2);
    setPos(node.position);
}

void LayoutNodeItem::setNode(const LayoutNode &node, const QString &label)
{
    if (node.type != m_node.type)
        prepareGeometryChange();
    m_node = node;
    m_label = label;
    if (pos() != node.position)
        setPos(node.position);
    update();
}

QRectF LayoutNodeItem::boxRect() const
{
    return QRectF(-kBoxWidth / 2, -kBoxHeight / 2, kBoxWidth, kBoxHeight);
}

QRectF LayoutNodeItem::boundingRect() const
{
    if (m_node.type == LayoutNodeType::Connector)
        return boxRect().adjusted(-4, -4, 4, 4);
    return QRectF(-70, -kRadius - 4, 140, kRadius * 2 + 26);
}

QPainterPath LayoutNodeItem::shape() const
{
    QPainterPath path;
    if (m_node.type == LayoutNodeType::Connector)
        path.addRect(boxRect());
    else
        path.addEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
    return path;
}

void LayoutNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor highlight = pal.color(QPalette::Highlight);
    const bool unassigned = m_node.refId.isEmpty() && m_node.type != LayoutNodeType::Branch;
    QFont font = painter->font();
    font.setPixelSize(11);

    if (m_node.type == LayoutNodeType::Connector) {
        QPen pen(isSelected() ? highlight : mix(base, text, 0.5), isSelected() ? 2.5 : 1.2);
        if (unassigned)
            pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(mix(pal.color(QPalette::Button), highlight, 0.12));
        painter->drawRoundedRect(boxRect(), 4, 4);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(unassigned ? mix(text, base, 0.5) : text);
        const QString label = unassigned ? QApplication::translate("tbh::app::LayoutNodeItem", "(unassigned)")
                                         : m_label;
        painter->drawText(boxRect().adjusted(6, 0, -6, 0), Qt::AlignCenter,
                          painter->fontMetrics().elidedText(label, Qt::ElideRight, int(kBoxWidth - 12)));
        return;
    }

    if (isSelected()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(highlight);
        painter->drawEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
    }
    painter->setPen(QPen(text, 2));
    switch (m_node.type) {
    case LayoutNodeType::Branch:
        painter->setBrush(text);
        painter->drawEllipse(QPointF(0, 0), 4.5, 4.5);
        break;
    case LayoutNodeType::Splice:
        painter->setBrush(base);
        painter->drawEllipse(QPointF(0, 0), kRadius, kRadius);
        painter->setBrush(text);
        painter->drawEllipse(QPointF(0, 0), 3, 3);
        break;
    case LayoutNodeType::FlyingLead:
        painter->setBrush(base);
        painter->drawEllipse(QPointF(0, 0), kRadius, kRadius);
        painter->drawLine(QPointF(-4, -4), QPointF(4, 4));
        painter->drawLine(QPointF(-4, 4), QPointF(4, -4));
        break;
    case LayoutNodeType::Eyelet:
        painter->setBrush(base);
        painter->drawEllipse(QPointF(0, 0), kRadius, kRadius);
        painter->drawEllipse(QPointF(0, 0), 2.5, 2.5);
        break;
    case LayoutNodeType::Connector:
        break;
    }
    const QString label = unassigned ? QApplication::translate("tbh::app::LayoutNodeItem", "(unassigned)") : m_label;
    if (!label.isEmpty()) {
        painter->setFont(font);
        painter->setPen(unassigned ? mix(text, base, 0.5) : text);
        painter->drawText(QRectF(-70, kRadius + 4, 140, 16), Qt::AlignCenter,
                          painter->fontMetrics().elidedText(label, Qt::ElideRight, 140));
    }
}

QVariant LayoutNodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    auto *layout = qobject_cast<LayoutScene *>(scene());
    if (change == ItemPositionChange && layout && layout->snapToGrid())
        return snapToCanvasGrid(value.toPointF());
    if (change == ItemPositionHasChanged && layout)
        layout->nodeMoved(this);
    return QGraphicsItem::itemChange(change, value);
}

// ---- LayoutSegmentItem ------------------------------------------------------------

LayoutSegmentItem::LayoutSegmentItem(const LayoutSegment &segment)
    : m_segment(segment)
{
    setFlag(ItemIsSelectable);
    setZValue(1);
}

void LayoutSegmentItem::setSegment(const LayoutSegment &segment)
{
    m_segment = segment;
    update();
}

void LayoutSegmentItem::setEnds(const QPointF &a, const QPointF &b)
{
    prepareGeometryChange();
    m_a = a;
    m_b = b;
}

void LayoutSegmentItem::setCaption(const QString &lengthText, const QString &usageText)
{
    if (lengthText == m_lengthText && usageText == m_usageText)
        return;
    prepareGeometryChange();
    m_lengthText = lengthText;
    m_usageText = usageText;
}

QRectF LayoutSegmentItem::labelRect() const
{
    return QRectF(midpoint() - QPointF(60, 17), QSizeF(120, 34));
}

QRectF LayoutSegmentItem::boundingRect() const
{
    return QRectF(m_a, m_b).normalized().adjusted(-8, -8, 8, 8).united(labelRect());
}

QPainterPath LayoutSegmentItem::shape() const
{
    QPainterPath line(m_a);
    line.lineTo(m_b);
    QPainterPathStroker stroker;
    stroker.setWidth(12);
    QPainterPath path = stroker.createStroke(line);
    path.addRect(labelRect());
    return path;
}

void LayoutSegmentItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const bool noLength = m_segment.lengthMm <= 0;
    if (isSelected()) {
        painter->setPen(QPen(pal.color(QPalette::Highlight), 10, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(m_a, m_b);
    }
    QPen pen(noLength ? QColor(0xd6, 0x27, 0x28) : mix(base, text, 0.55), 5, Qt::SolidLine, Qt::RoundCap);
    if (noLength)
        pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->drawLine(m_a, m_b);

    // Caption box: length (bold) over wire count.
    const QRectF box = labelRect().adjusted(14, 0, -14, 0);
    painter->setPen(QPen(mix(base, text, 0.3), 1));
    painter->setBrush(base);
    painter->drawRoundedRect(box, 4, 4);
    QFont font = painter->font();
    font.setPixelSize(11);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(noLength ? QColor(0xd6, 0x27, 0x28) : text);
    painter->drawText(QRectF(box.left(), box.top() + 1, box.width(), 16), Qt::AlignCenter, m_lengthText);
    font.setBold(false);
    font.setPixelSize(10);
    painter->setFont(font);
    painter->setPen(mix(text, base, 0.35));
    painter->drawText(QRectF(box.left(), box.top() + 16, box.width(), 16), Qt::AlignCenter, m_usageText);
}

} // namespace tbh::app
