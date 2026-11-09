// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QObject>

namespace tbh {

enum class EntityKind { Connector, Wire, Splice, LayoutNode, LayoutSegment };

template<typename T>
constexpr EntityKind entityKindOf();
template<>
constexpr EntityKind entityKindOf<Connector>() { return EntityKind::Connector; }
template<>
constexpr EntityKind entityKindOf<Wire>() { return EntityKind::Wire; }
template<>
constexpr EntityKind entityKindOf<Splice>() { return EntityKind::Splice; }
template<>
constexpr EntityKind entityKindOf<LayoutNode>() { return EntityKind::LayoutNode; }
template<>
constexpr EntityKind entityKindOf<LayoutSegment>() { return EntityKind::LayoutSegment; }

// Owns the Harness being edited. Views read from it and react to its signals;
// they never mutate it directly but push QUndoCommands that call the mutators.
// Mutators do not enforce referential integrity: commands keep it (e.g. by
// removing attached wires before a connector).
class HarnessModel : public QObject
{
    Q_OBJECT

public:
    explicit HarnessModel(QObject *parent = nullptr);

    const Harness &harness() const { return m_harness; }

    // Replace the whole harness (new/open). Not undoable.
    void reset(Harness harness);

    // Library used for calculations: the user's library merged with the entries embedded
    // in the harness (see mergeLibraries). Not part of the undo history.
    const Library &library() const { return m_library; }
    const Library &userLibrary() const { return m_userLibrary; }
    void setUserLibrary(Library library);

    void setInfo(const HarnessInfo &info);
    void setSettings(const Settings &settings);

    // T is Connector, Wire, Splice, LayoutNode or LayoutSegment.
    template<typename T>
    const T *find(const QString &id) const;
    template<typename T>
    qsizetype indexOf(const QString &id) const;
    // Inserts at `index` (clamped; -1 = append).
    template<typename T>
    void insert(T item, qsizetype index = -1);
    // Replaces the item with the same id.
    template<typename T>
    void update(T item);
    // Removes the item and its schematic placements (see placementKeys()).
    template<typename T>
    void remove(const QString &id);

    // Schematic placement keys owned by an entity: connector/splice id, wire free-end
    // keys; none for layout entities (their position is part of the entity).
    static std::vector<QString> placementKeys(EntityKind kind, const QString &id);

    const SchematicPlacement *placement(const QString &key) const;
    // nullopt removes the placement.
    void setPlacement(const QString &key, const std::optional<SchematicPlacement> &placement);

signals:
    void harnessReset();
    void infoChanged();
    void settingsChanged();
    void libraryChanged();
    void entityAdded(tbh::EntityKind kind, const QString &id);
    void entityChanged(tbh::EntityKind kind, const QString &id);
    void entityRemoved(tbh::EntityKind kind, const QString &id);
    void placementChanged(const QString &key);

private:
    template<typename T>
    std::vector<T> &items();
    template<typename T>
    const std::vector<T> &items() const;

    Harness m_harness;
    Library m_userLibrary;
    Library m_library;
};

} // namespace tbh
