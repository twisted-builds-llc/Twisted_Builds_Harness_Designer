// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/SetHarnessInfoCommand.h"

#include <QCoreApplication>

namespace tbh::app {

SetHarnessInfoCommand::SetHarnessInfoCommand(HarnessModel *model, HarnessInfo newInfo,
                                             QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("tbh::app::SetHarnessInfoCommand",
                                               "Edit harness properties"),
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

} // namespace tbh::app
