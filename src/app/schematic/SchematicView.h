// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"
#include "schematic/SchematicItems.h"
#include "ui/CanvasView.h"

#include <map>

class QGraphicsPathItem;

namespace tbh::app {

class Document;
class SchematicScene;
class SelectionModel;

// Interactive schematic editor: pan/zoom, wire drawing, inline text editing, clipboard,
// align/distribute. Every change goes through an undo command on the document.
//
// Mouse: drag a connector by its header; drag from a pin row to draw a wire (drop on a
// pin, a splice, or empty space for a flying lead); double-click text to edit it;
// wheel zooms (over a scrolling connector it scrolls rows; Ctrl+wheel always zooms);
// middle-drag pans. The Wire tool also starts wires from splices.
class SchematicView : public CanvasView
{
    Q_OBJECT

public:
    enum class Tool { Select, Wire };

    SchematicView(Document *document, SelectionModel *selection, QWidget *parent = nullptr);

    SchematicScene *schematicScene() const { return m_scene; }

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    void setSnapToGrid(bool on);

    void addConnector();
    void addSplice();
    void deleteSelection();
    void renameSelection();
    void copySelection();
    void paste();
    void duplicateSelection();
    void selectAll();
    // Qt::AlignLeft/Right/Top/Bottom/HCenter/VCenter.
    void align(Qt::Alignment alignment);
    void distribute(Qt::Orientation orientation);
    void setConnectorDisplay(const QString &connectorId, bool collapsed, int maxVisibleRows);
    // Highlights matches and moves to the next one. Returns the number of matches.
    int find(const QString &text);

signals:
    void statusMessage(const QString &message);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool wheelOverItem(QWheelEvent *event) override;
    bool nudgeSelection(const QPointF &delta) override;

private:
    struct DropTarget
    {
        std::optional<WireEnd> end;
        ConnectorItem *connector = nullptr;
        int row = -1;
        SpliceItem *splice = nullptr;
    };

    DropTarget targetAt(const QPointF &scenePos) const;
    void setHover(const DropTarget &target);
    void beginWire(const WireEnd &start, const QPoint &pressPos);
    void updateWirePreview(const QPointF &scenePos);
    void finishWire(const QPointF &scenePos);
    void cancelWire();
    Anchor startAnchor(const QPointF &towards) const;

    // Re-attach: end of the single selected wire under `viewPos` (pin or splice end).
    std::optional<bool> selectedWireEndAt(const QPoint &viewPos) const;
    void finishReattach(const QPointF &scenePos);
    // A free end being dragged (single selection), or empty.
    QString draggedFreeEnd() const;
    bool dropFreeEnd(const QPointF &scenePos);

    void rememberPressPositions();
    void commitMoves();
    void toggleCollapsed(ConnectorItem *item);

    void editPinCell(const QString &connectorId, int row, int column);
    void editConnectorTitle(const QString &connectorId);
    void editPinCount(const QString &connectorId);

    QString uniqueLabel(const QString &prefix, bool splice) const;
    QPointF viewCenterSnapped() const;
    QByteArray selectionFragment() const;
    void pasteFragmentBytes(const QByteArray &bytes, QPointF offset);
    std::vector<PlacedItem *> selectedPlacedItems() const;

    Document *m_document;
    SelectionModel *m_selection;
    SchematicScene *m_scene;
    Tool m_tool = Tool::Select;

    // Wire drawing.
    bool m_wirePending = false;
    bool m_wireDrawing = false;
    QPoint m_pressPos;
    WireEnd m_wireStart;
    QGraphicsPathItem *m_preview = nullptr;
    ConnectorItem *m_hoverConnector = nullptr;
    SpliceItem *m_hoverSplice = nullptr;
    // Re-attaching an existing wire end.
    QString m_reattachWire;
    bool m_reattachEndB = false;

    // Moving.
    std::map<QString, QPointF> m_pressPositions;

    int m_pasteCount = 0;
    QString m_searchText;
    int m_searchIndex = -1;
};

} // namespace tbh::app
