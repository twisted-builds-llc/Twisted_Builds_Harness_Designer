// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/HelpDialog.h"
#include "ui/theme/Theme.h"

#include <QMainWindow>

class QLineEdit;
class QTabWidget;
class QMenu;
class QSplitter;
class QToolBar;
class QToolButton;
class QTreeWidget;

namespace tbh::app {

class Autosave;
class BomView;
class CanvasView;
class Document;
class Highlighter;
class LayoutEditor;
class SchematicView;
class SheetTabBar;
class SelectionModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Theme *theme, QWidget *parent = nullptr);

    // Opens without prompting (used for command-line arguments).
    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void createMenus();
    void createDocks();

    void newFile();
    void openWithDialog();
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
    // Asks for a page size and file name, then writes the printable build package.
    void exportPdf();
    // Offers to save unsaved changes. Returns false if the user cancels.
    bool maybeSave();

    void addRecentFile(const QString &path);
    void rebuildRecentMenu();
    void refreshProjectTree();
    void updateTitle();
    void showAbout();
    void showSelectionStatus();
    void editLibrary();
    void showHelp(HelpDialog::Page page);
    // Offers unsaved work from sessions that ended unexpectedly. `quietIfNone` skips the
    // "nothing to recover" message (used at startup).
    void offerRecovery(bool quietIfNone);
    // The editor menu actions and zoom act on: the current tab, or in split view the canvas
    // that last had focus.
    bool layoutActive() const;
    CanvasView *currentCanvas() const;
    // Shows the schematic or layout editor (`m_schematic` or `m_layout`) in either view mode.
    void showEditor(QWidget *editor);
    // Puts the schematic and layout side by side (or stacked) in one tab, or back in their own tabs.
    void setSplitView(bool on);
    void setSplitStacked(bool stacked);
    // Gives the schematic and layout half of the split each.
    void equalizeSplit();
    // Outlines the toolbar and split view buttons. The colors come from the palette when
    // applied, so this runs again when the theme changes.
    void styleButtons();
    void syncToolbars();

    Theme *m_theme;
    Document *m_document;
    SelectionModel *m_selection;
    SchematicView *m_schematic = nullptr;
    QWidget *m_schematicPage = nullptr; // sheet tabs + m_schematic
    SheetTabBar *m_sheetTabs = nullptr;
    LayoutEditor *m_layout = nullptr;
    BomView *m_bom = nullptr;
    Highlighter *m_highlighter = nullptr;
    Autosave *m_autosave = nullptr;
    QTabWidget *m_tabs = nullptr;
    QSplitter *m_splitter = nullptr;
    QAction *m_splitAction = nullptr;
    QToolButton *m_splitButton = nullptr;
    CanvasView *m_lastCanvas = nullptr;
    QToolBar *m_schematicBar = nullptr;
    QToolBar *m_layoutBar = nullptr;
    QLineEdit *m_find = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QTreeWidget *m_projectTree = nullptr;
};

} // namespace tbh::app
