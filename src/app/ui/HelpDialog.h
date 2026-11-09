// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QAction;
class QTabWidget;

namespace tbh::app {

// Help > Getting Started / Keyboard Shortcuts / Mouse. The shortcut list is built from
// the application's actions, so it always matches the real key bindings.
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Page { GettingStarted, Shortcuts, Mouse };

    struct ShortcutEntry
    {
        QString action;
        QString keys;
        QString where; // "Anywhere", "Schematic", "Layout"
    };

    HelpDialog(const std::vector<ShortcutEntry> &shortcuts, Page page, QWidget *parent = nullptr);

    // Collects actions with shortcuts. `editors` maps an editor widget to its name; actions
    // added to an editor only work there.
    static std::vector<ShortcutEntry> collectShortcuts(QWidget *window,
                                                       const std::vector<std::pair<QWidget *, QString>> &editors);

private:
    QTabWidget *m_tabs;
};

} // namespace tbh::app
