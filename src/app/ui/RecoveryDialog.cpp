// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/RecoveryDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace tbh::app {

RecoveryDialog::RecoveryDialog(QWidget *parent)
    : QDialog(parent)
    , m_list(new QListWidget(this))
{
    setWindowTitle(tr("Recover Unsaved Work"));
    resize(560, 320);
    auto *intro = new QLabel(tr("The application did not close normally last time. These harnesses had "
                                "changes that were not saved. Open one to recover it, then save it."),
                             this);
    intro->setWordWrap(true);
    m_list->setAccessibleName(tr("Unsaved harnesses"));

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *open = buttons->addButton(tr("Open"), QDialogButtonBox::AcceptRole);
    QPushButton *remove = buttons->addButton(tr("Delete"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    open->setDefault(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(open, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        m_chosen = m_entries[size_t(row)];
        accept();
    });
    connect(m_list, &QListWidget::itemActivated, open, &QPushButton::click);
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        if (QMessageBox::question(this, tr("Delete Unsaved Work"),
                                  tr("Delete these unsaved changes permanently?"))
            != QMessageBox::Yes)
            return;
        Autosave::removeRecovery(m_entries[size_t(row)]);
        reload();
    });
    reload();
}

void RecoveryDialog::reload()
{
    m_entries = Autosave::orphanedRecoveries();
    m_list->clear();
    for (const RecoveryInfo &info : m_entries) {
        const QString name = info.harnessName.isEmpty() ? tr("(unnamed harness)") : info.harnessName;
        const QString where = info.originalPath.isEmpty() ? tr("never saved")
                                                          : QDir::toNativeSeparators(info.originalPath);
        const QString when = QLocale().toString(info.savedAt.toLocalTime(), QLocale::ShortFormat);
        m_list->addItem(tr("%1 — %2 — autosaved %3").arg(name, where, when));
    }
    if (!m_entries.empty())
        m_list->setCurrentRow(0);
}

} // namespace tbh::app
