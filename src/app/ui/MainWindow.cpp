// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainWindow.h"

#include "Document.h"
#include "io/FileFormat.h"
#include "ui/HarnessPropertiesPanel.h"
#include "ui/PreferencesDialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeWidget>

namespace tbh::app {

namespace {

constexpr auto kRecentFilesKey = "recentFiles";
constexpr int kMaxRecentFiles = 10;

QString fileFilter()
{
    return QCoreApplication::translate("tbh::app::MainWindow", "Harness files (*.%1)")
        .arg(io::kFileExtension);
}

QWidget *placeholder(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setEnabled(false);
    return label;
}

} // namespace

MainWindow::MainWindow(Theme *theme, QWidget *parent)
    : QMainWindow(parent)
    , m_theme(theme)
    , m_document(new Document(this))
{
    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(placeholder(tr("Schematic editor (milestone 2)")), tr("Schematic"));
    tabs->addTab(placeholder(tr("Layout editor (milestone 3)")), tr("Layout"));
    tabs->addTab(placeholder(tr("Bill of materials (milestone 5)")), tr("BOM"));
    setCentralWidget(tabs);

    createDocks();
    createMenus();

    connect(m_document, &Document::filePathChanged, this, &MainWindow::updateTitle);
    connect(m_document, &Document::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_document->model(), &HarnessModel::harnessReset, this, &MainWindow::refreshProjectTree);
    connect(m_document->model(), &HarnessModel::infoChanged, this, &MainWindow::refreshProjectTree);

    refreshProjectTree();
    updateTitle();
    statusBar()->showMessage(tr("Ready"));
    resize(1280, 800);
}

void MainWindow::createDocks()
{
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    m_projectTree = new QTreeWidget(this);
    m_projectTree->setHeaderHidden(true);
    auto *projectDock = new QDockWidget(tr("Project"), this);
    projectDock->setObjectName(QStringLiteral("projectDock"));
    projectDock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    auto *propertiesDock = new QDockWidget(tr("Properties"), this);
    propertiesDock->setObjectName(QStringLiteral("propertiesDock"));
    propertiesDock->setWidget(new HarnessPropertiesPanel(m_document, propertiesDock));
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    auto *problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(new QListWidget(problemsDock));
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newFile);
    file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openWithDialog);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = m_document->undoStack()->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_document->undoStack()->createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    QAction *preferences = edit->addAction(tr("&Preferences..."), QKeySequence::Preferences, this,
                                           [this] { PreferencesDialog(this).exec(); });
    preferences->setMenuRole(QAction::PreferencesRole);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    for (QDockWidget *dock : findChildren<QDockWidget *>())
        m_viewMenu->addAction(dock->toggleViewAction());
    m_viewMenu->addSeparator();
    QMenu *themeMenu = m_viewMenu->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    const std::pair<Theme::Mode, QString> modes[] = {{Theme::Mode::System, tr("&System")},
                                                     {Theme::Mode::Light, tr("&Light")},
                                                     {Theme::Mode::Dark, tr("&Dark")}};
    for (const auto &[mode, label] : modes) {
        QAction *action = themeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_theme->mode() == mode);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { m_theme->setMode(mode); });
    }

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, &MainWindow::showAbout);
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    if (!m_document->open(path, &error)) {
        QMessageBox::critical(this, tr("Open failed"), error);
        return false;
    }
    addRecentFile(path);
    statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

void MainWindow::newFile()
{
    if (maybeSave())
        m_document->newHarness();
}

void MainWindow::openWithDialog()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open harness"), {}, fileFilter());
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::save()
{
    if (m_document->filePath().isEmpty())
        return saveAs();
    return saveTo(m_document->filePath());
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save harness"), m_document->filePath(),
                                                fileFilter());
    if (path.isEmpty())
        return false;
    const QString suffix = QLatin1Char('.') + io::kFileExtension;
    if (!path.endsWith(suffix, Qt::CaseInsensitive))
        path += suffix;
    return saveTo(path);
}

bool MainWindow::saveTo(const QString &path)
{
    QString error;
    if (!m_document->saveAs(path, &error)) {
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    addRecentFile(path);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_document->isModified())
        return true;
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved changes"), tr("The harness has unsaved changes. Save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::addRecentFile(const QString &path)
{
    QSettings settings;
    QStringList files = settings.value(kRecentFilesKey).toStringList();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    files.removeAll(absolute);
    files.prepend(absolute);
    while (files.size() > kMaxRecentFiles)
        files.removeLast();
    settings.setValue(kRecentFilesKey, files);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = QSettings().value(kRecentFilesKey).toStringList();
    for (const QString &path : files) {
        m_recentMenu->addAction(QDir::toNativeSeparators(path), this, [this, path] {
            if (maybeSave())
                openFile(path);
        });
    }
    if (!files.isEmpty()) {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(tr("&Clear list"), this, [this] {
            QSettings().remove(kRecentFilesKey);
            rebuildRecentMenu();
        });
    }
    m_recentMenu->setEnabled(!files.isEmpty());
}

void MainWindow::refreshProjectTree()
{
    const Harness &h = m_document->model()->harness();
    m_projectTree->clear();
    auto *root = new QTreeWidgetItem(m_projectTree, {h.info.name.isEmpty() ? tr("(unnamed)") : h.info.name});

    auto *connectors = new QTreeWidgetItem(root, {tr("Connectors (%1)").arg(h.connectors.size())});
    for (const Connector &c : h.connectors)
        new QTreeWidgetItem(connectors, {tr("%1 - %n pin(s)", nullptr, int(c.pins.size())).arg(c.label)});
    new QTreeWidgetItem(root, {tr("Wires (%1)").arg(h.wires.size())});
    new QTreeWidgetItem(root, {tr("Splices (%1)").arg(h.splices.size())});
    new QTreeWidgetItem(root, {tr("Layout: %1 node(s), %2 segment(s)")
                                   .arg(h.layoutNodes.size())
                                   .arg(h.layoutSegments.size())});
    m_projectTree->expandAll();
}

void MainWindow::updateTitle()
{
    const QString path = m_document->filePath();
    const QString name = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
    setWindowFilePath(path);
    setWindowTitle(tr("%1[*] - %2").arg(name, QApplication::applicationDisplayName()));
    setWindowModified(m_document->isModified());
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About %1").arg(QApplication::applicationDisplayName()),
        tr("<h3>%1 %2</h3>"
           "<p>Free, open-source wiring harness design software.</p>"
           "<p>This application makes no network connections. Your designs never leave "
           "this computer.</p>"
           "<p>Licensed under the GNU General Public License v3 or later.</p>")
            .arg(QApplication::applicationDisplayName(), QApplication::applicationVersion()));
}

} // namespace tbh::app
