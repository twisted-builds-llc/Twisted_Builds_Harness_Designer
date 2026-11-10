// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout/LayoutItems.h"

#include "layout/LayoutScene.h"
#include "ui/CanvasView.h"

#include <QApplication>
#include <QPainter>
#include <QFontMetricsF>
#include <QPainterPathStroker>
#include <QTransform>

#include <cmath>
#include <numbers>

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

constexpr qreal kRadius = 7;
constexpr qreal kStubLength = 14;
// Branch point breakout boot.
constexpr qreal kBlockLength = 28; // block of a 0-2 segment branch
constexpr qreal kTrunkLength = 20; // trunk block of a 3+ segment branch, from the fork
constexpr qreal kLegLength = 26;   // outlet legs, from the fork
constexpr qreal kBootMargin = 3;   // boot extends this far past each side of its segment line
// End icons, in the facing-up frame (the wire enters at the origin from below).
constexpr qreal kPlugHalfWidth = 12;   // connector body
constexpr qreal kPlugReach = 21;       // connector body top
constexpr qreal kPlugNeck = 5;         // narrow back where the wires enter
constexpr qreal kRingRadius = 6;       // eyelet ring
constexpr qreal kRingReach = 10 + 2 * kRingRadius;
constexpr qreal kLabelGap = 5;         // icon to name label
constexpr qreal kLabelHeight = 16;
// Segment caption.
constexpr qreal kCaptionGap = 3;   // between the bundle edge and the text
constexpr qreal kCaptionHeight = 15;
constexpr int kCaptionPixelSize = 12;
// Bundle drawing: each wire gets this much width, within these bounds.
constexpr qreal kWirePitch = 1.5;
constexpr qreal kMinLineWidth = 4;
constexpr qreal kMaxLineWidth = 18;

QFont nodeFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(12);
    font.setBold(true);
    return font;
}

QFont captionFont(bool bold)
{
    QFont font = QApplication::font();
    font.setPixelSize(kCaptionPixelSize);
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return font;
}

// Gray of connector bodies and branch boots.
QColor bodyColor(const QPalette &pal)
{
    return mix(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.5);
}

// A wire color as drawn on the canvas: near-black wires stay visible on a dark canvas
// and near-white ones on a light canvas.
QColor visibleWireColor(QColor color, const QPalette &pal)
{
    if (!color.isValid())
        return mix(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.45);
    const bool darkCanvas = pal.color(QPalette::Base).lightness() < 128;
    if (darkCanvas && color.lightness() < 60)
        return QColor(0x4a, 0x4a, 0x4a);
    if (!darkCanvas && color.lightness() > 230)
        return QColor(0xc8, 0xc8, 0xc8);
    return color;
}

qreal angleOf(const QPointF &direction)
{
    return std::atan2(direction.y(), direction.x()) * 180.0 / std::numbers::pi;
}

// Rectangle from `from` to `to` along the x axis, `width` across, rotated to `direction`.
QPainterPath block(qreal from, qreal to, qreal width, const QPointF &direction)
{
    QPainterPath path;
    path.addRect(QRectF(from, -width / 2, to - from, width));
    return QTransform().rotate(angleOf(direction)).map(path);
}

// Bends whose curve midpoint is this close to the chord midpoint count as straight.
constexpr qreal kStraightTolerance = 4;
constexpr int kCurveSamples = 32;
constexpr qreal kHandleRadius = 5;

} // namespace

QPointF bendControlPoint(const QPointF &a, const QPointF &b, const std::optional<QPointF> &bend)
{
    if (!bend)
        return (a + b) / 2;
    const QPointF d = b - a;
    return a + bend->x() * d + bend->y() * QPointF(-d.y(), d.x());
}

std::optional<QPointF> bendThroughMidpoint(const QPointF &a, const QPointF &b, const QPointF &midpoint)
{
    const QPointF d = b - a;
    const qreal lengthSquared = d.x() * d.x() + d.y() * d.y();
    if (lengthSquared < 1e-6 || QLineF(midpoint, (a + b) / 2).length() <= kStraightTolerance)
        return std::nullopt;
    // A quadratic Bezier is at (a + 2c + b) / 4 halfway, so c = 2 * midpoint - (a + b) / 2.
    const QPointF v = 2 * midpoint - (a + b) / 2 - a;
    const QPointF n(-d.y(), d.x());
    return QPointF((v.x() * d.x() + v.y() * d.y()) / lengthSquared, (v.x() * n.x() + v.y() * n.y()) / lengthSquared);
}

namespace {

// Inverse of bendControlPoint() for a curve with control point `c`.
std::optional<QPointF> bendFromControl(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF d = b - a;
    const qreal lengthSquared = d.x() * d.x() + d.y() * d.y();
    if (lengthSquared < 1e-6)
        return std::nullopt;
    const QPointF v = c - a;
    const QPointF n(-d.y(), d.x());
    return QPointF((v.x() * d.x() + v.y() * d.y()) / lengthSquared, (v.x() * n.x() + v.y() * n.y()) / lengthSquared);
}

} // namespace

SegmentSplit splitNear(const QPointF &a, const QPointF &b, const std::optional<QPointF> &bend,
                       const QPointF &scenePos)
{
    const QPointF c = bendControlPoint(a, b, bend);
    auto at = [&](qreal u) { return (1 - u) * (1 - u) * a + 2 * (1 - u) * u * c + u * u * b; };
    // Sample the curve: nearest sample to the drop, and drawn length up to each sample.
    constexpr int kSteps = 200;
    std::vector<qreal> lengthTo(kSteps + 1, 0.0);
    int nearest = 0;
    qreal nearestDistance = QLineF(at(0), scenePos).length();
    for (int i = 1; i <= kSteps; ++i) {
        const QPointF p = at(qreal(i) / kSteps);
        lengthTo[size_t(i)] = lengthTo[size_t(i - 1)] + QLineF(at(qreal(i - 1) / kSteps), p).length();
        const qreal distance = QLineF(p, scenePos).length();
        if (distance < nearestDistance) {
            nearest = i;
            nearestDistance = distance;
        }
    }
    const int lo = kSteps / 20;
    nearest = std::clamp(nearest, lo, kSteps - lo);
    const qreal u = qreal(nearest) / kSteps;
    SegmentSplit split = splitAt(a, b, bend, u);
    split.fraction = lengthTo.back() > 1e-6 ? lengthTo[size_t(nearest)] / lengthTo.back() : u;
    return split;
}

SegmentSplit splitAt(const QPointF &a, const QPointF &b, const std::optional<QPointF> &bend, qreal u)
{
    const QPointF c = bendControlPoint(a, b, bend);
    SegmentSplit split;
    split.point = (1 - u) * (1 - u) * a + 2 * (1 - u) * u * c + u * u * b;
    split.fraction = u;
    if (bend) {
        // de Casteljau: the halves of a quadratic Bezier at u.
        split.bendA = bendFromControl(a, split.point, a + u * (c - a));
        split.bendB = bendFromControl(split.point, b, c + u * (b - c));
    }
    return split;
}

// ---- LayoutNodeItem -----------------------------------------------------------

LayoutNodeItem::LayoutNodeItem(const LayoutNode &node)
    : m_node(node)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(2);
    setAcceptHoverEvents(true);
    setPos(node.position);
    m_branchPath = branchPath({});
    setNode(node, {});
}

QPainterPath LayoutNodeItem::branchPath(const std::vector<Leg> &legs)
{
    const size_t n = legs.size();
    qreal widest = LayoutSegmentItem::lineWidthFor(0);
    for (const Leg &leg : legs)
        widest = std::max(widest, leg.width);
    if (n <= 2) {
        // A block along the line through the node; a pass-through is drawn straight.
        QPointF axis(1, 0);
        if (n == 1) {
            axis = legs[0].direction;
        } else if (n == 2) {
            const QPointF d = legs[0].direction - legs[1].direction;
            axis = std::hypot(d.x(), d.y()) > 1e-6 ? d : legs[0].direction;
        }
        return block(-kBlockLength / 2, kBlockLength / 2, widest + 2 * kBootMargin, axis);
    }
    // Trunk: the segment pointing most against the others (the bundle the outlets leave).
    size_t trunk = 0;
    qreal best = 0;
    for (size_t i = 0; i < n; ++i) {
        QPointF others;
        for (size_t j = 0; j < n; ++j) {
            if (j != i)
                others += legs[j].direction;
        }
        const qreal dot = legs[i].direction.x() * others.x() + legs[i].direction.y() * others.y();
        if (i == 0 || dot < best) {
            best = dot;
            trunk = i;
        }
    }
    const qreal hubRadius = widest / 2 + kBootMargin;
    QPainterPath hub;
    hub.addEllipse(QPointF(0, 0), hubRadius, hubRadius);
    // united(), not addPath(): overlapping subpaths would leave holes under odd-even fill.
    QPainterPath path =
        block(-hubRadius, kTrunkLength, legs[trunk].width + 2 * kBootMargin, legs[trunk].direction).united(hub);
    QPainterPathStroker stroker;
    stroker.setCapStyle(Qt::FlatCap);
    for (size_t i = 0; i < n; ++i) {
        if (i == trunk)
            continue;
        QPainterPath line(QPointF(0, 0));
        line.lineTo(legs[i].direction * kLegLength);
        stroker.setWidth(legs[i].width + 2 * kBootMargin);
        path = path.united(stroker.createStroke(line).simplified());
    }
    return path;
}

int LayoutNodeItem::facingFor(const std::vector<Leg> &legs)
{
    QPointF sum;
    for (const Leg &leg : legs)
        sum += leg.direction;
    const QPointF away = -sum;
    if (std::hypot(away.x(), away.y()) < 1e-6)
        return 0;
    if (std::abs(away.x()) > std::abs(away.y()))
        return away.x() > 0 ? 1 : 3;
    return away.y() > 0 ? 2 : 0;
}

void LayoutNodeItem::setLegs(std::vector<Leg> legs)
{
    if (legs == m_legs)
        return;
    prepareGeometryChange();
    m_legs = std::move(legs);
    m_branchPath = branchPath(m_legs);
    m_facing = facingFor(m_legs);
    update();
}

void LayoutNodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = true;
    update();
    QGraphicsItem::hoverEnterEvent(event);
}

void LayoutNodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = false;
    update();
    QGraphicsItem::hoverLeaveEvent(event);
}

void LayoutNodeItem::setNode(const LayoutNode &node, const QString &label)
{
    prepareGeometryChange();
    m_node = node;
    m_label = label;
    m_labelWidth = QFontMetricsF(nodeFont()).horizontalAdvance(displayLabel()) + 4;
    if (pos() != node.position)
        setPos(node.position);
    update();
}

QString LayoutNodeItem::displayLabel() const
{
    if (m_node.type == LayoutNodeType::Branch)
        return {};
    if (m_node.refId.isEmpty())
        return QApplication::translate("tbh::app::LayoutNodeItem", "(unassigned)");
    return m_label;
}

bool LayoutNodeItem::hasIcon() const
{
    return m_node.type == LayoutNodeType::Connector || m_node.type == LayoutNodeType::Eyelet;
}

QTransform LayoutNodeItem::facingTransform() const
{
    return QTransform().rotate(90.0 * m_facing);
}

qreal LayoutNodeItem::iconReach() const
{
    switch (m_node.type) {
    case LayoutNodeType::Connector:
        return kPlugReach;
    case LayoutNodeType::Eyelet:
        return kRingReach;
    default:
        return kRadius;
    }
}

QPainterPath LayoutNodeItem::iconPath() const
{
    QPainterPath path;
    if (m_node.type == LayoutNodeType::Connector) {
        // A plug seen from the side: body, and a narrower back where the wires enter.
        path.addRoundedRect(QRectF(-kPlugHalfWidth, -kPlugReach, 2 * kPlugHalfWidth, kPlugReach - kPlugNeck), 3, 3);
        path.addRect(QRectF(-kPlugHalfWidth / 2, -kPlugNeck - 1, kPlugHalfWidth, kPlugNeck + 1));
        return path.simplified();
    }
    // Ring terminal: crimp barrel, then the ring.
    path.addRect(QRectF(-3, -10, 6, 10));
    path.addEllipse(QPointF(0, -10 - kRingRadius), kRingRadius, kRingRadius);
    return path.simplified();
}

QRectF LayoutNodeItem::stubRect(bool right) const
{
    if (hasIcon()) {
        // Beside the icon body, across the wire; 10 px tall so it is easy to grab.
        const qreal y = m_node.type == LayoutNodeType::Connector ? -(kPlugReach + kPlugNeck) / 2 : -10 - kRingRadius;
        const qreal inner = (m_node.type == LayoutNodeType::Connector ? kPlugHalfWidth : kRingRadius) + 2;
        return right ? QRectF(inner, y - 5, kStubLength, 10) : QRectF(-inner - kStubLength, y - 5, kStubLength, 10);
    }
    const qreal inner = kRadius + 1;
    return right ? QRectF(inner, -5, kStubLength, 10) : QRectF(-inner - kStubLength, -5, kStubLength, 10);
}

bool LayoutNodeItem::isStubAt(const QPointF &itemPos) const
{
    if (hasIcon()) {
        const QPointF p = facingTransform().inverted().map(itemPos);
        return stubRect(false).contains(p) || stubRect(true).contains(p);
    }
    if (!stubRect(false).contains(itemPos) && !stubRect(true).contains(itemPos))
        return false;
    // Not on the dot of a round node (its selection ring overlaps the lead).
    return std::hypot(itemPos.x(), itemPos.y()) > kRadius + 1;
}

void LayoutNodeItem::setStubHovered(bool on)
{
    if (on == m_stubHovered)
        return;
    m_stubHovered = on;
    update();
}

QRectF LayoutNodeItem::labelRect() const
{
    if (m_node.type == LayoutNodeType::Branch)
        return {};
    const qreal width = std::max(m_labelWidth, 20.0);
    const qreal d = (hasIcon() ? iconReach() : kRadius) + kLabelGap;
    switch (hasIcon() ? m_facing : 2) {
    case 1:
        return QRectF(d, -kLabelHeight / 2, width, kLabelHeight);
    case 2:
        return QRectF(-width / 2, d, width, kLabelHeight);
    case 3:
        return QRectF(-d - width, -kLabelHeight / 2, width, kLabelHeight);
    default:
        return QRectF(-width / 2, -d - kLabelHeight, width, kLabelHeight);
    }
}

QRectF LayoutNodeItem::boundingRect() const
{
    QRectF rect(-30, -30, 60, 60); // icons, leads and selection rings in any facing
    if (m_node.type == LayoutNodeType::Branch)
        return rect | m_branchPath.boundingRect().adjusted(-4, -4, 4, 4);
    return rect | labelRect().adjusted(-2, -2, 2, 2);
}

QPainterPath LayoutNodeItem::shape() const
{
    // Winding fill: overlapping parts (dot and boot) must not cancel out into a hole.
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (hasIcon()) {
        QPainterPath local = iconPath();
        local.addRect(stubRect(false));
        local.addRect(stubRect(true));
        path.addPath(facingTransform().map(local));
    } else {
        path.addEllipse(QPointF(0, 0), kRadius + 4, kRadius + 4);
        if (m_node.type == LayoutNodeType::Branch)
            path.addPath(m_branchPath);
        path.addRect(stubRect(false));
        path.addRect(stubRect(true));
    }
    if (m_node.type != LayoutNodeType::Branch)
        path.addRect(labelRect());
    return path;
}

void LayoutNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor body = bodyColor(pal);
    const bool unassigned = m_node.refId.isEmpty() && m_node.type != LayoutNodeType::Branch;
    painter->setRenderHint(QPainter::Antialiasing);

    // Leads beside the node: drag handles for drawing a segment. Hidden until the node is
    // hovered or selected so they do not clutter the drawing.
    if (m_hovered || m_stubHovered || isSelected()) {
        painter->save();
        if (hasIcon())
            painter->setTransform(facingTransform(), true);
        painter->setPen(QPen(m_stubHovered ? highlight : mix(base, text, 0.5), 2, Qt::SolidLine, Qt::FlatCap));
        for (bool right : {false, true}) {
            const QRectF r = stubRect(right);
            painter->drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
        }
        painter->restore();
    }

    if (m_node.type == LayoutNodeType::Branch) {
        if (isSelected()) {
            painter->setPen(QPen(highlight, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(highlight);
            painter->drawPath(m_branchPath);
        }
        painter->setPen(QPen(body.darker(160), 1));
        painter->setBrush(body);
        painter->drawPath(m_branchPath);
        return; // branch points have no label
    }

    if (hasIcon()) {
        painter->save();
        painter->setTransform(facingTransform(), true);
        const QPainterPath icon = iconPath();
        if (isSelected()) {
            painter->setPen(QPen(highlight, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(icon);
        }
        QPen pen(body.darker(170), 1);
        if (unassigned)
            pen = QPen(mix(text, base, 0.3), 1.2, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(unassigned ? QColor(Qt::transparent) : body);
        painter->drawPath(icon);
        if (m_node.type == LayoutNodeType::Connector) {
            // Mating face edge near the top.
            painter->setPen(QPen(body.darker(150), 1.2));
            painter->drawLine(QPointF(-kPlugHalfWidth + 2, -kPlugReach + 4), QPointF(kPlugHalfWidth - 2, -kPlugReach + 4));
        } else {
            painter->setBrush(pal.color(QPalette::Base).darker(115)); // ring hole
            painter->drawEllipse(QPointF(0, -10 - kRingRadius), kRingRadius / 2.2, kRingRadius / 2.2);
        }
        painter->restore();
    } else {
        if (isSelected()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(highlight);
            painter->drawEllipse(QPointF(0, 0), kRadius + 3, kRadius + 3);
        }
        painter->setPen(QPen(body.darker(170), 1));
        painter->setBrush(body);
        if (m_node.type == LayoutNodeType::Splice) {
            painter->drawEllipse(QPointF(0, 0), kRadius - 1, kRadius - 1);
            painter->setBrush(body.darker(200));
            painter->drawEllipse(QPointF(0, 0), 2.5, 2.5);
        } else { // flying lead: an open wire end
            painter->setPen(QPen(body, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QPointF(0, 0), 4, 4);
        }
    }

    const QString label = displayLabel();
    if (!label.isEmpty()) {
        painter->setFont(nodeFont());
        painter->setPen(unassigned ? mix(text, base, 0.5) : text);
        const QRectF r = labelRect();
        Qt::Alignment align = Qt::AlignVCenter | Qt::AlignHCenter;
        if (hasIcon() && m_facing == 1)
            align = Qt::AlignVCenter | Qt::AlignLeft;
        else if (hasIcon() && m_facing == 3)
            align = Qt::AlignVCenter | Qt::AlignRight;
        painter->drawText(r, align, label);
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
    if (segment.bend != m_segment.bend)
        prepareGeometryChange();
    m_segment = segment;
    update();
}

QPainterPath LayoutSegmentItem::path() const
{
    QPainterPath path(m_a);
    if (m_segment.bend)
        path.quadTo(bendControlPoint(m_a, m_b, m_segment.bend), m_b);
    else
        path.lineTo(m_b);
    return path;
}

QPointF LayoutSegmentItem::midpoint() const
{
    return (m_a + 2 * bendControlPoint(m_a, m_b, m_segment.bend) + m_b) / 4;
}

std::vector<std::pair<QPointF, QPointF>> LayoutSegmentItem::samples() const
{
    const QPointF c = bendControlPoint(m_a, m_b, m_segment.bend);
    const int steps = m_segment.bend ? kCurveSamples : 1;
    std::vector<std::pair<QPointF, QPointF>> result;
    result.reserve(size_t(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const qreal u = qreal(i) / steps;
        const QPointF point = (1 - u) * (1 - u) * m_a + 2 * (1 - u) * u * c + u * u * m_b;
        QPointF tangent = 2 * (1 - u) * (c - m_a) + 2 * u * (m_b - c);
        if (std::hypot(tangent.x(), tangent.y()) < 1e-6)
            tangent = m_b - m_a;
        const qreal length = std::hypot(tangent.x(), tangent.y());
        result.emplace_back(point, length > 1e-6 ? QPointF(-tangent.y(), tangent.x()) / length : QPointF(0, 1));
    }
    return result;
}

bool LayoutSegmentItem::isBendHandleAt(const QPointF &scenePos, qreal tolerance) const
{
    return isSelected() && QLineF(scenePos, midpoint()).length() <= kHandleRadius + tolerance;
}

void LayoutSegmentItem::setEnds(const QPointF &a, const QPointF &b)
{
    prepareGeometryChange();
    m_a = a;
    m_b = b;
}

void LayoutSegmentItem::setCaption(const QString &lengthText, const QString &detailText)
{
    if (lengthText == m_lengthText && detailText == m_detailText)
        return;
    prepareGeometryChange();
    m_lengthText = lengthText;
    m_detailText = detailText;
    m_lengthWidth = QFontMetricsF(captionFont(true)).horizontalAdvance(m_lengthText);
    m_detailWidth = m_detailText.isEmpty() ? 0 : QFontMetricsF(captionFont(false)).horizontalAdvance(m_detailText) + 6;
}

void LayoutSegmentItem::setWires(std::vector<WireStripe> wires)
{
    if (wires == m_wires)
        return;
    prepareGeometryChange();
    m_wires = std::move(wires);
    update();
}

qreal LayoutSegmentItem::lineWidthFor(int wireCount)
{
    // Each wire is a stripe kWirePitch wide; thin bundles stay easy to click, big ones
    // stop growing and squeeze their stripes.
    return std::clamp(kWirePitch * std::max(wireCount, 0), kMinLineWidth, kMaxLineWidth);
}

QPolygonF LayoutSegmentItem::labelPolygon() const
{
    // Text frame: x along the line, rotated so the text reads left to right (or bottom to
    // top for a vertical line); the caption sits on the frame's -y side of the line.
    const qreal width = m_lengthWidth + m_detailWidth;
    const qreal bottom = -(lineWidth() / 2 + kCaptionGap);
    const QRectF local(-width / 2, bottom - kCaptionHeight, width, kCaptionHeight);
    return captionTransform().map(QPolygonF(local));
}

QTransform LayoutSegmentItem::captionTransform() const
{
    const QPointF d = m_b - m_a;
    qreal angle = std::atan2(d.y(), d.x()) * 180.0 / std::numbers::pi;
    if (angle >= 90)
        angle -= 180;
    else if (angle < -90)
        angle += 180;
    const QPointF mid = midpoint();
    return QTransform().translate(mid.x(), mid.y()).rotate(angle);
}

QRectF LayoutSegmentItem::boundingRect() const
{
    const qreal margin = lineWidth() / 2 + kHandleRadius + 6;
    return path().controlPointRect().adjusted(-margin, -margin, margin, margin).united(labelRect());
}

QPainterPath LayoutSegmentItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(12.0, lineWidth() + 4));
    QPainterPath path = stroker.createStroke(this->path());
    path.setFillRule(Qt::WindingFill);
    path.addPolygon(labelPolygon());
    return path;
}

void LayoutSegmentItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = paletteOf(this);
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor warning(0xd6, 0x27, 0x28);
    const bool noLength = m_segment.lengthMm <= 0;
    const qreal width = lineWidth();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    const QPainterPath centerLine = path();
    if (isSelected()) {
        painter->setPen(QPen(pal.color(QPalette::Highlight), width + 6, Qt::SolidLine, Qt::RoundCap));
        painter->drawPath(centerLine);
    }

    if (m_wires.empty() || QLineF(m_a, m_b).length() < 1e-6) {
        QPen pen(noLength ? warning : mix(base, text, 0.4), width, Qt::SolidLine, Qt::FlatCap);
        if (noLength)
            pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->drawPath(centerLine);
    } else {
        // One stripe per wire, side by side across the bundle, following the curve.
        const auto points = samples();
        const qreal pitch = width / qreal(m_wires.size());
        for (size_t i = 0; i < m_wires.size(); ++i) {
            const qreal offset = -width / 2 + pitch * (qreal(i) + 0.5);
            QPolygonF line;
            line.reserve(qsizetype(points.size()));
            for (const auto &[point, normal] : points)
                line.append(point + normal * offset);
            // Slight overlap so antialiasing leaves no gaps between stripes.
            painter->setPen(QPen(visibleWireColor(m_wires[i].base, pal), pitch + 0.4, Qt::SolidLine, Qt::FlatCap,
                                 Qt::RoundJoin));
            painter->drawPolyline(line);
            if (m_wires[i].stripe.isValid()) {
                painter->setPen(QPen(visibleWireColor(m_wires[i].stripe, pal), pitch * 0.4, Qt::SolidLine,
                                     Qt::FlatCap, Qt::RoundJoin));
                painter->drawPolyline(line);
            }
        }
    }
    if (isSelected()) {
        // Bend handle: drag to curve the segment.
        painter->setPen(QPen(text, 1.5));
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->drawEllipse(midpoint(), kHandleRadius, kHandleRadius);
    }

    // Caption: length, then the detail, along the line.
    painter->save();
    painter->setTransform(captionTransform(), true);
    const qreal total = m_lengthWidth + m_detailWidth;
    const qreal bottom = -(width / 2 + kCaptionGap);
    const QRectF lengthRect(-total / 2, bottom - kCaptionHeight, m_lengthWidth + 1, kCaptionHeight);
    painter->setFont(captionFont(true));
    painter->setPen(noLength ? warning : text);
    painter->drawText(lengthRect, Qt::AlignLeft | Qt::AlignBottom, m_lengthText);
    if (!m_detailText.isEmpty()) {
        painter->setFont(captionFont(false));
        painter->setPen(mix(text, base, 0.4));
        painter->drawText(QRectF(lengthRect.right() + 5, lengthRect.top(), m_detailWidth, kCaptionHeight),
                          Qt::AlignLeft | Qt::AlignBottom, m_detailText);
    }
    painter->restore();
}

} // namespace tbh::app
