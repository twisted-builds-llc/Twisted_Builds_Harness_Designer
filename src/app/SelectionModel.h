// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/HarnessModel.h"

#include <QObject>
#include <QSet>

namespace tbh::app {

struct Selection {
    QSet<QString> connectors;
    QSet<QString> pins; // "connectorId/pinId": narrows a selected connector to these pins
    QSet<QString> splices;
    QSet<QString> wires; // selecting a wire's free end selects the wire
    QSet<QString> layoutNodes;
    QSet<QString> layoutSegments;

    bool isEmpty() const
    {
        return connectors.isEmpty() && splices.isEmpty() && wires.isEmpty() && layoutNodes.isEmpty()
               && layoutSegments.isEmpty();
    }
    QSet<QString> &ids(EntityKind kind)
    {
        switch (kind) {
        case EntityKind::Connector:
            return connectors;
        case EntityKind::Splice:
            return splices;
        case EntityKind::Wire:
            return wires;
        case EntityKind::LayoutNode:
            return layoutNodes;
        case EntityKind::LayoutSegment:
            return layoutSegments;
        }
        Q_UNREACHABLE_RETURN(wires);
    }
    bool operator==(const Selection &) const = default;
};

// Selection shared by all views and panels of one document. Drops ids of removed
// items and clears on new/open.
class SelectionModel : public QObject
{
    Q_OBJECT

public:
    explicit SelectionModel(HarnessModel *model, QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(model, &HarnessModel::harnessReset, this, [this] { setSelection({}); });
        connect(model, &HarnessModel::entityRemoved, this, [this](EntityKind kind, const QString &id) {
            if (!m_selection.ids(kind).contains(id))
                return;
            Selection s = m_selection;
            s.ids(kind).remove(id);
            setSelection(s);
        });
    }

    const Selection &selection() const { return m_selection; }
    void setSelection(const Selection &selection)
    {
        if (m_selection == selection)
            return;
        m_selection = selection;
        emit selectionChanged();
    }

signals:
    void selectionChanged();

private:
    Selection m_selection;
};

} // namespace tbh::app
