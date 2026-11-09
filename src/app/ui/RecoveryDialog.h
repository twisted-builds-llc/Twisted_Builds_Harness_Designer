// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Autosave.h"

#include <QDialog>

class QListWidget;

namespace tbh::app {

// Lists unsaved work left by sessions that ended unexpectedly. "Open" returns the chosen
// entry (QDialog::Accepted); "Delete" removes entries after confirmation.
class RecoveryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecoveryDialog(QWidget *parent = nullptr);

    bool isEmpty() const { return m_entries.empty(); }
    const RecoveryInfo &chosen() const { return m_chosen; }

private:
    void reload();

    std::vector<RecoveryInfo> m_entries;
    RecoveryInfo m_chosen;
    QListWidget *m_list;
};

} // namespace tbh::app
