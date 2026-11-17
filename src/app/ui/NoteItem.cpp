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

QRectF handleRect(const QSizeF &frame)
{
    return QRectF(frame.width() - NoteItem::kHandleSize / 2, frame.height() - NoteItem::kHandleSize / 2,
                  NoteItem::kHandleSize, NoteItem::kHandleSize);
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
    if (note.frameSize) {
        // The title sits inside the top-left corner, on one line.
        QFont bold = noteFont();
        bold.setBold(true);
        const QFontMetricsF metrics(bold);
        m_textRect = QRectF(6, 4, std::max(0.0, note.frameSize->width() - 12), metrics.height());
        setZValue(-1); // behind what it outlines
    } else {
        m_textRect = QFontMetricsF(noteFont()).boundingRect(QRectF(0, 0, kWidth, 1e6), Qt::TextWordWrap, note.text);
        m_textRect.setLeft(0);
        m_textRect.setWidth(std::max(m_textRect.width(), 20.0));
        setZValue(3);
    }
    if (pos() != note.position)
        setPos(note.position);
    update();
}

QRectF NoteItem::frameSceneRect() const
{
    return isFrame() ? QRectF(scenePos(), *m_note.frameSize) : QRectF();
}

void NoteItem::previewFrameSize(const QSizeF &size)
{
    if (!isFrame())
        return;
    Note preview = m_note;
    preview.frameSize = size.expandedTo(kMinFrameSize);
    setNote(preview);
}

bool NoteItem::isResizeHandleAt(const QPointF &itemPos) const
{
    return isFrame() && isSelected() && handleRect(*m_note.frameSize).adjusted(-3, -3, 3, 3).contains(itemPos);
}

QRectF NoteItem::boundingRect() const
{
    if (isFrame())
        return QRectF(QPointF(0, 0), *m_note.frameSize).adjusted(-kHandleSize, -kHandleSize, kHandleSize, kHandleSize);
    return m_textRect.adjusted(-4, -3, 4, 3);
}

QPainterPath NoteItem::shape() const
{
    QPainterPath path;
    if (!isFrame()) {
        path.addRect(boundingRect());
        return path;
    }
    // The edge (a band 10 wide) and the title; the inside is left to the items in it.
    const QRectF box(QPointF(0, 0), *m_note.frameSize);
    path.addRect(box.adjusted(-5, -5, 5, 5));
    QPainterPath inner;
    inner.addRect(box.adjusted(5, 5, -5, -5));
    path = path.subtracted(inner);
    // United, not added: overlapping parts added to one path would cancel out.
    QPainterPath extras;
    extras.setFillRule(Qt::WindingFill);
    if (!m_note.text.isEmpty())
        extras.addRect(m_textRect.adjusted(-2, -2, 2, 2));
    extras.addRect(handleRect(*m_note.frameSize).adjusted(-3, -3, 3, 3));
    return path.united(extras);
}

void NoteItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QPalette pal = scene() ? scene()->palette() : QApplication::palette();
    const QColor text = pal.color(QPalette::Text);
    painter->setRenderHint(QPainter::Antialiasing);
    if (isFrame()) {
        const QRectF box(QPointF(0, 0), *m_note.frameSize);
        QColor edge = isSelected() ? pal.color(QPalette::Highlight) : text;
        if (!isSelected())
            edge.setAlphaF(m_hovered ? 0.9f : 0.65f);
        QPen pen(edge, isSelected() ? 2 : 1.5, Qt::DashLine);
        pen.setDashPattern({6, 4});
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(box, 6, 6);
        QFont bold = noteFont();
        bold.setBold(true);
        painter->setFont(bold);
        painter->setPen(text);
        painter->drawText(m_textRect, Qt::AlignLeft | Qt::AlignTop,
                          QFontMetricsF(bold).elidedText(m_note.text, Qt::ElideRight, m_textRect.width()));
        if (isSelected()) {
            painter->setPen(QPen(pal.color(QPalette::Highlight), 1.5));
            painter->setBrush(pal.color(QPalette::Base));
            painter->drawRect(handleRect(*m_note.frameSize));
        }
        return;
    }
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
