// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Graphics items of the layout editor. Positions are for drawing only; segment lengths
// are the entered values, never derived from the drawing.

#include "model/Harness.h"

#include <QColor>
#include <QGraphicsItem>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

#include <optional>
#include <vector>

namespace tbh::app {

enum LayoutItemType {
    LayoutNodeItemType = QGraphicsItem::UserType + 20,
    LayoutSegmentItemType,
};

// Curved segments (LayoutSegment::bend): quadratic Bezier from `a` to `b`.
// Control point of the curve; the chord midpoint for a straight segment.
QPointF bendControlPoint(const QPointF &a, const QPointF &b, const std::optional<QPointF> &bend);
// Bend that makes the curve pass through `midpoint` halfway along; nullopt (straight) when
// `midpoint` is within a few pixels of the chord midpoint or the ends coincide.
std::optional<QPointF> bendThroughMidpoint(const QPointF &a, const QPointF &b, const QPointF &midpoint);

// A segment cut in two at a point along it (see splitSegmentAt()).
struct SegmentSplit
{
    QPointF point;          // on the curve
    double fraction = 0.5;  // drawn length from `a` to `point` over the whole drawn length
    std::optional<QPointF> bendA; // a to point; nullopt for a straight segment
    std::optional<QPointF> bendB; // point to b
};
// Cuts the segment from `a` to `b` at the point of the curve nearest `scenePos`, kept at
// least 5 % from either end. The halves trace the same curve.
SegmentSplit splitNear(const QPointF &a, const QPointF &b, const std::optional<QPointF> &bend,
                       const QPointF &scenePos);
// Cuts the segment at curve parameter `u` (0 at `a`, 1 at `b`).
SegmentSplit splitAt(const QPointF &a, const QPointF &b, const std::optional<QPointF> &bend, qreal u);

class LayoutNodeItem : public QGraphicsItem
{
public:
    explicit LayoutNodeItem(const LayoutNode &node);

    int type() const override { return LayoutNodeItemType; }
    QString id() const { return m_node.id; }
    const LayoutNode &node() const { return m_node; }
    // `label` is the name of what the node stands for (empty for branch points).
    void setNode(const LayoutNode &node, const QString &label);
    // Dimmer second line under the name, e.g. a splice's total run length; empty = none.
    void setSubLabel(const QString &text);
    QString subLabel() const { return m_subLabel; }
    // True if `itemPos` is on one of the short leads beside the node. Dragging a lead draws
    // a segment; dragging the node itself moves it.
    bool isStubAt(const QPointF &itemPos) const;
    void setStubHovered(bool on);
    // A segment leaving this node: unit direction (item coords) towards its far end and its
    // drawn line width.
    struct Leg
    {
        QPointF direction;
        qreal width = 0;
        bool operator==(const Leg &) const = default;
    };
    // Branch points draw a breakout boot around their segments: a block for 0-2 segments,
    // a Y for 3 (trunk plus two outlets), and one more leg per extra segment. Connectors and
    // eyelets face away from their segments (see facingFor()).
    void setLegs(std::vector<Leg> legs);
    int legCount() const { return int(m_legs.size()); }
    // Filled outline of a branch point for `legs` (see setLegs()).
    static QPainterPath branchPath(const std::vector<Leg> &legs);
    // Direction an end icon faces, in quarter turns clockwise from up (0 up, 1 right,
    // 2 down, 3 left): away from the sum of its leg directions, snapped to the nearest
    // axis (vertical on a tie). Up without legs.
    static int facingFor(const std::vector<Leg> &legs);
    int facing() const { return m_facing; }
    // Name label, in item coordinates.
    QRectF labelRect() const;
    // Name of what the node stands for; empty for branch points.
    QString label() const { return displayLabel(); }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    // Name shown beside the node, "(unassigned)" for an end node without a reference.
    QString displayLabel() const;
    QStringList subLabelLines() const;
    // Node types drawn as an icon facing away from their wire (connector, eyelet).
    bool hasIcon() const;
    // Outline of the icon in the facing-up frame (wire enters at the origin from below).
    QPainterPath iconPath() const;
    // How far the icon reaches from the origin towards the facing direction.
    qreal iconReach() const;
    // Lead rectangles, in the facing frame for icons, else in item coordinates.
    QRectF stubRect(bool right) const;
    QTransform facingTransform() const;

    LayoutNode m_node;
    QString m_label;
    bool m_stubHovered = false;
    bool m_hovered = false; // the segment-drawing leads only show on hover or selection
    std::vector<Leg> m_legs;
    QPainterPath m_branchPath; // branchPath(m_legs)
    int m_facing = 0;          // facingFor(m_legs)
    qreal m_labelWidth = 0;
    QString m_subLabel;
    qreal m_subLabelWidth = 0;
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
    // Caption along the line: the length, then optional detail drawn dimmer.
    void setCaption(const QString &lengthText, const QString &detailText);
    // A routed wire, drawn as one colored stripe of the bundle.
    struct WireStripe
    {
        QColor base;
        QColor stripe; // invalid = no stripe
        bool operator==(const WireStripe &) const = default;
    };
    // Wires routed through, in a stable order; heavier bundles draw thicker.
    void setWires(std::vector<WireStripe> wires);
    int wireCount() const { return int(m_wires.size()); }
    qreal lineWidth() const { return lineWidthFor(wireCount()); }
    static qreal lineWidthFor(int wireCount);
    // Point halfway along the (possibly curved) segment; the bend handle sits here.
    QPointF midpoint() const;
    QPointF endA() const { return m_a; }
    QPointF endB() const { return m_b; }
    // True if `scenePos` is within `tolerance` of the bend handle (shown when selected).
    bool isBendHandleAt(const QPointF &scenePos, qreal tolerance) const;
    // Caption beside the line (above it, or left of a vertical one), rotated along it.
    QPolygonF labelPolygon() const;
    QRectF labelRect() const { return labelPolygon().boundingRect(); }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    // Caption frame: origin at the midpoint, x along the line, text upright.
    QTransform captionTransform() const;
    QPainterPath path() const;
    // Points along the segment with unit normals, for drawing parallel stripes.
    std::vector<std::pair<QPointF, QPointF>> samples() const;

    LayoutSegment m_segment;
    QPointF m_a;
    QPointF m_b;
    QString m_lengthText;
    QString m_detailText;
    qreal m_lengthWidth = 0; // text widths at the caption font
    qreal m_detailWidth = 0;
    std::vector<WireStripe> m_wires;
};

} // namespace tbh::app
