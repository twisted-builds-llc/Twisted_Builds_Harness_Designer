// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Graphics items of the layout editor. Positions are for drawing only; segment lengths
// are the entered values, never derived from the drawing.

#include "model/Harness.h"

#include <QGraphicsItem>
#include <QPainterPath>

namespace tbh::app {

enum LayoutItemType {
    LayoutNodeItemType = QGraphicsItem::UserType + 20,
    LayoutSegmentItemType,
};

class LayoutNodeItem : public QGraphicsItem
{
public:
    explicit LayoutNodeItem(const LayoutNode &node);

    int type() const override { return LayoutNodeItemType; }
    QString id() const { return m_node.id; }
    const LayoutNode &node() const { return m_node; }
    // `label` is the name of what the node stands for (empty for branch points).
    void setNode(const LayoutNode &node, const QString &label);
    // True if `itemPos` is on one of the short leads left and right of the node. Dragging
    // a lead draws a segment; dragging the node itself moves it.
    bool isStubAt(const QPointF &itemPos) const;
    void setStubHovered(bool on);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QRectF boxRect() const;
    QRectF stubRect(bool right) const;

    LayoutNode m_node;
    QString m_label;
    bool m_stubHovered = false;
};

class LayoutSegmentItem : public QGraphicsItem
{
public:
    explicit LayoutSegmentItem(const LayoutSegment &segment);

    int type() const override { return LayoutSegmentItemType; }
    QString id() const { return m_segment.id; }
    const LayoutSegment &segment() const { return m_segment; }
    void setSegment(const LayoutSegment &segment);
    void setEnds(const QPointF &a, const QPointF &b);
    // Caption under the length, e.g. "12 wires".
    void setCaption(const QString &lengthText, const QString &usageText);
    QPointF midpoint() const { return (m_a + m_b) / 2; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QRectF labelRect() const;

    LayoutSegment m_segment;
    QPointF m_a;
    QPointF m_b;
    QString m_lengthText;
    QString m_usageText;
};

} // namespace tbh::app
