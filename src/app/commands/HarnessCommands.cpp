// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/HarnessCommands.h"

#include <QCoreApplication>

namespace tbh::app {

SetHarnessInfoCommand::SetHarnessInfoCommand(HarnessModel *model, HarnessInfo newInfo,
                                             QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("tbh::app::HarnessCommands", "Edit harness properties"),
                   parent)
    , m_model(model)
    , m_old(model->harness().info)
    , m_new(std::move(newInfo))
{
}

void SetHarnessInfoCommand::redo()
{
    m_model->setInfo(m_new);
}

void SetHarnessInfoCommand::undo()
{
    m_model->setInfo(m_old);
}

SetHarnessSettingsCommand::SetHarnessSettingsCommand(HarnessModel *model, Settings newSettings,
                                                     QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("tbh::app::HarnessCommands", "Edit harness settings"),
                   parent)
    , m_model(model)
    , m_old(model->harness().settings)
    , m_new(std::move(newSettings))
{
}

void SetHarnessSettingsCommand::redo()
{
    m_model->setSettings(m_new);
}

void SetHarnessSettingsCommand::undo()
{
    m_model->setSettings(m_old);
}

} // namespace tbh::app
