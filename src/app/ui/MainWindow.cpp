// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainWindow.h"

#include "Document.h"
#include "io/FileFormat.h"
#include "SelectionModel.h"
#include "model/Queries.h"
#include "schematic/SchematicView.h"
#include "ui/PreferencesDialog.h"
#include "ui/properties/ConnectorPanel.h"
#include "ui/properties/PropertiesPanel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
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
    , m_selection(new SelectionModel(this))
{
    m_schematic = new SchematicView(m_document, m_selection, this);
    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(m_schematic, tr("Schematic"));
    tabs->addTab(placeholder(tr("Layout editor (coming in a later version)")), tr("Layout"));
    tabs->addTab(placeholder(tr("Bill of materials (coming in a later version)")), tr("BOM"));
    setCentralWidget(tabs);

    createDocks();
    createMenus();

    connect(m_document, &Document::filePathChanged, this, &MainWindow::updateTitle);
    connect(m_document, &Document::modifiedChanged, this, &MainWindow::updateTitle);
    HarnessModel *model = m_document->model();
    connect(model, &HarnessModel::harnessReset, this, &MainWindow::refreshProjectTree);
    connect(model, &HarnessModel::infoChanged, this, &MainWindow::refreshProjectTree);
    connect(model, &HarnessModel::entityAdded, this, &MainWindow::refreshProjectTree);
    connect(model, &HarnessModel::entityRemoved, this, &MainWindow::refreshProjectTree);
    connect(model, &HarnessModel::entityChanged, this, [this](EntityKind kind) {
        if (kind != EntityKind::Wire)
            refreshProjectTree();
    });
    connect(m_schematic, &SchematicView::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 5000); });
    connect(m_selection, &SelectionModel::selectionChanged, this, &MainWindow::showSelectionStatus);

    refreshProjectTree();
    updateTitle();
    statusBar()->showMessage(tr("Ready"));
    resize(1400, 860);
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

    auto *properties = new PropertiesPanel(m_document, m_selection, this);
    connect(properties->connectorPanel(), &ConnectorPanel::displayChangeRequested, m_schematic,
            &SchematicView::setConnectorDisplay);
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(properties);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *propertiesDock = new QDockWidget(tr("Properties"), this);
    propertiesDock->setObjectName(QStringLiteral("propertiesDock"));
    propertiesDock->setWidget(scroll);
    propertiesDock->setMinimumWidth(360);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    auto *problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(new QListWidget(problemsDock));
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
    problemsDock->hide(); // nothing reports problems yet
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

    SchematicView *view = m_schematic;
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = m_document->undoStack()->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_document->undoStack()->createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    // Canvas actions only fire while the schematic has focus, so they never steal
    // keys (Delete, Ctrl+C...) from text fields in the panels.
    auto canvasAction = [&](QMenu *menu, const QString &text, const QKeySequence &key, auto slot) {
        QAction *action = menu->addAction(text);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(action);
        connect(action, &QAction::triggered, view, slot);
        return action;
    };
    canvasAction(edit, tr("&Copy"), QKeySequence::Copy, &SchematicView::copySelection);
    canvasAction(edit, tr("&Paste"), QKeySequence::Paste, &SchematicView::paste);
    canvasAction(edit, tr("D&uplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), &SchematicView::duplicateSelection);
    QAction *del = canvasAction(edit, tr("&Delete"), QKeySequence::Delete, &SchematicView::deleteSelection);
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    canvasAction(edit, tr("Re&name"), QKeySequence(Qt::Key_F2), &SchematicView::renameSelection);
    canvasAction(edit, tr("Select &All"), QKeySequence::SelectAll, &SchematicView::selectAll);
    edit->addSeparator();
    edit->addAction(tr("&Find Pin..."), QKeySequence::Find, this, [this] {
        m_find->setFocus();
        m_find->selectAll();
    });
    edit->addSeparator();
    QAction *preferences = edit->addAction(tr("&Preferences..."), QKeySequence::Preferences, this,
                                           [this] { PreferencesDialog(this).exec(); });
    preferences->setMenuRole(QAction::PreferencesRole);

    QMenu *place = menuBar()->addMenu(tr("&Place"));
    QAction *addConnector =
        canvasAction(place, tr("Add &Connector"), QKeySequence(Qt::Key_C), &SchematicView::addConnector);
    QAction *addSplice = canvasAction(place, tr("Add &Splice"), QKeySequence(Qt::Key_S), &SchematicView::addSplice);
    place->addSeparator();
    auto *toolGroup = new QActionGroup(this);
    QAction *selectTool = place->addAction(tr("S&elect Tool"));
    QAction *wireTool = place->addAction(tr("&Wire Tool"));
    selectTool->setShortcut(QKeySequence(Qt::Key_V));
    wireTool->setShortcut(QKeySequence(Qt::Key_W));
    for (QAction *tool : {selectTool, wireTool}) {
        tool->setCheckable(true);
        tool->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        toolGroup->addAction(tool);
        view->addAction(tool);
    }
    selectTool->setChecked(true);
    selectTool->setToolTip(tr("Select and move items; drag from a pin to draw a wire (V)"));
    wireTool->setToolTip(tr("Draw wires from pins or splices (W)"));
    connect(selectTool, &QAction::triggered, view, [view] { view->setTool(SchematicView::Tool::Select); });
    connect(wireTool, &QAction::triggered, view, [view] { view->setTool(SchematicView::Tool::Wire); });

    QMenu *arrange = menuBar()->addMenu(tr("&Arrange"));
    const std::pair<QString, Qt::Alignment> alignments[] = {
        {tr("Align &Left"), Qt::AlignLeft},       {tr("Align &Right"), Qt::AlignRight},
        {tr("Align &Top"), Qt::AlignTop},         {tr("Align &Bottom"), Qt::AlignBottom},
        {tr("Align Centers &Horizontally"), Qt::AlignHCenter},
        {tr("Align Centers &Vertically"), Qt::AlignVCenter}};
    for (const auto &[text, alignment] : alignments)
        arrange->addAction(text, view, [view, alignment] { view->align(alignment); });
    arrange->addSeparator();
    arrange->addAction(tr("Distribute Hori&zontally"), view, [view] { view->distribute(Qt::Horizontal); });
    arrange->addAction(tr("Distribute Verti&cally"), view, [view] { view->distribute(Qt::Vertical); });

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, view, &SchematicView::zoomIn);
    m_viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, view, &SchematicView::zoomOut);
    QAction *fit = m_viewMenu->addAction(tr("Zoom to &Fit"), QKeySequence(Qt::CTRL | Qt::Key_0), view,
                                         &SchematicView::zoomToFit);
    QAction *snap = m_viewMenu->addAction(tr("&Snap to Grid"));
    snap->setCheckable(true);
    snap->setChecked(true);
    connect(snap, &QAction::toggled, view, &SchematicView::setSnapToGrid);
    m_viewMenu->addSeparator();
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

    QToolBar *toolbar = addToolBar(tr("Schematic"));
    toolbar->setObjectName(QStringLiteral("schematicToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(selectTool);
    toolbar->addAction(wireTool);
    toolbar->addSeparator();
    toolbar->addAction(addConnector);
    toolbar->addAction(addSplice);
    toolbar->addSeparator();
    toolbar->addAction(fit);
    toolbar->addSeparator();
    m_find = new QLineEdit(toolbar);
    m_find->setPlaceholderText(tr("Find pin or circuit (Enter = next)"));
    m_find->setClearButtonEnabled(true);
    m_find->setMaximumWidth(280);
    toolbar->addWidget(m_find);
    connect(m_find, &QLineEdit::returnPressed, view, [this] { m_schematic->find(m_find->text()); });
    connect(m_find, &QLineEdit::textChanged, view, [this](const QString &text) {
        if (text.isEmpty())
            m_schematic->find({});
    });
}

void MainWindow::showSelectionStatus()
{
    const Selection &s = m_selection->selection();
    const Harness &h = m_document->model()->harness();
    if (s.wires.size() == 1 && s.connectors.isEmpty() && s.splices.isEmpty()) {
        if (const Wire *w = m_document->model()->find<Wire>(*s.wires.begin()))
            statusBar()->showMessage(tr("Wire: %1 — length needs the layout editor").arg(describeWire(h, *w)));
    } else if (!s.isEmpty()) {
        statusBar()->showMessage(tr("%n item(s) selected", nullptr,
                                    int(s.wires.size() + s.connectors.size() + s.splices.size())));
    } else {
        statusBar()->clearMessage();
    }
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
