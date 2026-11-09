// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/HarnessModel.h"
#include "schematic/SchematicItems.h"

#include <QGraphicsScene>
#include <QHash>

namespace tbh::app {

class SelectionModel;

// Renders the harness model as schematic items and keeps them in sync with model
// signals. Holds no data of its own; editing happens in SchematicView via commands.
class SchematicScene : public QGraphicsScene
{
    Q_OBJECT

public:
    static constexpr qreal kGrid = 10;

    SchematicScene(HarnessModel *model, SelectionModel *selection, QObject *parent = nullptr);
    ~SchematicScene() override;

    HarnessModel *model() const { return m_model; }

    bool snapToGrid() const { return m_snap; }
    void setSnapToGrid(bool on) { m_snap = on; }
    static QPointF snapped(const QPointF &point);

    ConnectorItem *connectorItem(const QString &id) const { return m_connectors.value(id); }
    SpliceItem *spliceItem(const QString &id) const { return m_splices.value(id); }
    WireItem *wireItem(const QString &id) const { return m_wires.value(id); }
    FreeEndItem *freeEndItem(const QString &key) const { return m_freeEnds.value(key); }
    PlacedItem *placedItem(const QString &key) const;

    // Anchor of a wire end (scene coords), facing the wire's other end.
    Anchor endAnchor(const Wire &wire, bool endB) const;
    // Reference point of a wire end used to pick anchor sides.
    QPointF endReference(const WireEnd &end, const QString &wireId, bool endB) const;

    // Highlights pin rows whose label or circuit text contains `text`.
    void setSearchText(const QString &text);
    // (connector id, row) of every match, in model order.
    std::vector<std::pair<QString, int>> searchMatches() const;

    // Free spot to the right of everything placed so far (scene coords).
    QPointF nextFreePosition() const;

    // Called by PlacedItem when it moves.
    void itemMoved(PlacedItem *item);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void rebuild();
    void onAdded(EntityKind kind, const QString &id);
    void onChanged(EntityKind kind, const QString &id);
    void onRemoved(EntityKind kind, const QString &id);
    void onPlacementChanged(const QString &key);
    void syncSelectionToModel();
    void syncSelectionFromModel();

    void addConnector(const Connector &connector);
    void addSplice(const Splice &splice);
    void addWire(const Wire &wire);
    void syncFreeEnds(const Wire &wire);
    void applyPlacement(PlacedItem *item);
    void indexWire(const Wire &wire);
    void unindexWire(const QString &wireId);
    void updateWire(const QString &wireId);
    void updateWiresOf(const QString &key);
    static QStringList endKeys(const Wire &wire);

    HarnessModel *m_model;
    SelectionModel *m_selection;
    bool m_snap = true;
    bool m_building = false;
    bool m_syncingSelection = false;
    QString m_search;

    QHash<QString, ConnectorItem *> m_connectors;
    QHash<QString, SpliceItem *> m_splices;
    QHash<QString, WireItem *> m_wires;
    QHash<QString, FreeEndItem *> m_freeEnds;     // by placement key
    QMultiHash<QString, QString> m_wiresByItem;   // connector/splice id or free-end key -> wire id
};

} // namespace tbh::app
