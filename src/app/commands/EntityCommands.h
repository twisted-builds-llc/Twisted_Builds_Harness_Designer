// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Generic undo commands for Connector, Wire and Splice entities and schematic placements.

#include "model/HarnessModel.h"

#include <QUndoCommand>

#include <map>
#include <optional>

namespace tbh::app {

using Placements = std::map<QString, SchematicPlacement>;

template<typename T>
class AddEntityCommand : public QUndoCommand
{
public:
    AddEntityCommand(HarnessModel *model, T item, Placements placements, const QString &text,
                     QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent)
        , m_model(model)
        , m_item(std::move(item))
        , m_placements(std::move(placements))
    {
    }

    void redo() override
    {
        m_model->insert(m_item);
        for (const auto &[key, placement] : m_placements)
            m_model->setPlacement(key, placement);
    }

    void undo() override { m_model->remove<T>(m_item.id); }

private:
    HarnessModel *m_model;
    T m_item;
    Placements m_placements;
};

template<typename T>
class RemoveEntityCommand : public QUndoCommand
{
public:
    RemoveEntityCommand(HarnessModel *model, const QString &id, const QString &text,
                        QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent)
        , m_model(model)
        , m_item(*model->find<T>(id))
        , m_index(model->indexOf<T>(id))
    {
        for (const QString &key : HarnessModel::placementKeys(entityKindOf<T>(), id)) {
            if (const SchematicPlacement *placement = model->placement(key))
                m_placements[key] = *placement;
        }
    }

    void redo() override { m_model->remove<T>(m_item.id); }

    void undo() override
    {
        m_model->insert(m_item, m_index);
        for (const auto &[key, placement] : m_placements)
            m_model->setPlacement(key, placement);
    }

private:
    HarnessModel *m_model;
    T m_item;
    qsizetype m_index;
    Placements m_placements;
};

template<typename T>
class UpdateEntityCommand : public QUndoCommand
{
public:
    UpdateEntityCommand(HarnessModel *model, T newItem, const QString &text,
                        QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent)
        , m_model(model)
        , m_old(*model->find<T>(newItem.id))
        , m_new(std::move(newItem))
    {
    }

    void redo() override { m_model->update(m_new); }
    void undo() override { m_model->update(m_old); }

private:
    HarnessModel *m_model;
    T m_old;
    T m_new;
};

// Sets (or, with nullopt, removes) several placements at once, e.g. after a drag.
class SetPlacementsCommand : public QUndoCommand
{
public:
    SetPlacementsCommand(HarnessModel *model,
                         const std::map<QString, std::optional<SchematicPlacement>> &newPlacements,
                         const QString &text, QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent)
        , m_model(model)
    {
        for (const auto &[key, placement] : newPlacements) {
            const SchematicPlacement *old = model->placement(key);
            m_changes[key] = {old ? std::optional(*old) : std::nullopt, placement};
        }
    }

    void redo() override
    {
        for (const auto &[key, change] : m_changes)
            m_model->setPlacement(key, change.second);
    }

    void undo() override
    {
        for (const auto &[key, change] : m_changes)
            m_model->setPlacement(key, change.first);
    }

private:
    using Change = std::pair<std::optional<SchematicPlacement>, std::optional<SchematicPlacement>>;
    HarnessModel *m_model;
    std::map<QString, Change> m_changes;
};

} // namespace tbh::app
