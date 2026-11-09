// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/theme/Theme.h"

#include <QMainWindow>

class QMenu;
class QTreeWidget;

namespace tbh::app {

class Document;

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

    Theme *m_theme;
    Document *m_document;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QTreeWidget *m_projectTree = nullptr;
};

} // namespace tbh::app
