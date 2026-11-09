// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "calc/Highlight.h"

#include <QObject>

#include <optional>

namespace tbh {
class HarnessModel;
}

namespace tbh::app {

class SelectionModel;

// Highlight mode (plan 8.4): while enabled and something is selected, everything related
// to the selection is highlighted and the rest is dimmed, in every view.
class Highlighter : public QObject
{
    Q_OBJECT

public:
    Highlighter(HarnessModel *model, SelectionModel *selection, QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    // Set while highlighting; nullopt when off or nothing is selected.
    const std::optional<calc::HighlightSet> &current() const { return m_current; }

signals:
    void changed();

private:
    void schedule();
    void recompute();

    HarnessModel *m_model;
    SelectionModel *m_selection;
    bool m_enabled = true;
    bool m_pending = false;
    std::optional<calc::HighlightSet> m_current;
};

} // namespace tbh::app
