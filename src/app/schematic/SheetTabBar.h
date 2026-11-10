// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QTabBar;
class QToolButton;

namespace tbh::app {

class Document;
class SchematicScene;

// One tab per schematic sheet above the schematic canvas, plus a button that adds a sheet.
// Clicking a tab shows that sheet; double-clicking renames it; the tab context menu renames,
// moves or deletes it. Every change is an undo command on the document.
class SheetTabBar : public QWidget
{
    Q_OBJECT

public:
    SheetTabBar(Document *document, SchematicScene *scene, QWidget *parent = nullptr);

    QTabBar *tabBar() const { return m_tabs; }

    void addSheet();
    // Asks for a new name for the sheet at tab `index`.
    void renameSheet(int index);
    void deleteSheet(int index);
    void moveSheet(int index, int toIndex);

private:
    void refresh();
    void showMenu(const QPoint &pos);
    QString sheetIdAt(int index) const;

    Document *m_document;
    SchematicScene *m_scene;
    QTabBar *m_tabs;
    QToolButton *m_add;
    bool m_refreshing = false;
};

} // namespace tbh::app
