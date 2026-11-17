// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUndoCommand>
#include <QUndoStack>

#include <memory>

namespace tbh::app {

// Pushes commands as one undo step. The macro opens only with the first command, because
// QUndoStack keeps empty macros; it ends when the batch goes out of scope.
class UndoBatch
{
public:
    UndoBatch(QUndoStack *stack, QString text)
        : m_stack(stack)
        , m_text(std::move(text))
    {
    }
    ~UndoBatch()
    {
        if (m_open)
            m_stack->endMacro();
    }
    UndoBatch(const UndoBatch &) = delete;
    UndoBatch &operator=(const UndoBatch &) = delete;

    // Pushes `command` (null is ignored). Returns whether it was pushed.
    bool push(std::unique_ptr<QUndoCommand> command)
    {
        if (!command)
            return false;
        if (!m_open) {
            m_stack->beginMacro(m_text);
            m_open = true;
        }
        m_stack->push(command.release());
        return true;
    }
    bool pushedAny() const { return m_open; }

private:
    QUndoStack *m_stack;
    QString m_text;
    bool m_open = false;
};

} // namespace tbh::app
