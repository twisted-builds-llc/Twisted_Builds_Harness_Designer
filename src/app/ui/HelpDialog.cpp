// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/HelpDialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

namespace tbh::app {

namespace {

QTextBrowser *page(const QString &html, QWidget *parent)
{
    auto *browser = new QTextBrowser(parent);
    browser->setOpenExternalLinks(false);
    browser->setHtml(html);
    return browser;
}

} // namespace

std::vector<HelpDialog::ShortcutEntry>
HelpDialog::collectShortcuts(QWidget *window, const std::vector<std::pair<QWidget *, QString>> &editors)
{
    std::vector<ShortcutEntry> entries;
    QSet<QString> seen;
    for (QAction *action : window->findChildren<QAction *>()) {
        if (action->shortcuts().isEmpty() || action->text().isEmpty())
            continue;
        QString where = tr("Anywhere");
        if (action->shortcutContext() == Qt::WidgetWithChildrenShortcut) {
            QStringList places;
            for (const auto &[editor, name] : editors) {
                if (editor->actions().contains(action))
                    places << name;
            }
            if (places.isEmpty())
                continue; // only reachable from a menu
            where = places.join(QStringLiteral(", "));
        }
        QStringList keys;
        for (const QKeySequence &key : action->shortcuts())
            keys << key.toString(QKeySequence::NativeText);
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        text.remove(QStringLiteral("..."));
        const QString id = text + keys.join(QLatin1Char('|')) + where;
        if (seen.contains(id))
            continue;
        seen.insert(id);
        entries.push_back({text, keys.join(QStringLiteral(", ")), where});
    }
    // Keys the editors handle themselves (not actions).
    entries.push_back({tr("Next / previous item"), QStringLiteral("] / ["), tr("Schematic, Layout")});
    entries.push_back({tr("Nudge selection (10x with Shift)"), tr("Arrow keys"), tr("Schematic, Layout")});
    entries.push_back({tr("Cancel / clear selection"), tr("Esc"), tr("Schematic, Layout")});
    entries.push_back({tr("Edit next row / previous row"), tr("Enter / Shift+Enter"), tr("Pin text editor")});
    entries.push_back({tr("Edit next / previous cell"), tr("Tab / Shift+Tab"), tr("Pin text editor")});
    std::stable_sort(entries.begin(), entries.end(), [](const ShortcutEntry &a, const ShortcutEntry &b) {
        return a.where < b.where;
    });
    return entries;
}

HelpDialog::HelpDialog(const std::vector<ShortcutEntry> &shortcuts, Page start, QWidget *parent)
    : QDialog(parent)
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(tr("Help"));
    resize(760, 600);

    const QString gettingStarted = tr(
        "<h2>Getting started</h2>"
        "<ol>"
        "<li><b>Schematic tab.</b> Add connectors (<b>C</b>). Set the pin count with "
        "<b>&minus;</b>/<b>+</b> in the header or by typing it, and fill labels in bulk with "
        "<i>Pin labels...</i> in Properties. Double-click a pin to type its label or circuit.</li>"
        "<li><b>Draw wires</b> by dragging from a pin, or the short lead next to it, to another pin or a "
        "splice (<b>S</b> adds one), or to empty space for a flying lead. A pin's circuit text is copied to "
        "an empty pin at the other end, through any splices; after that each end can be renamed on its own. Set color, stripe, gauge and wire type in Properties; select "
        "several wires to change them together. Drag a selected wire's end to move it, or drag the wire "
        "sideways to move its vertical run so parallel wires do not overlap.</li>"
        "<li><b>Layout tab.</b> Double-click each item under <i>Not in the layout yet</i> to place it, add "
        "branch points (<b>B</b>), and draw segments by dragging the short lead on the side of a point (or "
        "anywhere on it with the Segment tool, <b>W</b>). Type each branch "
        "length: the drawing does not need to be to scale. A splice lives inside the sleeving: place it, "
        "then drag it onto a bundle. The bundle is split there; double-click the splice to type its "
        "distance from one end, and the bundle keeps its total length.</li>"
        "<li><b>Settings.</b> With nothing selected, Properties shows the harness settings: end "
        "allowances, service loop, rounding and waste factors.</li>"
        "<li><b>Check.</b> Select a wire to see its cut length and how it was calculated. Select anything "
        "to highlight what it connects to. The <i>Problems</i> panel lists unrouted wires.</li>"
        "<li><b>Sleeving.</b> Select a segment to see its bundle diameter and pick sleeving; the "
        "recommended size follows your wires.</li>"
        "<li><b>BOM tab.</b> Review wire, cut list, sleeving and parts, add other items, and export CSV.</li>"
        "</ol>"
        "<p>Every change can be undone. Unsaved work is autosaved for recovery (see Preferences). Wire "
        "and sleeving sizes shipped with the app are approximate: check them in "
        "<i>Edit &gt; Parts Library</i>.</p>"
        "<p>This application makes no network connections.</p>");

    const QString mouse = tr(
        "<h2>Mouse</h2>"
        "<table cellpadding='4'>"
        "<tr><td><b>Wheel</b></td><td>Zoom around the pointer (over a scrolling connector: scroll its "
        "rows; Ctrl+wheel always zooms)</td></tr>"
        "<tr><td><b>Middle drag</b></td><td>Pan</td></tr>"
        "<tr><td><b>Drag on empty space</b></td><td>Select with a rectangle</td></tr>"
        "<tr><td><b>Drag a connector header</b></td><td>Move it</td></tr>"
        "<tr><td><b>Drag from a pin row or its lead</b></td><td>Draw a wire (a splice's lead works "
        "too)</td></tr>"
        "<tr><td><b>Drag a layout point's lead</b></td><td>Draw a segment to another point, or to empty "
        "space for a new branch point</td></tr>"
        "<tr><td><b>Drop a branch point onto another point</b></td><td>Join the two points</td></tr>"
        "<tr><td><b>Drag a selected segment's middle handle</b></td><td>Curve the segment (drag back to "
        "straighten, or right-click &gt; Straighten Segment)</td></tr>"
        "<tr><td><b>Double-click a note</b></td><td>Edit its text (clear it to delete the note); "
        "notes work on the schematic and the layout (Place &gt; Add Note, N)</td></tr>"
        "<tr><td><b>Drag a selected wire's end</b></td><td>Move the end to another pin or splice, or make "
        "it a flying lead</td></tr>"
        "<tr><td><b>Drag a flying lead onto a pin</b></td><td>Attach it</td></tr>"
        "<tr><td><b>Click a pin row</b></td><td>Select that pin (highlights its wires)</td></tr>"
        "<tr><td><b>Double-click text</b></td><td>Edit it (pin label, circuit, connector name, wire "
        "name, segment length)</td></tr>"
        "<tr><td><b>Ctrl+click</b></td><td>Add to or remove from the selection</td></tr>"
        "</table>");

    auto *table = new QTableWidget(int(shortcuts.size()), 3, this);
    table->setHorizontalHeaderLabels({tr("Action"), tr("Keys"), tr("Where")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAccessibleName(tr("Keyboard shortcuts"));
    for (int r = 0; r < int(shortcuts.size()); ++r) {
        table->setItem(r, 0, new QTableWidgetItem(shortcuts[size_t(r)].action));
        table->setItem(r, 1, new QTableWidgetItem(shortcuts[size_t(r)].keys));
        table->setItem(r, 2, new QTableWidgetItem(shortcuts[size_t(r)].where));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_tabs->addTab(page(gettingStarted, this), tr("Getting Started"));
    m_tabs->addTab(table, tr("Keyboard Shortcuts"));
    m_tabs->addTab(page(mouse, this), tr("Mouse"));
    m_tabs->setCurrentIndex(int(start));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
}

} // namespace tbh::app
