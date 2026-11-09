// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainWindow.h"

#include "Document.h"
#include "io/FileFormat.h"
#include "Highlighter.h"
#include "LibraryStore.h"
#include "SelectionModel.h"
#include "layout/LayoutScene.h"
#include "schematic/SchematicScene.h"
#include "ui/LibraryDialog.h"
#include "model/Queries.h"
#include "layout/LayoutView.h"
#include "schematic/SchematicView.h"
#include "ui/Format.h"
#include "ui/ProblemsPanel.h"
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
#include <QTimer>
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
    , m_selection(new SelectionModel(m_document->model(), this))
{
    QString libraryWarning;
    m_document->model()->setUserLibrary(libraryStore::loadUserLibrary(&libraryWarning));
    if (!libraryWarning.isEmpty())
        QTimer::singleShot(0, this, [this, libraryWarning] {
            QMessageBox::warning(this, tr("Parts Library"), libraryWarning);
        });

    m_schematic = new SchematicView(m_document, m_selection, this);
    m_layout = new LayoutEditor(m_document, m_selection, this);
    m_highlighter = new Highlighter(m_document->model(), m_selection, this);
    connect(m_highlighter, &Highlighter::changed, this, [this] {
        m_schematic->schematicScene()->setHighlight(m_highlighter->current());
        m_layout->view()->layoutScene()->setHighlight(m_highlighter->current());
    });
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(m_schematic, tr("Schematic"));
    m_tabs->addTab(m_layout, tr("Layout"));
    m_tabs->addTab(placeholder(tr("Bill of materials (coming in a later version)")), tr("BOM"));
    setCentralWidget(m_tabs);

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
    auto showMessage = [this](const QString &message) { statusBar()->showMessage(message, 5000); };
    connect(m_schematic, &SchematicView::statusMessage, this, showMessage);
    connect(m_layout->view(), &LayoutView::statusMessage, this, showMessage);
    connect(m_selection, &SelectionModel::selectionChanged, this, &MainWindow::showSelectionStatus);

    refreshProjectTree();
    updateTitle();
    statusBar()->showMessage(tr("Ready"));
    resize(1400, 860);
}

bool MainWindow::layoutTabActive() const
{
    return m_tabs->currentWidget() == m_layout;
}

CanvasView *MainWindow::currentCanvas() const
{
    if (layoutTabActive())
        return m_layout->view();
    return m_schematic;
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

    auto *problems = new ProblemsPanel(m_document, this);
    auto *problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(problems);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
    resizeDocks({problemsDock}, {120}, Qt::Vertical);
    connect(problems, &ProblemsPanel::countChanged, problemsDock, [problemsDock](int count) {
        problemsDock->setWindowTitle(count ? tr("Problems (%1)").arg(count) : tr("Problems"));
    });
    connect(problems, &ProblemsPanel::problemActivated, this,
            [this](calc::Problem::Kind kind, const QString &id) {
                Selection selection;
                if (kind == calc::Problem::Kind::UnroutedWire) {
                    selection.wires.insert(id);
                    m_tabs->setCurrentWidget(m_schematic);
                } else {
                    selection.layoutSegments.insert(id);
                    m_tabs->setCurrentWidget(m_layout);
                }
                m_selection->setSelection(selection);
            });
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
    LayoutView *layout = m_layout->view();
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = m_document->undoStack()->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_document->undoStack()->createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    // Canvas actions: shortcuts only fire while an editor canvas has focus, so they never
    // steal keys (Delete, Ctrl+C...) from text fields and tables in the panels. From the
    // menu they act on the editor of the current tab; `onLayout` may be empty.
    auto canvasAction = [&](QMenu *menu, const QString &text, const QKeySequence &key,
                            std::function<void()> onSchematic, std::function<void()> onLayout) {
        QAction *action = menu->addAction(text);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(action);
        if (onLayout)
            layout->addAction(action);
        connect(action, &QAction::triggered, this, [this, onSchematic, onLayout] {
            if (!layoutTabActive())
                onSchematic();
            else if (onLayout)
                onLayout();
        });
        return action;
    };
    canvasAction(edit, tr("&Copy"), QKeySequence::Copy, [view] { view->copySelection(); }, {});
    canvasAction(edit, tr("&Paste"), QKeySequence::Paste, [view] { view->paste(); }, {});
    canvasAction(edit, tr("D&uplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), [view] { view->duplicateSelection(); },
                 {});
    QAction *del = canvasAction(edit, tr("&Delete"), QKeySequence::Delete, [view] { view->deleteSelection(); },
                                [layout] { layout->deleteSelection(); });
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    canvasAction(edit, tr("Re&name"), QKeySequence(Qt::Key_F2), [view] { view->renameSelection(); }, {});
    canvasAction(edit, tr("Select &All"), QKeySequence::SelectAll, [view] { view->selectAll(); },
                 [layout] { layout->selectAll(); });
    edit->addSeparator();
    edit->addAction(tr("&Find Pin..."), QKeySequence::Find, this, [this] {
        m_tabs->setCurrentWidget(m_schematic);
        m_find->setFocus();
        m_find->selectAll();
    });
    edit->addSeparator();
    edit->addAction(tr("Parts &Library..."), this, &MainWindow::editLibrary);
    QAction *preferences = edit->addAction(tr("&Preferences..."), QKeySequence::Preferences, this,
                                           [this] { PreferencesDialog(this).exec(); });
    preferences->setMenuRole(QAction::PreferencesRole);

    // Tools of one editor: shortcuts bound to that editor only, so both can use V/W.
    auto toolAction = [this](QWidget *owner, QActionGroup *group, const QString &text, const QKeySequence &key,
                             const QString &tip, std::function<void()> slot) {
        QAction *action = new QAction(text, this);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setToolTip(tip);
        if (group) {
            action->setCheckable(true);
            group->addAction(action);
        }
        owner->addAction(action);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    QMenu *place = menuBar()->addMenu(tr("&Place"));
    auto *schematicTools = new QActionGroup(this);
    QAction *selectTool = toolAction(view, schematicTools, tr("Select"), QKeySequence(Qt::Key_V),
                                     tr("Select and move items; drag from a pin to draw a wire, or "
                                        "drag a selected wire's end to re-attach it (V)"),
                                     [view] { view->setTool(SchematicView::Tool::Select); });
    QAction *wireTool = toolAction(view, schematicTools, tr("Wire"), QKeySequence(Qt::Key_W),
                                   tr("Draw wires from pins or splices (W)"),
                                   [view] { view->setTool(SchematicView::Tool::Wire); });
    selectTool->setChecked(true);
    QAction *addConnector = toolAction(view, nullptr, tr("Add Connector"), QKeySequence(Qt::Key_C),
                                       tr("Add a connector (C)"), [view] { view->addConnector(); });
    QAction *addSplice = toolAction(view, nullptr, tr("Add Splice"), QKeySequence(Qt::Key_S),
                                    tr("Add a splice (S)"), [view] { view->addSplice(); });
    auto *layoutTools = new QActionGroup(this);
    QAction *layoutSelect = toolAction(layout, layoutTools, tr("Select"), QKeySequence(Qt::Key_V),
                                       tr("Select and move layout points; double-click a segment to "
                                          "type its length (V)"),
                                       [layout] { layout->setTool(LayoutView::Tool::Select); });
    QAction *segmentTool = toolAction(layout, layoutTools, tr("Segment"), QKeySequence(Qt::Key_W),
                                      tr("Drag from a point to another point, or to empty space, to add "
                                         "a segment (W)"),
                                      [layout] { layout->setTool(LayoutView::Tool::Segment); });
    layoutSelect->setChecked(true);
    QAction *addBranch = toolAction(layout, nullptr, tr("Add Branch Point"), QKeySequence(Qt::Key_B),
                                    tr("Add a branch point (B)"), [layout] { layout->addBranchPoint(); });
    place->addSection(tr("Schematic"));
    place->addActions({addConnector, addSplice, selectTool, wireTool});
    place->addSection(tr("Layout"));
    place->addActions({addBranch, layoutSelect, segmentTool});

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
    m_viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, this, [this] { currentCanvas()->zoomIn(); });
    m_viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, this, [this] { currentCanvas()->zoomOut(); });
    QAction *fit = m_viewMenu->addAction(tr("Zoom to &Fit"), QKeySequence(Qt::CTRL | Qt::Key_0), this,
                                         [this] { currentCanvas()->zoomToFit(); });
    QAction *snap = m_viewMenu->addAction(tr("&Snap to Grid"));
    snap->setCheckable(true);
    snap->setChecked(true);
    connect(snap, &QAction::toggled, this, [view, layout](bool on) {
        view->setSnapToGrid(on);
        layout->setSnapToGrid(on);
    });
    QAction *highlight = m_viewMenu->addAction(tr("&Highlight Related Items"));
    highlight->setCheckable(true);
    highlight->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    highlight->setToolTip(tr("Selecting something highlights everything connected to it and dims the rest"));
    highlight->setChecked(QSettings().value(QStringLiteral("view/highlight"), true).toBool());
    m_highlighter->setEnabled(highlight->isChecked());
    connect(highlight, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("view/highlight"), on);
        m_highlighter->setEnabled(on);
    });
    QAction *diameters = m_viewMenu->addAction(tr("Show &Bundle Diameters"));
    diameters->setCheckable(true);
    diameters->setChecked(QSettings().value(QStringLiteral("view/bundleDiameters"), true).toBool());
    m_layout->view()->layoutScene()->setShowBundleDiameters(diameters->isChecked());
    connect(diameters, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("view/bundleDiameters"), on);
        m_layout->view()->layoutScene()->setShowBundleDiameters(on);
    });
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

    // One toolbar per editor; the current tab decides which is shown.
    QToolBar *schematicBar = addToolBar(tr("Schematic"));
    schematicBar->setObjectName(QStringLiteral("schematicToolbar"));
    schematicBar->setMovable(false);
    schematicBar->addActions({selectTool, wireTool});
    schematicBar->addSeparator();
    schematicBar->addActions({addConnector, addSplice});
    schematicBar->addSeparator();
    schematicBar->addAction(fit);
    schematicBar->addSeparator();
    m_find = new QLineEdit(schematicBar);
    m_find->setPlaceholderText(tr("Find pin or circuit (Enter = next)"));
    m_find->setClearButtonEnabled(true);
    m_find->setMaximumWidth(280);
    schematicBar->addWidget(m_find);
    connect(m_find, &QLineEdit::returnPressed, view, [this] { m_schematic->find(m_find->text()); });
    connect(m_find, &QLineEdit::textChanged, view, [this](const QString &text) {
        if (text.isEmpty())
            m_schematic->find({});
    });

    QToolBar *layoutBar = addToolBar(tr("Layout"));
    layoutBar->setObjectName(QStringLiteral("layoutToolbar"));
    layoutBar->setMovable(false);
    layoutBar->addActions({layoutSelect, segmentTool});
    layoutBar->addSeparator();
    layoutBar->addAction(addBranch);
    layoutBar->addSeparator();
    layoutBar->addAction(fit);

    for (QToolBar *bar : {schematicBar, layoutBar})
        bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto syncToolbars = [this, schematicBar, layoutBar] {
        schematicBar->setVisible(!layoutTabActive());
        layoutBar->setVisible(layoutTabActive());
    };
    connect(m_tabs, &QTabWidget::currentChanged, this, syncToolbars);
    syncToolbars();
}

void MainWindow::editLibrary()
{
    LibraryDialog dialog(m_document->model()->userLibrary(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!libraryStore::saveUserLibrary(dialog.library(), &error))
        QMessageBox::warning(this, tr("Parts Library"), error);
    m_document->model()->setUserLibrary(dialog.library());
}

void MainWindow::showSelectionStatus()
{
    const Selection &s = m_selection->selection();
    const Harness &h = m_document->model()->harness();
    if (s.wires.size() == 1 && s.connectors.isEmpty() && s.splices.isEmpty()) {
        if (const Wire *w = m_document->model()->find<Wire>(*s.wires.begin())) {
            const WireLengthText length =
                describeWireLength(h, *w, calc::wireLength(h, calc::LayoutGraph(h), *w));
            statusBar()->showMessage(tr("Wire %1 — cut length %2. %3")
                                         .arg(describeWire(h, *w), length.headline, length.detail));
        }
    } else if (!s.isEmpty()) {
        statusBar()->showMessage(tr("%n item(s) selected", nullptr,
                                    int(s.wires.size() + s.connectors.size() + s.splices.size()
                                        + s.layoutNodes.size() + s.layoutSegments.size())));
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
