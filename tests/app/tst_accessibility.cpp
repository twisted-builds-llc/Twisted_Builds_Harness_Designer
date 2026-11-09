// SPDX-License-Identifier: GPL-3.0-or-later
#include "../core/SampleHarness.h"
#include "Autosave.h"
#include "Document.h"
#include "SelectionModel.h"
#include "io/TbhFile.h"
#include "layout/LayoutView.h"
#include "schematic/SchematicScene.h"
#include "schematic/SchematicView.h"
#include "ui/HelpDialog.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

using namespace tbh;
using namespace tbh::app;
using namespace tbh::testing;

namespace {

// Visible, focusable input widgets whose accessible name is empty.
QStringList unnamedInputs(QWidget *root)
{
    QStringList missing;
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (!w->isVisible() || w->focusPolicy() == Qt::NoFocus)
            continue;
        const bool input = qobject_cast<QLineEdit *>(w) || qobject_cast<QComboBox *>(w)
                           || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QAbstractButton *>(w)
                           || qobject_cast<QAbstractItemView *>(w) || qobject_cast<QPlainTextEdit *>(w)
                           || qobject_cast<QGraphicsView *>(w);
        if (!input)
            continue;
        if (qobject_cast<QLineEdit *>(w)
            && (qobject_cast<QComboBox *>(w->parentWidget()) || qobject_cast<QAbstractSpinBox *>(w->parentWidget())))
            continue; // a combo or spin box's own editor: the box itself is checked
        if (w->inherits("QTabBar") || w->inherits("QScrollBar") || w->inherits("QHeaderView")
            || w->inherits("QToolButton") /* toolbar actions read their action text */)
            continue;
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(w);
        if (!iface || iface->text(QAccessible::Name).trimmed().isEmpty())
            missing << QStringLiteral("%1 (%2)").arg(QString::fromLatin1(w->metaObject()->className()),
                                                    w->parentWidget() ? QString::fromLatin1(w->parentWidget()->metaObject()->className())
                                                                      : QString());
    }
    return missing;
}

} // namespace

class TestAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_accessibility"_s);
        QSettings().clear();
        QDir(Autosave::recoveryDirectory()).removeRecursively();
    }

    void everyVisibleInputHasAName()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(u"a11y.tbh"_s);
        Harness h = sample();
        h.schematic.placements[u"ecu"_s] = SchematicPlacement{};
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        Theme theme;
        MainWindow window(&theme);
        window.resize(1500, 950);
        window.show();
        QVERIFY(window.openFile(file));
        auto *selection = window.findChild<SelectionModel *>();
        auto *tabs = window.findChild<QTabWidget *>();

        auto check = [&](const char *state) {
            QTest::qWait(10);
            const QStringList missing = unnamedInputs(&window);
            QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QLatin1StringView(state),
                                                                               missing.join(u", "_s))));
        };
        check("nothing selected");
        Selection s;
        s.connectors.insert(u"ecu"_s);
        selection->setSelection(s);
        check("connector");
        selection->setSelection(Selection{{}, {}, {}, {u"w1"_s}, {}, {}, {}});
        check("wire");
        selection->setSelection(Selection{{}, {}, {u"sp"_s}, {}, {}, {}, {}});
        check("splice");
        tabs->setCurrentIndex(1);
        selection->setSelection(Selection{{}, {}, {}, {}, {}, {u"seg1"_s}, {}});
        check("layout segment");
        selection->setSelection(Selection{{}, {}, {}, {}, {u"n2"_s}, {}, {}});
        check("layout node");
        tabs->setCurrentIndex(2);
        check("BOM");
    }

    void keyboardNavigationAndNudge()
    {
        Document document;
        SelectionModel selection(document.model());
        SchematicView view(&document, &selection);
        view.resize(1000, 700);
        view.show();
        Harness h = sample();
        h.schematic.placements[u"ecu"_s] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        h.schematic.placements[u"sensor"_s] = SchematicPlacement{QPointF(600, 0), false, 0, {}};
        h.schematic.placements[u"lamp"_s] = SchematicPlacement{QPointF(600, 300), false, 0, {}};
        h.schematic.placements[u"sp"_s] = SchematicPlacement{QPointF(300, 400), false, 0, {}};
        document.model()->reset(h);
        QTest::qWait(1);
        view.setFocus();

        QTest::keyClick(view.viewport(), Qt::Key_BracketRight);
        QCOMPARE(view.schematicScene()->selectedItems().size(), 1);
        QTest::keyClick(view.viewport(), Qt::Key_BracketRight);
        QCOMPARE(view.schematicScene()->selectedItems().size(), 1);

        // Select the ECU and nudge it right, then 10x down with Shift; undo restores.
        selection.setSelection(Selection{{u"ecu"_s}, {}, {}, {}, {}, {}, {}});
        QTest::keyClick(view.viewport(), Qt::Key_Right);
        QCOMPARE(document.model()->placement(u"ecu"_s)->position, QPointF(10, 0));
        QTest::keyClick(view.viewport(), Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(document.model()->placement(u"ecu"_s)->position, QPointF(10, 100));
        document.undoStack()->undo();
        document.undoStack()->undo();
        QCOMPARE(document.model()->placement(u"ecu"_s)->position, QPointF(0, 0));
    }

    void shortcutListMatchesActions()
    {
        Theme theme;
        MainWindow window(&theme);
        const auto entries = HelpDialog::collectShortcuts(
            &window, {{window.findChild<SchematicView *>(), u"Schematic"_s},
                      {window.findChild<LayoutView *>(), u"Layout"_s}});
        auto has = [&](const QString &action, const QString &where) {
            return std::any_of(entries.begin(), entries.end(), [&](const HelpDialog::ShortcutEntry &e) {
                return e.action == action && e.where.contains(where);
            });
        };
        QVERIFY(has(u"Add Connector"_s, u"Schematic"_s));
        QVERIFY(has(u"Add Branch Point"_s, u"Layout"_s));
        QVERIFY(has(u"Delete"_s, u"Layout"_s));
        QVERIFY(has(u"Save"_s, u"Anywhere"_s));
        QVERIFY(has(u"Undo"_s, u"Anywhere"_s));
        if (const QString out = qEnvironmentVariable("TBH_SCREENSHOT_DIR"); !out.isEmpty()) {
            HelpDialog dialog(entries, HelpDialog::Page::Shortcuts);
            dialog.show();
            QTest::qWait(10);
            dialog.grab().save(out + u"/m6-shortcuts.png"_s);
        }
    }
};

QTEST_MAIN(TestAccessibility)
#include "tst_accessibility.moc"
