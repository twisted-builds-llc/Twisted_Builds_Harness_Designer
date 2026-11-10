// SPDX-License-Identifier: GPL-3.0-or-later
#include "schematic/SheetTabBar.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "model/Queries.h"
#include "schematic/SchematicScene.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QTabBar>
#include <QToolButton>
#include <QUndoStack>

namespace tbh::app {

SheetTabBar::SheetTabBar(Document *document, SchematicScene *scene, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_scene(scene)
    , m_tabs(new QTabBar(this))
    , m_add(new QToolButton(this))
{
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabs->setAccessibleName(tr("Schematic sheets"));
    m_add->setText(tr("+"));
    m_add->setAutoRaise(true);
    m_add->setToolTip(tr("Add a schematic sheet"));
    m_add->setAccessibleName(tr("Add sheet"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(2);
    layout->addWidget(m_tabs);
    layout->addWidget(m_add);
    layout->addStretch();

    HarnessModel *model = document->model();
    connect(model, &HarnessModel::harnessReset, this, &SheetTabBar::refresh);
    connect(model, &HarnessModel::sheetsChanged, this, &SheetTabBar::refresh);
    connect(scene, &SchematicScene::currentSheetChanged, this, &SheetTabBar::refresh);
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_refreshing && index >= 0)
            m_scene->setCurrentSheet(sheetIdAt(index));
    });
    connect(m_tabs, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index >= 0)
            renameSheet(index);
    });
    connect(m_tabs, &QTabBar::customContextMenuRequested, this, &SheetTabBar::showMenu);
    connect(m_add, &QToolButton::clicked, this, &SheetTabBar::addSheet);
    refresh();
}

QString SheetTabBar::sheetIdAt(int index) const
{
    const auto &sheets = m_document->model()->harness().schematic.sheets;
    return index >= 0 && index < int(sheets.size()) ? sheets[size_t(index)].id : QString();
}

void SheetTabBar::refresh()
{
    m_refreshing = true;
    const Schematic &schematic = m_document->model()->harness().schematic;
    const int count = int(schematic.sheets.size());
    while (m_tabs->count() > count)
        m_tabs->removeTab(m_tabs->count() - 1);
    for (int i = 0; i < count; ++i) {
        const QString name = sheetDisplayName(schematic, i);
        if (i < m_tabs->count())
            m_tabs->setTabText(i, name);
        else
            m_tabs->addTab(name);
        m_tabs->setTabToolTip(i, tr("Double-click to rename; right-click for more"));
    }
    m_tabs->setCurrentIndex(schematic.sheetIndex(m_scene->currentSheet()));
    m_refreshing = false;
}

void SheetTabBar::addSheet()
{
    QString id;
    m_document->undoStack()->push(tbh::app::addSheet(m_document->model(), &id).release());
    m_scene->setCurrentSheet(id);
}

void SheetTabBar::renameSheet(int index)
{
    const QString id = sheetIdAt(index);
    if (id.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Sheet"), tr("Sheet name:"), QLineEdit::Normal,
                                               m_document->model()->harness().schematic.sheets[size_t(index)].name,
                                               &ok);
    if (!ok)
        return;
    if (auto command = tbh::app::renameSheet(m_document->model(), id, name))
        m_document->undoStack()->push(command.release());
}

void SheetTabBar::deleteSheet(int index)
{
    if (auto command = tbh::app::removeSheet(m_document->model(), sheetIdAt(index)))
        m_document->undoStack()->push(command.release());
}

void SheetTabBar::moveSheet(int index, int toIndex)
{
    const QString id = sheetIdAt(index);
    if (auto command = tbh::app::moveSheet(m_document->model(), id, toIndex))
        m_document->undoStack()->push(command.release());
}

void SheetTabBar::showMenu(const QPoint &pos)
{
    const int index = m_tabs->tabAt(pos);
    if (index < 0)
        return;
    const int count = m_tabs->count();
    QMenu menu(this);
    menu.addAction(tr("&Rename..."), this, [this, index] { renameSheet(index); });
    menu.addAction(tr("Move &Left"), this, [this, index] { moveSheet(index, index - 1); })->setEnabled(index > 0);
    menu.addAction(tr("Move R&ight"), this, [this, index] { moveSheet(index, index + 1); })
        ->setEnabled(index + 1 < count);
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("&Delete Sheet"), this, [this, index] { deleteSheet(index); });
    remove->setEnabled(count > 1);
    remove->setToolTip(tr("Its items move to the sheet before it"));
    menu.setToolTipsVisible(true);
    menu.addSeparator();
    menu.addAction(tr("&Add Sheet"), this, &SheetTabBar::addSheet);
    menu.exec(m_tabs->mapToGlobal(pos));
}

} // namespace tbh::app
