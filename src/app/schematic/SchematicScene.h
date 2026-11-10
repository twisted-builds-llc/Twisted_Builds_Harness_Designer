// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "calc/Highlight.h"
#include "model/HarnessModel.h"
#include "schematic/SchematicItems.h"
#include "ui/NoteItem.h"

#include <QGraphicsScene>
#include <QHash>
#include <QSet>

namespace tbh::app {

class SelectionModel;
struct Selection;

// Renders the harness model as schematic items and keeps them in sync with model
// signals. Holds no data of its own; editing happens in SchematicView via commands.
class SchematicScene : public QGraphicsScene
{
    Q_OBJECT

public:
    SchematicScene(HarnessModel *model, SelectionModel *selection, QObject *parent = nullptr);
    ~SchematicScene() override;

    HarnessModel *model() const { return m_model; }

    bool snapToGrid() const { return m_snap; }
    void setSnapToGrid(bool on) { m_snap = on; }
    static QPointF snapped(const QPointF &point);

    ConnectorItem *connectorItem(const QString &id) const { return m_connectors.value(id); }
    SpliceItem *spliceItem(const QString &id) const { return m_splices.value(id); }
    WireItem *wireItem(const QString &id) const { return m_wires.value(id); }
    FreeEndItem *freeEndItem(const QString &id) const { return m_freeEnds.value(id); }
    NoteItem *noteItem(const QString &id) const { return m_notes.value(id); }
    PlacedItem *placedItem(const QString &key) const;

    // The sheet shown. Items on other sheets are hidden; a wire with one end here and the
    // other on another sheet ends in an off-sheet tag. Selecting items that are all on
    // another sheet (e.g. from the layout) switches to their sheet.
    QString currentSheet() const { return m_sheet; }
    void setCurrentSheet(const QString &sheetId);
    // Home sheet of the item with this placement key (connector, splice, free end).
    QString sheetOf(const QString &key) const;
    // True if the item is shown on the current sheet (items can be on several).
    bool isOnCurrentSheet(const QString &key) const;
    // True if this end of the wire is drawn on the current sheet (wire or off-sheet tag).
    bool isEndOnCurrentSheet(const Wire &wire, bool endB) const;

    // Anchor of a wire end (scene coords), facing the wire's other end (or facing right
    // when the other end is on another sheet).
    Anchor endAnchor(const Wire &wire, bool endB) const;
    // Reference point of a wire end used to pick anchor sides.
    QPointF endReference(const WireEnd &end) const;

    // Highlights pin rows whose label or circuit text contains `text`.
    void setSearchText(const QString &text);
    // (connector id, row) of every match, in model order.
    std::vector<std::pair<QString, int>> searchMatches() const;

    // Dims everything outside `set`; nullopt turns highlighting off.
    void setHighlight(const std::optional<calc::HighlightSet> &set);

    // Free spot to the right of everything placed so far on `sheetId` (scene coords).
    QPointF nextFreePosition(const QString &sheetId) const;

    // Called by PlacedItem when it moves.
    void itemMoved(PlacedItem *item);
    // Re-routes a wire from the model (anchors and stored route).
    void updateWire(const QString &wireId);

signals:
    void currentSheetChanged(const QString &sheetId);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void rebuild();
    void onAdded(EntityKind kind, const QString &id);
    void onChanged(EntityKind kind, const QString &id);
    void onRemoved(EntityKind kind, const QString &id);
    void onPlacementChanged(const QString &key);
    void onSheetsChanged();
    // Shows the items of the current sheet and re-routes every wire.
    void applySheet();
    void applyNoteSheet(NoteItem *item);
    void syncSelectionToModel();
    void syncSelectionFromModel();
    void followSelectionSheet(const Selection &selection);

    void addConnector(const Connector &connector);
    void addSplice(const Splice &splice);
    void addFreeEnd(const FreeEnd &end);
    void addWire(const Wire &wire);
    void addNote(const Note &note); // schematic notes only
    void applyPlacement(PlacedItem *item);
    void indexWire(const Wire &wire);
    void unindexWire(const QString &wireId);
    void updateWiresOf(const QString &key);
    static QStringList endKeys(const Wire &wire);

    HarnessModel *m_model;
    SelectionModel *m_selection;
    bool m_snap = true;
    bool m_building = false;
    bool m_syncingSelection = false;
    QString m_search;
    QString m_sheet;
    std::optional<calc::HighlightSet> m_highlight;
    void applyHighlight();

    QHash<QString, ConnectorItem *> m_connectors;
    QHash<QString, SpliceItem *> m_splices;
    QHash<QString, WireItem *> m_wires;
    QHash<QString, FreeEndItem *> m_freeEnds;
    QHash<QString, NoteItem *> m_notes;
    QSet<QString> m_autoPlaced; // items without a placement, laid out by nextFreePosition()
    QMultiHash<QString, QString> m_wiresByItem;   // connector/splice/free end id -> wire id
};

} // namespace tbh::app
