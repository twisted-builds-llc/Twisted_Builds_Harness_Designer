// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Graphics items of the schematic editor. Items hold a copy of the model data they draw;
// SchematicScene keeps them in sync. Items never modify the model.

#include "model/Harness.h"

#include <QGraphicsItem>
#include <QPainterPath>
#include <QSet>

#include <optional>

namespace tbh::app {

enum ItemType {
    ConnectorItemType = QGraphicsItem::UserType + 1,
    SpliceItemType,
    FreeEndItemType,
    WireItemType,
};

// Anchor where a wire meets an item: scene point plus the direction the wire leaves in
// (+1 right, -1 left, 0 any).
struct Anchor
{
    QPointF point;
    int direction = 0;
};

// Base for items with a schematic placement (movable, snap to grid, notify the scene).
class PlacedItem : public QGraphicsItem
{
public:
    explicit PlacedItem(QString key);

    const QString &placementKey() const { return m_key; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString m_key;
};

// A connector box. Pin rows are painted directly (no child item per pin) and only the
// rows inside the exposed rect are drawn, so 999-pin connectors stay cheap.
class ConnectorItem : public PlacedItem
{
public:
    static constexpr qreal kWidth = 300;
    static constexpr qreal kHeaderHeight = 46;
    static constexpr qreal kRowHeight = 20;
    static constexpr qreal kLabelColumn = 64;
    // Length of the lead drawn on both sides of each pin row. Dragging it draws a wire;
    // wires attach at its outer tip.
    static constexpr qreal kStubLength = 14;

    enum class Part { None, Title, Collapse, Minus, Count, Plus, PinLabel, PinCircuit, PinStub };
    struct Hit
    {
        Part part = Part::None;
        int row = -1;
    };

    explicit ConnectorItem(const Connector &connector);

    int type() const override { return ConnectorItemType; }
    const Connector &connector() const { return m_connector; }
    QString id() const { return m_connector.id; }

    void setConnector(const Connector &connector);
    void setDisplay(bool collapsed, int maxVisibleRows);
    bool isCollapsed() const { return m_collapsed; }
    int maxVisibleRows() const { return m_maxVisibleRows; }

    int rowCount() const { return int(m_connector.pins.size()); }
    bool isScrollMode() const;
    int visibleRowCount() const;
    int firstVisibleRow() const { return m_firstRow; }
    // Scrolls by `rows` (scroll mode only). Returns true if the view changed.
    bool scrollRows(int rows);
    // Scrolls so the row is visible (scroll mode only).
    void ensureRowVisible(int row);
    int rowOfPin(const QString &pinId) const;

    // Item coordinates.
    QRectF bodyRect() const; // the box, without pin leads
    QRectF rowRect(int row) const; // null if hidden
    QRectF labelCellRect(int row) const;
    QRectF circuitCellRect(int row) const;
    QRectF titleRect() const;
    QRectF countRect() const;
    Hit hitTest(const QPointF &itemPos) const;

    // Where a wire on `row` attaches, on the side facing `towards` (scene coords).
    Anchor pinAnchor(int row, const QPointF &towards) const;

    void setHoverRow(int row);
    // Highlight mode: rows of these pin ids are emphasized, other rows faded.
    // `active` false clears it.
    void setRelatedPins(bool active, const QSet<QString> &pinIds);
    void setSearchText(const QString &text);
    std::vector<int> searchMatches() const;

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QRectF minusRect() const;
    QRectF plusRect() const;
    QRectF collapseRect() const;
    bool rowMatches(int row) const;

    Connector m_connector;
    bool m_collapsed = false;
    int m_maxVisibleRows = 0;
    int m_firstRow = 0;
    int m_hoverRow = -1;
    bool m_relatedActive = false;
    QSet<QString> m_relatedPins;
    QString m_search;
};

class SpliceItem : public PlacedItem
{
public:
    static constexpr qreal kRadius = 6;
    static constexpr qreal kStubLength = 14; // drag handle on each side, see isStubAt()

    explicit SpliceItem(const Splice &splice);

    int type() const override { return SpliceItemType; }
    QString id() const { return m_splice.id; }
    void setSplice(const Splice &splice);
    void setHighlighted(bool on);
    Anchor anchor() const { return {scenePos(), 0}; }
    // True if `itemPos` is on one of the short leads left and right of the dot, which
    // start a wire when dragged (the dot itself moves the splice).
    bool isStubAt(const QPointF &itemPos) const;

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    static QRectF stubRect(bool right);

    Splice m_splice;
    bool m_highlighted = false;
};

// A free end (flying lead, eyelet, ring terminal, bare end): a small symbol of its kind
// with a lead on each side. Wires attach at the tip of the lead facing them; dragging a
// lead draws a wire, dragging the symbol moves it.
class FreeEndItem : public PlacedItem
{
public:
    static constexpr qreal kHalfWidth = 12;  // symbol
    static constexpr qreal kStubLength = 14; // lead on each side, see isStubAt()

    explicit FreeEndItem(const FreeEnd &end);

    int type() const override { return FreeEndItemType; }
    QString id() const { return m_end.id; }
    void setFreeEnd(const FreeEnd &end);
    void setHighlighted(bool on);
    // Tip of the lead on the side facing `towards` (scene coords).
    Anchor anchor(const QPointF &towards) const;
    bool isStubAt(const QPointF &itemPos) const;

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    static QRectF stubRect(bool right);

    FreeEnd m_end;
    bool m_highlighted = false;
};

// A wire drawn as an orthogonal route between two anchors: out of each end, then along one
// vertical run. The run sits halfway between the ends unless the user dragged it
// (the wire's schematic placement). Striped wires get a dashed stripe-color pen over the
// base-color pen.
class WireItem : public QGraphicsItem
{
public:
    static constexpr qreal kPenWidth = 3;
    static constexpr qreal kCornerRadius = 12; // wire bends are rounded, not sharp

    explicit WireItem(const Wire &wire);

    int type() const override { return WireItemType; }
    const Wire &wire() const { return m_wire; }
    QString id() const { return m_wire.id; }
    void setWire(const Wire &wire);
    // `runX`: scene x of the vertical run; empty = halfway between the ends.
    void setRoute(const Anchor &a, const Anchor &b, std::optional<qreal> runX = {});
    // Moves the vertical run, keeping the anchors (live preview while dragging).
    void setRunX(qreal x);
    // Scene x of the vertical run as drawn.
    qreal runX() const;

    // Orthogonal route between anchors with rounded bends; also used for the rubber-band
    // wire while drawing.
    static QPainterPath routePath(const Anchor &a, const Anchor &b, std::optional<qreal> runX = {});
    // Polyline through `points` with each bend rounded off by up to `radius` (less where a
    // leg is shorter than twice the radius).
    static QPainterPath roundedPath(const std::vector<QPointF> &points, qreal radius);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    Wire m_wire;
    Anchor m_a;
    Anchor m_b;
    std::optional<qreal> m_runX;
    QPainterPath m_path;
    QPainterPath m_shape;
};

} // namespace tbh::app
