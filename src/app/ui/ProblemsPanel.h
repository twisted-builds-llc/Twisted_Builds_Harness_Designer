// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "calc/WireLength.h"

#include <QListWidget>

namespace tbh::app {

class Document;

// Lists calc::findProblems() for the open harness, refreshed after every change.
class ProblemsPanel : public QListWidget
{
    Q_OBJECT

public:
    explicit ProblemsPanel(Document *document, QWidget *parent = nullptr);

signals:
    void countChanged(int count);
    void problemActivated(tbh::calc::Problem::Kind kind, const QString &itemId);

private:
    void scheduleRefresh();
    void refresh();

    Document *m_document;
    bool m_pending = false;
};

} // namespace tbh::app
