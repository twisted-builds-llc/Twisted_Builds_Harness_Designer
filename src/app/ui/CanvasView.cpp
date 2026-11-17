// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/CanvasView.h"

#include "Preferences.h"

#include <QApplication>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QPainter>
#include <QVarLengthArray>
#include <QLineEdit>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QStyleHints>
#include <QTimer>
#include <QTouchEvent>

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
    // Touchscreens: Qt's own mouse emulation would hide a second finger, so the view takes
    // touches itself (see handleTouch()).
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    for (QScrollBar *bar : {horizontalScrollBar(), verticalScrollBar()})
        connect(bar, &QScrollBar::sliderPressed, this, [this] { m_fitPending = false; });
}

void CanvasView::zoomIn()
{
    zoomBy(1.25);
}

void CanvasView::zoomOut()
{
    zoomBy(1 / 1.25);
}

void CanvasView::zoomBy(qreal factor)
{
    m_fitPending = false;
    const qreal scale = transform().m11() * factor;
    if (scale >= kMinScale && scale <= kMaxScale)
        this->scale(factor, factor);
}

void CanvasView::zoomToFit()
{
    // Visible items only: e.g. other schematic sheets' items are hidden.
    QRectF bounds;
    if (scene()) {
        for (const QGraphicsItem *item : scene()->items()) {
            if (item->isVisible())
                bounds |= item->sceneBoundingRect();
        }
    }
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

void CanvasView::fitWhenShown()
{
    m_fitPending = true;
    if (isVisible())
        fitLater();
}

void CanvasView::fitLater()
{
    // After pending layout, so the viewport has its final size.
    QTimer::singleShot(0, this, [this] {
        if (m_fitPending && isVisible())
            zoomToFit();
    });
}

bool CanvasView::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
        m_fitPending = false; // the user is working in the view
    return QGraphicsView::event(event);
}

bool CanvasView::viewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        m_fitPending = false; // the user is working in the view
        break;
    case QEvent::NativeGesture: {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1 + gesture->value());
            return true;
        }
        break;
    }
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        return handleTouch(static_cast<QTouchEvent *>(event));
    case QEvent::ContextMenu:
        // With right-drag panning the menu opens on a right-click release without movement
        // (see panMouseEvent()), not when the platform sends it.
        if (static_cast<QContextMenuEvent *>(event)->reason() == QContextMenuEvent::Mouse
            && preferences::panInput() == preferences::PanInput::RightDrag)
            return true;
        break;
    default:
        break;
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
        if (panMouseEvent(static_cast<QMouseEvent *>(event)))
            return true;
        break;
    default:
        break;
    }
    return QGraphicsView::viewportEvent(event);
}

bool CanvasView::panMouseEvent(QMouseEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        if (m_panButton != Qt::NoButton)
            return true; // other buttons do nothing during a pan
        const preferences::PanInput input = preferences::panInput();
        const Qt::MouseButton button = event->button();
        const bool pans = button == Qt::MiddleButton
                          || (button == Qt::RightButton && input == preferences::PanInput::RightDrag)
                          || (button == Qt::LeftButton && m_spaceHeld);
        if (!pans)
            return false;
        closeEditor();
        m_panButton = button;
        m_panPress = m_panLast = event->pos();
        // A right-click without movement still opens the context menu, so the right button
        // pans only once it has moved.
        m_panMoved = button != Qt::RightButton;
        if (m_panMoved)
            setPanCursor(Qt::ClosedHandCursor);
        return true;
    }
    case QEvent::MouseMove: {
        if (m_panButton == Qt::NoButton)
            return m_spaceHeld; // keep the open-hand cursor (no hover feedback) while Space is held
        if (!m_panMoved) {
            if ((event->pos() - m_panPress).manhattanLength() < QApplication::startDragDistance())
                return true;
            m_panMoved = true;
            setPanCursor(Qt::ClosedHandCursor);
        }
        const QPoint delta = event->pos() - m_panLast;
        m_panLast = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (m_panButton == Qt::NoButton)
            return false;
        if (event->button() != m_panButton)
            return true;
        const bool click = !m_panMoved;
        m_panButton = Qt::NoButton;
        if (m_spaceHeld)
            setPanCursor(Qt::OpenHandCursor);
        else
            restoreCursor();
        if (click) {
            QContextMenuEvent menu(QContextMenuEvent::Mouse, event->pos(),
                                   event->globalPosition().toPoint(), event->modifiers());
            contextMenuEvent(&menu);
        }
        return true;
    }
    default:
        return false;
    }
}

bool CanvasView::handleTouch(QTouchEvent *event)
{
    // Touchpads (macOS sends their raw touches too) keep their wheel and pinch events.
    if (event->device()->type() != QInputDevice::DeviceType::TouchScreen) {
        event->ignore();
        return false;
    }
    if (event->type() == QEvent::TouchBegin) {
        const QPointF pos = event->points().constFirst().position();
        // Touches on the inline editor (a child of the viewport) reach it as Qt emulates them.
        if (viewport()->childAt(pos.toPoint())) {
            event->ignore();
            return false;
        }
        m_fitPending = false;
        m_touchMode = TouchMode::Pending;
        m_touchId = event->points().constFirst().id();
        m_touchStart = m_touchLast = pos;
        m_pinchIds[0] = m_pinchIds[1] = -1;
    }
    event->accept();
    if (m_touchMode == TouchMode::None)
        return true;

    if (event->type() == QEvent::TouchCancel) {
        if (m_touchMode == TouchMode::Mouse)
            sendTouchMouse(QEvent::MouseButtonRelease, m_touchLast);
        m_touchMode = TouchMode::None;
        return true;
    }

    std::vector<const QEventPoint *> down;
    for (const QEventPoint &point : event->points()) {
        if (point.state() != QEventPoint::State::Released)
            down.push_back(&point);
    }
    if (m_touchMode == TouchMode::Pending && down.size() >= 2)
        m_touchMode = TouchMode::Pinch; // a click or drag already under way stays one

    switch (m_touchMode) {
    case TouchMode::Pinch:
        if (down.size() >= 2) {
            const QPointF a = down[0]->position();
            const QPointF b = down[1]->position();
            const qreal distance = std::max(QLineF(a, b).length(), qreal(1));
            const QPointF mid = (a + b) / 2;
            if (down[0]->id() == m_pinchIds[0] && down[1]->id() == m_pinchIds[1])
                pinchTo(m_pinchMid, mid, distance / m_pinchDistance);
            // New pair of fingers: start measuring from here.
            m_pinchIds[0] = down[0]->id();
            m_pinchIds[1] = down[1]->id();
            m_pinchDistance = distance;
            m_pinchMid = mid;
        } else {
            m_pinchIds[0] = m_pinchIds[1] = -1;
        }
        break;
    case TouchMode::Pending:
    case TouchMode::Mouse:
        for (const QEventPoint &point : event->points()) {
            if (point.id() != m_touchId)
                continue;
            m_touchLast = point.position();
            if (m_touchMode == TouchMode::Pending
                && (m_touchLast - m_touchStart).manhattanLength() >= QApplication::startDragDistance()) {
                m_touchMode = TouchMode::Mouse;
                sendTouchMouse(QEvent::MouseButtonPress, m_touchStart);
            }
            if (m_touchMode == TouchMode::Mouse && point.state() == QEventPoint::State::Updated)
                sendTouchMouse(QEvent::MouseMove, m_touchLast);
        }
        break;
    case TouchMode::None:
        break;
    }

    if (event->type() == QEvent::TouchEnd) {
        if (m_touchMode == TouchMode::Pending) {
            // A tap: a click, or a double click when it follows a tap close by.
            const QStyleHints *hints = QGuiApplication::styleHints();
            const bool doubleTap = m_lastTap.isValid()
                                   && m_lastTap.elapsed() < hints->mouseDoubleClickInterval()
                                   && (m_touchStart - m_lastTapPos).manhattanLength()
                                          <= hints->touchDoubleTapDistance();
            sendTouchMouse(doubleTap ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress,
                           m_touchStart);
            sendTouchMouse(QEvent::MouseButtonRelease, m_touchStart);
            if (doubleTap) {
                m_lastTap.invalidate();
            } else {
                m_lastTap.start();
                m_lastTapPos = m_touchStart;
            }
        } else if (m_touchMode == TouchMode::Mouse) {
            sendTouchMouse(QEvent::MouseButtonRelease, m_touchLast);
        }
        m_touchMode = TouchMode::None;
    }
    return true;
}

void CanvasView::sendTouchMouse(QEvent::Type type, QPointF pos)
{
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent mouse(type, pos, viewport()->mapToGlobal(pos), button, buttons,
                      QGuiApplication::keyboardModifiers());
    QCoreApplication::sendEvent(viewport(), &mouse);
}

void CanvasView::pinchTo(QPointF from, QPointF to, qreal factor)
{
    const QPointF scenePoint = viewportTransform().inverted().map(from);
    const qreal scale = std::clamp(transform().m11() * factor, kMinScale, kMaxScale);
    const ViewportAnchor anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setTransform(QTransform::fromScale(scale, scale));
    setTransformationAnchor(anchor);
    // Scroll so the content follows the fingers.
    const QPointF offset = viewportTransform().map(scenePoint) - to;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(offset.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(offset.y()));
}

void CanvasView::setPanCursor(Qt::CursorShape shape)
{
    if (!m_cursorSaved) {
        m_savedCursor = viewport()->cursor();
        m_cursorSaved = true;
    }
    viewport()->setCursor(shape);
}

void CanvasView::restoreCursor()
{
    if (m_cursorSaved) {
        viewport()->setCursor(m_savedCursor);
        m_cursorSaved = false;
    }
}

void CanvasView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    if (m_fitPending)
        fitLater();
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitPending)
        fitLater();
}

void CanvasView::editText(const QRectF &sceneRect, const QString &text,
                          std::function<void(const QString &, Nav)> commit, const QStringList &suggestions)
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
    if (!suggestions.isEmpty()) {
        auto *completer = new QCompleter(suggestions, editor);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setMaxVisibleItems(12);
        editor->setCompleter(completer);
        if (text.isEmpty()) {
            QTimer::singleShot(0, editor, [completer] {
                completer->setCompletionPrefix({});
                completer->complete();
            });
        }
    }
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

void CanvasView::wheelEvent(QWheelEvent *event)
{
    closeEditor();
    if (wheelOverItem(event)) {
        event->accept();
        return;
    }
    if (preferences::wheelPans() && !(event->modifiers() & Qt::ControlModifier)) {
        // Touchpads report pixels; a mouse wheel notch (120) scrolls 60 pixels.
        QPoint delta = event->pixelDelta();
        if (delta.isNull())
            delta = event->angleDelta() / 2;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    } else {
        zoomBy(std::pow(1.15, event->angleDelta().y() / 120.0));
    }
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
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier
        && preferences::panInput() == preferences::PanInput::SpaceDrag) {
        if (!event->isAutoRepeat()) {
            m_spaceHeld = true;
            if (m_panButton == Qt::NoButton)
                setPanCursor(Qt::OpenHandCursor);
        }
        return;
    }
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

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && m_spaceHeld) {
        if (!event->isAutoRepeat()) {
            m_spaceHeld = false;
            if (m_panButton == Qt::NoButton)
                restoreCursor();
        }
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent *event)
{
    // The Space release may go elsewhere (e.g. to the inline editor or another window).
    if (m_spaceHeld) {
        m_spaceHeld = false;
        if (m_panButton == Qt::NoButton)
            restoreCursor();
    }
    QGraphicsView::focusOutEvent(event);
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
