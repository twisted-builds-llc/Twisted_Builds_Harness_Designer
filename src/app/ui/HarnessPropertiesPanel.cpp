// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/HarnessPropertiesPanel.h"

#include "Document.h"
#include "commands/SetHarnessInfoCommand.h"
#include "ui/UnitsCombo.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>

namespace tbh::app {

HarnessPropertiesPanel::HarnessPropertiesPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_name(new QLineEdit(this))
    , m_revision(new QLineEdit(this))
    , m_author(new QLineEdit(this))
    , m_units(new QComboBox(this))
    , m_description(new QPlainTextEdit(this))
{
    fillUnitsCombo(m_units);
    m_description->setTabChangesFocus(true);
    m_description->installEventFilter(this);

    auto *form = new QFormLayout(this);
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("&Revision:"), m_revision);
    form->addRow(tr("&Author:"), m_author);
    form->addRow(tr("&Units:"), m_units);
    form->addRow(tr("&Description:"), m_description);

    for (QLineEdit *edit : {m_name, m_revision, m_author})
        connect(edit, &QLineEdit::editingFinished, this, &HarnessPropertiesPanel::commit);
    connect(m_units, &QComboBox::activated, this, &HarnessPropertiesPanel::commit);

    HarnessModel *model = m_document->model();
    connect(model, &HarnessModel::harnessReset, this, &HarnessPropertiesPanel::refresh);
    connect(model, &HarnessModel::infoChanged, this, &HarnessPropertiesPanel::refresh);
    refresh();
}

bool HarnessPropertiesPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_description && event->type() == QEvent::FocusOut)
        commit();
    return QWidget::eventFilter(watched, event);
}

void HarnessPropertiesPanel::refresh()
{
    const HarnessInfo &info = m_document->model()->harness().info;
    const QSignalBlocker b1(m_name), b2(m_revision), b3(m_author), b4(m_units), b5(m_description);
    m_name->setText(info.name);
    m_revision->setText(info.revision);
    m_author->setText(info.author);
    setComboUnits(m_units, info.units);
    if (m_description->toPlainText() != info.description)
        m_description->setPlainText(info.description);
}

void HarnessPropertiesPanel::commit()
{
    HarnessModel *model = m_document->model();
    HarnessInfo info = model->harness().info;
    info.name = m_name->text();
    info.revision = m_revision->text();
    info.author = m_author->text();
    info.units = comboUnits(m_units);
    info.description = m_description->toPlainText();
    if (info != model->harness().info)
        m_document->undoStack()->push(new SetHarnessInfoCommand(model, std::move(info)));
}

} // namespace tbh::app
