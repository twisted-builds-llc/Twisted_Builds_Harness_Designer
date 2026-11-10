// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "calc/Highlight.h"
#include "layout/LayoutItems.h"
#include "ui/NoteItem.h"
#include "model/HarnessModel.h"

#include <QGraphicsScene>
#include <QHash>

namespace tbh::app {

class SelectionModel;

// Renders the layout graph (nodes and segments) and keeps it in sync with the model.
// Segments draw their routed wires as colored stripes and caption the entered length.
class LayoutScene : public QGraphicsScene
{
    Q_OBJECT

public:
    LayoutScene(HarnessModel *model, SelectionModel *selection, QObject *parent = nullptr);
    ~LayoutScene() override;

    HarnessModel *model() const { return m_model; }
    bool snapToGrid() const { return m_snap; }
    void setSnapToGrid(bool on) { m_snap = on; }

    LayoutNodeItem *nodeItem(const QString &id) const { return m_nodes.value(id); }
    LayoutSegmentItem *segmentItem(const QString &id) const { return m_segments.value(id); }
    NoteItem *noteItem(const QString &id) const { return m_notes.value(id); }
    // Draws `segment` (e.g. while its bend is dragged) without changing the model.
    void previewSegment(const LayoutSegment &segment);

    void setHighlight(const std::optional<calc::HighlightSet> &set);
    void setShowBundleDiameters(bool on);

    // Called by LayoutNodeItem when it moves.
    void nodeMoved(LayoutNodeItem *item);

    // Recomputes labels and wire counts now; model changes normally do it on the next
    // event loop pass. Used before rendering a scene that is not shown (PDF export).
    void refreshCaptions();

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void rebuild();
    void onAdded(EntityKind kind, const QString &id);
    void onChanged(EntityKind kind, const QString &id);
    void onRemoved(EntityKind kind, const QString &id);
    void addNode(const LayoutNode &node);
    void addSegment(const LayoutSegment &segment);
    void addNote(const Note &note);
    void updateSegmentEnds(LayoutSegmentItem *item);
    // Tells a node which way its segments leave (branch point drawing).
    void updateLegs(LayoutNodeItem *node);
    // Labels and wire counts depend on the whole model; recompute once per event loop pass.
    void scheduleRefresh();
    void syncSelectionToModel();
    void syncSelectionFromModel();

    HarnessModel *m_model;
    SelectionModel *m_selection;
    bool m_snap = true;
    bool m_syncingSelection = false;
    bool m_refreshPending = false;
    bool m_showDiameters = false;
    std::optional<calc::HighlightSet> m_highlight;
    void applyHighlight();
    QHash<QString, LayoutNodeItem *> m_nodes;
    QHash<QString, LayoutSegmentItem *> m_segments;
    QHash<QString, NoteItem *> m_notes;
};

} // namespace tbh::app
