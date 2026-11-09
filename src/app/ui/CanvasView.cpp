// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/CanvasView.h"

#include <QGraphicsItem>
#include <QKeyEvent>
#include <QPainter>
#include <QVarLengthArray>
#include <QLineEdit>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

namespace tbh::app {

namespace {
constexpr qreal kMinScale = 0.05;
constexpr qreal kMaxScale = 8.0;
} // namespace

QPointF snapToCanvasGrid(const QPointF &point)
{
    return QPointF(std::round(point.x() / kCanvasGrid) * kCanvasGrid,
                   std::round(point.y() / kCanvasGrid) * kCanvasGrid);
}

void drawCanvasBackground(QPainter *painter, const QRectF &rect, const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Base);
    painter->fillRect(rect, base.darker(base.lightness() > 128 ? 103 : 115));
    const qreal scale = painter->worldTransform().m11();
    constexpr qreal minor = 20;
    constexpr qreal major = 100;
    QColor line = palette.color(QPalette::Text);
    auto drawGrid = [&](qreal step, qreal alpha) {
        line.setAlphaF(float(alpha));
        painter->setPen(QPen(line, 0));
        const qreal left = std::floor(rect.left() / step) * step;
        const qreal top = std::floor(rect.top() / step) * step;
        QVarLengthArray<QLineF, 256> lines;
        for (qreal x = left; x < rect.right(); x += step)
            lines.append(QLineF(x, rect.top(), x, rect.bottom()));
        for (qreal y = top; y < rect.bottom(); y += step)
            lines.append(QLineF(rect.left(), y, rect.right(), y));
        painter->drawLines(lines.constData(), int(lines.size()));
    };
    if (scale * minor >= 8)
        drawGrid(minor, 0.05);
    if (scale * major >= 8)
        drawGrid(major, 0.09);
}

// One-line editor laid over the canvas. Enter/Tab commit and move on, Esc cancels,
// losing focus commits.
class InlineEdit : public QLineEdit
{
public:
    using Commit = std::function<void(const QString &, int nav)>;

    InlineEdit(QWidget *parent, Commit commit)
        : QLineEdit(parent)
        , m_commit(std::move(commit))
    {
    }

    void finish(int nav)
    {
        if (m_done)
            return;
        m_done = true;
        if (nav >= 0)
            m_commit(text(), nav);
        hide();
        deleteLater();
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            finish(event->modifiers() & Qt::ShiftModifier ? 2 : 1); // Up : Down
            return;
        case Qt::Key_Tab:
            finish(3);
            return;
        case Qt::Key_Backtab:
            finish(4);
            return;
        case Qt::Key_Escape:
            finish(-1);
            return;
        default:
            QLineEdit::keyPressEvent(event);
        }
    }

    bool event(QEvent *event) override
    {
        // Keep Tab for navigation instead of focus change.
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
                keyPressEvent(key);
                return true;
            }
        }
        return QLineEdit::event(event);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QLineEdit::focusOutEvent(event);
        finish(0);
    }

private:
    Commit m_commit;
    bool m_done = false;
};

CanvasView::CanvasView(QWidget *parent)
    : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag);
    setFocusPolicy(Qt::StrongFocus);
}

void CanvasView::zoomIn()
{
    if (transform().m11() * 1.25 <= kMaxScale)
        scale(1.25, 1.25);
}

void CanvasView::zoomOut()
{
    if (transform().m11() / 1.25 >= kMinScale)
        scale(1 / 1.25, 1 / 1.25);
}

void CanvasView::zoomToFit()
{
    const QRectF bounds = scene() ? scene()->itemsBoundingRect() : QRectF();
    if (bounds.isEmpty()) {
        resetTransform();
        centerOn(0, 0);
        return;
    }
    fitInView(bounds.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    const qreal scale = std::clamp(transform().m11(), kMinScale, 1.5);
    setTransform(QTransform::fromScale(scale, scale));
    centerOn(bounds.center());
}

void CanvasView::editText(const QRectF &sceneRect, const QString &text,
                          std::function<void(const QString &, Nav)> commit)
{
    closeEditor();
    m_editor = new InlineEdit(viewport(), [commit](const QString &value, int nav) {
        commit(value, Nav(nav));
    });
    QFont font = m_editor->font();
    font.setPixelSize(std::clamp(int(12 * transform().m11()), 9, 28));
    m_editor->setFont(font);
    m_editor->setText(text);
    m_editor->selectAll();
    const QRect rect = mapFromScene(sceneRect).boundingRect();
    m_editor->setGeometry(rect.adjusted(0, 0, 0, std::max(0, m_editor->sizeHint().height() - rect.height())));
    m_editor->show();
    m_editor->setFocus();
    InlineEdit *editor = m_editor;
    connect(m_editor, &QObject::destroyed, this, [this, editor] {
        if (m_editor == editor)
            m_editor = nullptr;
    });
}

void CanvasView::closeEditor()
{
    if (m_editor)
        m_editor->finish(0);
    m_editor = nullptr;
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    closeEditor();
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panLast = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_panLast;
        m_panLast = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        viewport()->unsetCursor();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    closeEditor();
    if (wheelOverItem(event)) {
        event->accept();
        return;
    }
    const qreal factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    const qreal scale = transform().m11() * factor;
    if (scale < kMinScale || scale > kMaxScale)
        return;
    this->scale(factor, factor);
    event->accept();
}

void CanvasView::selectAdjacentItem(bool next)
{
    if (!scene())
        return;
    std::vector<QGraphicsItem *> items;
    for (QGraphicsItem *item : scene()->items()) {
        if ((item->flags() & QGraphicsItem::ItemIsSelectable) && item->isVisible())
            items.push_back(item);
    }
    if (items.empty())
        return;
    // Reading order: top to bottom, then left to right (rows of half a grid square).
    std::sort(items.begin(), items.end(), [](QGraphicsItem *a, QGraphicsItem *b) {
        const QPointF pa = a->sceneBoundingRect().topLeft();
        const QPointF pb = b->sceneBoundingRect().topLeft();
        const int rowA = int(std::floor(pa.y() / 20));
        const int rowB = int(std::floor(pb.y() / 20));
        return rowA != rowB ? rowA < rowB : pa.x() < pb.x();
    });
    const QList<QGraphicsItem *> selected = scene()->selectedItems();
    int index = next ? 0 : int(items.size()) - 1;
    if (!selected.isEmpty()) {
        const auto it = std::find(items.begin(), items.end(), selected.first());
        if (it != items.end()) {
            const int current = int(it - items.begin());
            index = (current + (next ? 1 : -1) + int(items.size())) % int(items.size());
        }
    }
    scene()->clearSelection();
    items[size_t(index)]->setSelected(true);
    ensureVisible(items[size_t(index)], 60, 60);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_BracketRight:
    case Qt::Key_BracketLeft:
        selectAdjacentItem(event->key() == Qt::Key_BracketRight);
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const qreal step = kCanvasGrid * ((event->modifiers() & Qt::ShiftModifier) ? 10 : 1);
        QPointF delta;
        if (event->key() == Qt::Key_Left)
            delta.setX(-step);
        else if (event->key() == Qt::Key_Right)
            delta.setX(step);
        else if (event->key() == Qt::Key_Up)
            delta.setY(-step);
        else
            delta.setY(step);
        if (nudgeSelection(delta))
            return;
        break;
    }
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::changeEvent(QEvent *event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        viewport()->update();
}

void CanvasView::scrollContentsBy(int dx, int dy)
{
    closeEditor();
    QGraphicsView::scrollContentsBy(dx, dy);
}

} // namespace tbh::app
