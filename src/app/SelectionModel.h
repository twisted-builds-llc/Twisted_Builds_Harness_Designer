// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QSet>

namespace tbh::app {

struct Selection {
    QSet<QString> connectors;
    QSet<QString> splices;
    QSet<QString> wires; // selecting a wire's free end selects the wire

    bool isEmpty() const { return connectors.isEmpty() && splices.isEmpty() && wires.isEmpty(); }
    bool operator==(const Selection &) const = default;
};

// Selection shared by all views and panels of one document.
class SelectionModel : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

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
