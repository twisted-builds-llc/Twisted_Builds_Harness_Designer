// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QGraphicsItem>

#include <functional>

namespace tbh::app {

inline constexpr int NoteItemType = QGraphicsItem::UserType + 40;

// A note: free text on a canvas (schematic or layout), wrapped to a fixed width. Movable
// and selectable; the owning scene keeps it in sync with the model. A note with a
// frameSize is an outline: a dashed box with the text as its title, grabbed by its edge
// (clicks inside reach the items within) and resized from its bottom-right handle.
class NoteItem : public QGraphicsItem
{
public:
    // `snap` adjusts a proposed position while dragging (grid snapping), or is empty.
    NoteItem(const Note &note, std::function<QPointF(const QPointF &)> snap);

    int type() const override { return NoteItemType; }
    QString id() const { return m_note.id; }
    const Note &note() const { return m_note; }
    void setNote(const Note &note);
    static constexpr qreal kWidth = 180;
    static constexpr qreal kHandleSize = 10;  // outline resize handle
    static constexpr QSizeF kMinFrameSize{60, 40};

    bool isFrame() const { return m_note.frameSize.has_value(); }
    // Outline only: its box in scene coordinates.
    QRectF frameSceneRect() const;
    // Outline only: shows a new size while the handle is dragged (the model is not changed).
    void previewFrameSize(const QSizeF &size);
    // Outline only: true if `itemPos` is on the resize handle (shown when selected).
    bool isResizeHandleAt(const QPointF &itemPos) const;

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    Note m_note;
    std::function<QPointF(const QPointF &)> m_snap;
    QRectF m_textRect;
    bool m_hovered = false;
};

} // namespace tbh::app
