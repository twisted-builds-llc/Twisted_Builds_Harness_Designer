// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"
#include "Preferences.h"
#include "Translations.h"
#include "commands/EditOps.h"
#include "commands/HarnessCommands.h"
#include "ui/properties/ConnectorPanel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLineEdit>
#include <QTableView>
#include <QUndoStack>

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace tbh;
using namespace tbh::app;
using namespace Qt::StringLiterals;

class TestCommands : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep tests away from the user's real settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_commands"_s);
    }

    void init() { QSettings().clear(); }
    void cleanupTestCase() { QSettings().clear(); }

    // Spreadsheet-style editing of several pin cells at once, each one undo step.
    void pinTableBulkEdit()
    {
        Document doc;
        Harness h = doc.model()->harness();
        Connector c;
        c.id = u"c1"_s;
        c.label = u"X1"_s;
        for (int i = 1; i <= 4; ++i)
            c.pins.push_back(Pin{u"p%1"_s.arg(i), QString::number(i), {}, {}, {}, false, {}, {}, {}});
        h.connectors.push_back(c);
        doc.model()->reset(h);
        ConnectorPanel panel(&doc);
        panel.setConnector(u"c1"_s);
        auto *table = panel.findChild<QTableView *>();
        QAbstractItemModel *model = table->model();
        auto pins = [&] { return doc.model()->find<Connector>(u"c1"_s)->pins; };
        auto select = [&](int top, int left, int bottom, int right) {
            table->setCurrentIndex(model->index(top, left));
            table->selectionModel()->select(QItemSelection(model->index(top, left), model->index(bottom, right)),
                                            QItemSelectionModel::ClearAndSelect);
        };
        const int undoBase = doc.undoStack()->count();

        // Typing with three terminal cells selected fills all three.
        select(0, PinTableModel::Terminal, 2, PinTableModel::Terminal);
        table->edit(model->index(0, PinTableModel::Terminal));
        auto *editor = table->viewport()->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(u"T-9"_s);
        QTest::keyClick(editor, Qt::Key_Return);
        // Qt commits the editor on Enter through a queued call.
        QTRY_COMPARE(pins()[0].terminalPartNumber, u"T-9"_s);
        QCOMPARE(pins()[2].terminalPartNumber, u"T-9"_s);
        QVERIFY(pins()[3].terminalPartNumber.isEmpty());
        QCOMPARE(doc.undoStack()->count(), undoBase + 1);

        // Ctrl+C copies the selected block, tab separated, one line per row.
        select(0, PinTableModel::Terminal, 1, PinTableModel::Seal);
        QTest::keyClick(table, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(), u"T-9\t\nT-9\t"_s);

        // One value pastes into every selected cell.
        QGuiApplication::clipboard()->setText(u"S-5"_s);
        select(0, PinTableModel::Seal, 3, PinTableModel::Seal);
        QTest::keyClick(table, Qt::Key_V, Qt::ControlModifier);
        for (const Pin &pin : pins())
            QCOMPARE(pin.sealPartNumber, u"S-5"_s);
        QCOMPARE(doc.undoStack()->count(), undoBase + 2);

        // A block pastes from the top left selected cell.
        QGuiApplication::clipboard()->setText(u"A\tB\r\nC\tD\r\n"_s);
        select(2, PinTableModel::Terminal, 2, PinTableModel::Terminal);
        QTest::keyClick(table, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(pins()[2].terminalPartNumber, u"A"_s);
        QCOMPARE(pins()[2].sealPartNumber, u"B"_s);
        QCOMPARE(pins()[3].terminalPartNumber, u"C"_s);
        QCOMPARE(pins()[3].sealPartNumber, u"D"_s);

        // Delete clears the selected cells.
        select(0, PinTableModel::Seal, 1, PinTableModel::Seal);
        QTest::keyClick(table, Qt::Key_Delete);
        QVERIFY(pins()[0].sealPartNumber.isEmpty());
        QVERIFY(pins()[1].sealPartNumber.isEmpty());
        QCOMPARE(pins()[2].sealPartNumber, u"B"_s);

        // Space toggles every selected Plug box; a plug part number marks the cavity plugged.
        select(0, PinTableModel::Plug, 1, PinTableModel::Plug);
        QTest::keyClick(table, Qt::Key_Space);
        QVERIFY(pins()[0].isCavityPlug);
        QVERIFY(pins()[1].isCavityPlug);
        QVERIFY(!pins()[2].isCavityPlug);
        QVERIFY(model->setData(model->index(3, PinTableModel::PlugPart), u"PL-7"_s, Qt::EditRole));
        QVERIFY(pins()[3].isCavityPlug);
        QCOMPARE(pins()[3].plugPartNumber, u"PL-7"_s);

        // Each bulk edit is one undo step: type, paste, block paste, delete, plug toggle,
        // plug P/N = 6 (copying is not an edit).
        QCOMPARE(doc.undoStack()->count() - undoBase, 6);
        doc.undoStack()->undo(); // plug P/N
        doc.undoStack()->undo(); // plug toggle
        doc.undoStack()->undo(); // delete
        QCOMPARE(pins()[0].sealPartNumber, u"S-5"_s);
        QCOMPARE(pins()[1].sealPartNumber, u"S-5"_s);
    }

    // Wiring pins by typing where they go; new wires get circuit ids.
    void pinTableWiredColumn()
    {
        Document doc;
        Harness h = doc.model()->harness();
        for (const auto &[id, label, count] : {std::tuple{u"c1"_s, u"X1"_s, 3}, std::tuple{u"c2"_s, u"X2"_s, 2}}) {
            Connector c;
            c.id = id;
            c.label = label;
            for (int i = 1; i <= count; ++i)
                c.pins.push_back(Pin{id + u"p%1"_s.arg(i), QString::number(i), {}, {}, {}, false, {}, {}, {}});
            h.connectors.push_back(c);
        }
        doc.model()->reset(h);
        ConnectorPanel panel(&doc);
        panel.setConnector(u"c1"_s);
        QAbstractItemModel *table = panel.findChild<QTableView *>()->model();
        auto *pins = static_cast<PinTableModel *>(table);
        const auto wires = [&] { return doc.model()->harness().wires; };
        const int undoBase = doc.undoStack()->count();

        QVERIFY(table->setData(table->index(0, PinTableModel::Wired), u"x2:1"_s, Qt::EditRole));
        QCOMPARE(wires().size(), size_t(1));
        const Wire first = wires()[0];
        QCOMPARE(first.endA, WireEnd(PinRef{u"c1"_s, u"c1p1"_s}));
        QCOMPARE(first.endB, WireEnd(PinRef{u"c2"_s, u"c2p1"_s}));
        QCOMPARE(first.circuitId, u"C001"_s);
        QCOMPARE(first.baseColor, doc.wireStyle().baseColor);
        QCOMPARE(table->data(table->index(0, PinTableModel::Wired), Qt::DisplayRole).toString(), u"X2 : 1"_s);

        // Names that match nothing change nothing and are reported.
        QSignalSpy notFound(pins, &PinTableModel::connectionsNotFound);
        QVERIFY(!table->setData(table->index(1, PinTableModel::Wired), u"X9 : 1"_s, Qt::EditRole));
        QCOMPARE(notFound.size(), 1);
        QCOMPARE(notFound[0][0].toStringList(), QStringList{u"X9 : 1"_s});
        QCOMPARE(wires().size(), size_t(1));

        // Two cells at once: two new wires, numbered on, one undo step.
        QVERIFY(pins->setCellTexts({{table->index(1, PinTableModel::Wired), u"X2 : 2"_s},
                                    {table->index(2, PinTableModel::Wired), u"X2 : 2"_s}}));
        QCOMPARE(wires().size(), size_t(3));
        QCOMPARE(wires()[1].circuitId, u"C002"_s);
        QCOMPARE(wires()[2].circuitId, u"C003"_s);
        QCOMPARE(doc.undoStack()->count(), undoBase + 2);

        // Naming another end moves the existing wire: same wire, same circuit id.
        QVERIFY(table->setData(table->index(0, PinTableModel::Wired), u"X2 : 2"_s, Qt::EditRole));
        QCOMPARE(wires().size(), size_t(3));
        QCOMPARE(wires()[0].id, first.id);
        QCOMPARE(wires()[0].circuitId, u"C001"_s);
        QCOMPARE(wires()[0].endB, WireEnd(PinRef{u"c2"_s, u"c2p2"_s}));

        doc.undoStack()->undo();
        doc.undoStack()->undo();
        QCOMPARE(wires().size(), size_t(1));
        QCOMPARE(wires()[0].endB, WireEnd(PinRef{u"c2"_s, u"c2p1"_s}));

        // Pasted wires are new circuits: copying C001 pastes as C002.
        Harness fragment;
        fragment.connectors = {h.connectors[0], h.connectors[1]};
        fragment.wires = {wires()[0]};
        QSet<QString> pasted;
        doc.undoStack()->push(pasteFragment(doc.model(), fragment, QPointF(100, 0), &pasted).release());
        QCOMPARE(wires().size(), size_t(2));
        QCOMPARE(wires()[1].circuitId, u"C002"_s);
    }

    void defaultUnitsPreference()
    {
        QCOMPARE(preferences::defaultUnits(), Units::Inches); // unset -> inches
        QCOMPARE(Document().model()->harness().info.units, Units::Inches);

        preferences::setDefaultUnits(Units::Centimeters);
        QCOMPARE(preferences::defaultUnits(), Units::Centimeters);
        Document doc;
        QCOMPARE(doc.model()->harness().info.units, Units::Centimeters);

        preferences::setDefaultUnits(Units::Millimeters);
        doc.newHarness();
        QCOMPARE(doc.model()->harness().info.units, Units::Millimeters);
    }

    void englishPluralsLoad()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        installTranslators(*QCoreApplication::instance());
        QCOMPARE(QCoreApplication::translate("tbh::app::LayoutScene", "%n wire(s)", nullptr, 1), u"1 wire"_s);
        QCOMPARE(QCoreApplication::translate("tbh::app::LayoutScene", "%n wire(s)", nullptr, 3), u"3 wires"_s);
        QCOMPARE(QCoreApplication::translate("tbh::app::SegmentPanel", "%n wire(s) pass through this segment:",
                                             nullptr, 1),
                 u"1 wire passes through this segment:"_s);
    }

    void setHarnessInfoUndoRedo()
    {
        Document doc;
        const HarnessInfo before = doc.model()->harness().info;
        HarnessInfo after = before;
        after.name = u"Renamed"_s;
        after.units = Units::Centimeters;
        after.description = u"desc"_s;

        doc.undoStack()->push(new SetHarnessInfoCommand(doc.model(), after));
        QVERIFY(doc.model()->harness().info == after);
        QVERIFY(doc.isModified());

        doc.undoStack()->undo();
        QVERIFY(doc.model()->harness().info == before);
        QVERIFY(!doc.isModified());

        doc.undoStack()->redo();
        QVERIFY(doc.model()->harness().info == after);
    }

    void documentSaveOpenClearsHistory()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"doc.tbh"_s);
        Document doc;
        QSignalSpy modified(&doc, &Document::modifiedChanged);
        HarnessInfo info = doc.model()->harness().info;
        info.name = u"Saved"_s;
        doc.undoStack()->push(new SetHarnessInfoCommand(doc.model(), info));
        QCOMPARE(modified.count(), 1);

        QString error;
        QVERIFY2(doc.saveAs(path, &error), qPrintable(error));
        QVERIFY(!doc.isModified());
        QCOMPARE(doc.filePath(), path);

        Document other;
        QVERIFY2(other.open(path, &error), qPrintable(error));
        QCOMPARE(other.model()->harness().info.name, u"Saved"_s);
        QCOMPARE(other.undoStack()->count(), 0);
        QVERIFY(!other.isModified());

        other.newHarness();
        QVERIFY(other.filePath().isEmpty());
        QVERIFY(other.model()->harness().createdAt.isValid());
    }
};

QTEST_MAIN(TestCommands)
#include "tst_commands.moc"
