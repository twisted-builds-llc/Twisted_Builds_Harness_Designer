// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ProblemsPanel.h"

#include "Document.h"

#include <QStyle>
#include <QTimer>

namespace tbh::app {

ProblemsPanel::ProblemsPanel(Document *document, QWidget *parent)
    : QListWidget(parent)
    , m_document(document)
{
    setWordWrap(true);
    HarnessModel *model = document->model();
    connect(model, &HarnessModel::harnessReset, this, &ProblemsPanel::scheduleRefresh);
    connect(model, &HarnessModel::entityAdded, this, &ProblemsPanel::scheduleRefresh);
    connect(model, &HarnessModel::entityChanged, this, &ProblemsPanel::scheduleRefresh);
    connect(model, &HarnessModel::entityRemoved, this, &ProblemsPanel::scheduleRefresh);
    connect(model, &HarnessModel::settingsChanged, this, &ProblemsPanel::scheduleRefresh);
    connect(model, &HarnessModel::libraryChanged, this, &ProblemsPanel::scheduleRefresh);
    auto activate = [this](QListWidgetItem *item) {
        emit problemActivated(calc::Problem::Kind(item->data(Qt::UserRole).toInt()),
                              item->data(Qt::UserRole + 1).toString());
    };
    connect(this, &QListWidget::itemClicked, this, activate);
    connect(this, &QListWidget::itemActivated, this, activate);
    refresh();
}

void ProblemsPanel::scheduleRefresh()
{
    if (m_pending)
        return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        refresh();
    });
}

void ProblemsPanel::refresh()
{
    clear();
    const auto problems = calc::findProblems(m_document->model()->harness(), m_document->model()->library());
    const QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    for (const calc::Problem &p : problems) {
        auto *item = new QListWidgetItem(icon, p.message, this);
        item->setData(Qt::UserRole, int(p.kind));
        item->setData(Qt::UserRole + 1, p.itemId);
    }
    emit countChanged(int(problems.size()));
}

} // namespace tbh::app
