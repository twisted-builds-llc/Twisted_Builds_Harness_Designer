// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/NoteItem.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QPainter>
#include <QPalette>

namespace tbh::app {

namespace {

QFont noteFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(12);
    return font;
}

} // namespace

NoteItem::NoteItem(const Note &note, std::function<QPointF(const QPointF &)> snap)
    : m_snap(std::move(snap))
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(3);
    setAcceptHoverEvents(true);
    setNote(note);
}

void NoteItem::setNote(const Note &note)
{
    prepareGeometryChange();
    m_note = note;
    m_textRect = QFontMetricsF(noteFont()).boundingRect(QRectF(0, 0, kWidth, 1e6), Qt::TextWordWrap, note.text);
    m_textRect.setLeft(0);
    m_textRect.setWidth(std::max(m_textRect.width(), 20.0));
    if (pos() != note.position)
        setPos(note.position);
    update();
}

QRectF NoteItem::boundingRect() const
{
    return m_textRect.adjusted(-4, -3, 4, 3);
}

void NoteItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = scene() ? scene()->palette() : QApplication::palette();
    const QColor text = pal.color(QPalette::Text);
    painter->setRenderHint(QPainter::Antialiasing);
    if (isSelected() || m_hovered) {
        QColor border = pal.color(QPalette::Highlight);
        if (!isSelected()) {
            border = text;
            border.setAlphaF(0.4f);
        }
        painter->setPen(QPen(border, isSelected() ? 1.5 : 1, isSelected() ? Qt::SolidLine : Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(boundingRect().adjusted(1, 1, -1, -1), 3, 3);
    }
    painter->setFont(noteFont());
    painter->setPen(text);
    painter->drawText(m_textRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_note.text);
}

QVariant NoteItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionChange && m_snap)
        return m_snap(value.toPointF());
    return QGraphicsItem::itemChange(change, value);
}

void NoteItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = true;
    update();
    QGraphicsItem::hoverEnterEvent(event);
}

void NoteItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = false;
    update();
    QGraphicsItem::hoverLeaveEvent(event);
}

} // namespace tbh::app
