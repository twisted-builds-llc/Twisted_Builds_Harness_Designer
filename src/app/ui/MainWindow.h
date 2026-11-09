// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/HelpDialog.h"
#include "ui/theme/Theme.h"

#include <QMainWindow>

class QLineEdit;
class QTabWidget;
class QMenu;
class QTreeWidget;

namespace tbh::app {

class Autosave;
class BomView;
class CanvasView;
class Document;
class Highlighter;
class LayoutEditor;
class SchematicView;
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

private:
    void createMenus();
    void createDocks();

    void newFile();
    void openWithDialog();
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
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
    bool layoutTabActive() const;
    CanvasView *currentCanvas() const;

    Theme *m_theme;
    Document *m_document;
    SelectionModel *m_selection;
    SchematicView *m_schematic = nullptr;
    LayoutEditor *m_layout = nullptr;
    BomView *m_bom = nullptr;
    Highlighter *m_highlighter = nullptr;
    Autosave *m_autosave = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_find = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QTreeWidget *m_projectTree = nullptr;
};

} // namespace tbh::app
