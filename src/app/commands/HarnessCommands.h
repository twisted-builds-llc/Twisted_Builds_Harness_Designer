// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/HarnessModel.h"

#include <QUndoCommand>

namespace tbh::app {

// Changes the harness name/description/revision/author/units.
class SetHarnessInfoCommand : public QUndoCommand
{
public:
    SetHarnessInfoCommand(HarnessModel *model, HarnessInfo newInfo, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    HarnessModel *m_model;
    HarnessInfo m_old;
    HarnessInfo m_new;
};

// Changes harness settings (allowances, service loop, rounding, waste factors...).
class SetHarnessSettingsCommand : public QUndoCommand
{
public:
    SetHarnessSettingsCommand(HarnessModel *model, Settings newSettings, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    HarnessModel *m_model;
    Settings m_old;
    Settings m_new;
};

} // namespace tbh::app
