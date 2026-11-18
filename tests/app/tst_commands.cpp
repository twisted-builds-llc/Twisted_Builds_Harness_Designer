// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"
#include "LibraryStore.h"
#include "Preferences.h"
#include "Translations.h"
#include "commands/EditOps.h"
#include "model/Fragment.h"
#include "model/Queries.h"
#include "commands/HarnessCommands.h"
#include "ui/properties/ConnectorPanel.h"

#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QToolButton>
#include <QAbstractItemView>
#include <QClipboard>
#include <QCompleter>
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

namespace {

// A twisted, built cable of `wires` (no shield).
Cable cable(const QString &id, const QString &label, std::vector<QString> wires)
{
    Cable c;
    c.id = id;
    c.label = label;
    c.wireIds = std::move(wires);
    return c;
}

} // namespace

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
        // Unchecking Plug opens the cavity again and clears its plug part number.
        QVERIFY(model->setData(model->index(3, PinTableModel::Plug), int(Qt::Unchecked), Qt::CheckStateRole));
        QVERIFY(!pins()[3].isCavityPlug);
        QVERIFY(pins()[3].plugPartNumber.isEmpty());

        // Each bulk edit is one undo step: type, paste, block paste, delete, plug toggle,
        // plug P/N, unplug = 7 (copying is not an edit).
        QCOMPARE(doc.undoStack()->count() - undoBase, 7);
        doc.undoStack()->undo(); // unplug
        QCOMPARE(pins()[3].plugPartNumber, u"PL-7"_s);
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

        // Pasting onto another sheet shows the same connectors and wire there: no copy, and
        // the wire keeps circuit id C001.
        Harness fragment;
        fragment.connectors = {h.connectors[0], h.connectors[1]};
        fragment.wires = {wires()[0]};
        fragment.schematic.placements[Schematic::wireSheetKey(wires()[0].id)] = SchematicPlacement{};
        QString sheet2;
        doc.undoStack()->push(addSheet(doc.model(), &sheet2).release());
        QSet<QString> pasted;
        doc.undoStack()->push(pasteFragment(doc.model(), fragment, QPointF(100, 0), &pasted, sheet2).release());
        QCOMPARE(doc.model()->harness().connectors.size(), size_t(2));
        QCOMPARE(wires().size(), size_t(1));
        QCOMPARE(wires()[0].circuitId, u"C001"_s);
        QVERIFY(wireSheets(doc.model()->harness(), wires()[0]).drawnOn(sheet2));

        // Pasting where the connectors already are makes real copies: the copied wire is a
        // new circuit, C002.
        doc.undoStack()->push(pasteFragment(doc.model(), fragment, QPointF(100, 0), &pasted, sheet2).release());
        QCOMPARE(doc.model()->harness().connectors.size(), size_t(4));
        QCOMPARE(wires().size(), size_t(2));
        QCOMPARE(wires()[1].circuitId, u"C002"_s);
    }

    // Deleting, cutting and pasting wires keeps cables valid, and undo restores them.
    void cablesFollowWireEdits()
    {
        Document doc;
        Harness h;
        Connector a, b;
        a.id = u"a"_s;
        a.label = u"A"_s;
        b.id = u"b"_s;
        b.label = u"B"_s;
        for (Connector *c : {&a, &b}) {
            for (int i = 1; i <= 4; ++i)
                c->pins.push_back(Pin{c->id + u"%1"_s.arg(i), QString::number(i), {}, {}, {}, false, {}, {}, {}});
        }
        h.connectors = {a, b};
        for (int i = 1; i <= 4; ++i) {
            Wire w;
            w.id = u"w%1"_s.arg(i);
            w.endA = PinRef{u"a"_s, u"a%1"_s.arg(i)};
            w.endB = PinRef{u"b"_s, u"b%1"_s.arg(i)};
            h.wires.push_back(w);
        }
        // A twisted triple w1-w3 with shield w4.
        Cable cb = cable(u"cb"_s, u"TP"_s, {u"w1"_s, u"w2"_s, u"w3"_s});
        cb.shieldWireId = u"w4"_s;
        h.cables = {cb};
        doc.model()->reset(h);
        QUndoStack *stack = doc.undoStack();
        const auto cables = [&]() -> const std::vector<Cable> & { return doc.model()->harness().cables; };

        // Deleting a conductor and the shield: the cable keeps the other two.
        stack->push(removeItems(doc.model(), {}, {}, {}, {u"w2"_s, u"w4"_s}).release());
        QCOMPARE(cables().size(), size_t(1));
        QCOMPARE(cables()[0].wireIds, (std::vector<QString>{u"w1"_s, u"w3"_s}));
        QVERIFY(!cables()[0].shielded());
        // One more conductor gone: no cable is left.
        stack->push(removeItems(doc.model(), {}, {}, {}, {u"w3"_s}).release());
        QVERIFY(cables().empty());
        stack->undo();
        stack->undo();
        QVERIFY(doc.model()->harness() == h);

        // Cutting a conductor: both pieces stay in the cable, the second right after the first.
        QString piece;
        stack->push(cutWire(doc.model(), u"w2"_s, QPointF(0, 0), QPointF(10, 0), {}, &piece).release());
        QCOMPARE(cables()[0].wireIds, (std::vector<QString>{u"w1"_s, u"w2"_s, piece, u"w3"_s}));
        stack->undo();
        // Cutting the shield: the first piece stays the shield, the second is not in the cb.
        stack->push(cutWire(doc.model(), u"w4"_s, QPointF(0, 0), QPointF(10, 0), {}, &piece).release());
        QCOMPARE(cables()[0], cb);
        stack->undo();
        QVERIFY(doc.model()->harness() == h);

        // Pasting a copy of both connectors copies the cable onto the new wires.
        const Harness fragment = extractFragment(doc.model()->harness(), {u"a"_s, u"b"_s}, {});
        QSet<QString> pasted;
        stack->push(pasteFragment(doc.model(), fragment, QPointF(100, 0), &pasted).release());
        QCOMPARE(cables().size(), size_t(2));
        const Cable &copy = cables()[1];
        QCOMPARE(copy.wireIds.size(), size_t(3));
        QVERIFY(copy.wireIds[0] != u"w1"_s);
        QVERIFY(doc.model()->find<Wire>(copy.wireIds[0]));
        QVERIFY(doc.model()->find<Wire>(copy.shieldWireId));
        QVERIFY(copy.shieldWireId != u"w4"_s);
        stack->undo();
        QVERIFY(doc.model()->harness() == h);
    }

    // Picking a library housing fills the connector; terminal cells offer its parts first.
    void connectorFromLibraryHousing()
    {
        Document doc;
        Library lib;
        ConnectorPart housing;
        housing.partNumber = u"DT06-4S"_s;
        housing.manufacturer = u"TE Deutsch"_s;
        housing.family = u"Deutsch DT"_s;
        housing.description = u"4-way plug"_s;
        housing.gender = Gender::Female;
        housing.cavityLabels = {u"A"_s, u"B"_s, u"C"_s, u"D"_s};
        housing.terminals = {u"S16"_s};
        ConnectorPart small = housing;
        small.partNumber = u"DT06-2S"_s;
        small.cavityLabels = {u"1"_s, u"2"_s};
        lib.connectorParts = {housing, small};
        PinPart other;
        other.partNumber = u"OTHER"_s;
        PinPart socket;
        socket.partNumber = u"S16"_s;
        lib.pinParts = {other, socket};
        doc.model()->setUserLibrary(lib);

        Harness h = doc.model()->harness();
        Connector c;
        c.id = u"c1"_s;
        c.label = u"X1"_s;
        c.partNumber = u"OLD"_s;
        for (int i = 1; i <= 2; ++i)
            c.pins.push_back(Pin{u"p%1"_s.arg(i), QString::number(i), {}, {}, {}, false, {}, {}, {}});
        h.connectors.push_back(c);
        doc.model()->reset(h);
        doc.model()->setUserLibrary(lib);
        ConnectorPanel panel(&doc);
        panel.setConnector(u"c1"_s);
        QLineEdit *partNumber = nullptr;
        for (auto *edit : panel.findChildren<QLineEdit *>()) {
            if (edit->accessibleName() == u"Connector part number"_s)
                partNumber = edit;
        }
        QVERIFY(partNumber);
        const auto connector = [&] { return *doc.model()->find<Connector>(u"c1"_s); };
        const int undoBase = doc.undoStack()->count();

        partNumber->setText(u"dt06-4s"_s); // not exact: kept as typed, nothing filled
        emit partNumber->editingFinished();
        QCOMPARE(connector().partNumber, u"dt06-4s"_s);
        QCOMPARE(connector().pins.size(), size_t(2));

        partNumber->setText(u"DT06-4S"_s);
        emit partNumber->editingFinished();
        QCOMPARE(connector().partNumber, u"DT06-4S"_s);
        QCOMPARE(connector().manufacturer, u"TE Deutsch"_s);
        QCOMPARE(connector().description, u"Deutsch DT, 4-way plug"_s);
        QCOMPARE(connector().gender, Gender::Female);
        QCOMPARE(connector().pins.size(), size_t(4));
        QCOMPARE(connector().pins[0].id, u"p1"_s); // existing pins kept
        QCOMPARE(connector().pins[3].label, u"D"_s);
        QCOMPARE(doc.undoStack()->count(), undoBase + 2); // the typed text, then the housing

        // Terminal cells: the housing's socket first, then the rest of the library.
        auto *pins = static_cast<PinTableModel *>(panel.findChild<QTableView *>()->model());
        const auto choices = pins->partChoices(PinTableModel::Terminal);
        QCOMPARE(choices.size(), size_t(2));
        QCOMPARE(choices[0].partNumber, u"S16"_s);
        QVERIFY(choices[0].madeForHousing);
        QCOMPARE(choices[1].partNumber, u"OTHER"_s);
        QVERIFY(pins->partChoices(PinTableModel::Notes).empty());

        // A smaller housing would lose the wire on pin D: cancelling changes nothing.
        Wire wire;
        wire.id = u"w1"_s;
        wire.endA = PinRef{u"c1"_s, connector().pins[3].id};
        wire.endB = PinRef{u"c1"_s, u"p1"_s};
        doc.undoStack()->push(addWire(doc.model(), wire, {}, u"wire"_s).release());
        QTimer::singleShot(0, [] {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(w))
                    box->reject();
            }
        });
        partNumber->setText(u"DT06-2S"_s);
        emit partNumber->editingFinished();
        QCOMPARE(connector().partNumber, u"DT06-4S"_s);
        QCOMPARE(connector().pins.size(), size_t(4));
        QCOMPARE(partNumber->text(), u"DT06-4S"_s); // the field shows the connector again

        doc.undoStack()->undo(); // the wire
        doc.undoStack()->undo(); // the housing, one step
        QCOMPARE(connector().partNumber, u"dt06-4s"_s);
        QCOMPARE(connector().pins.size(), size_t(2));
        // Optional look at the housing list and a terminal cell's list.
        const QString out = qEnvironmentVariable("TBH_SCREENSHOT_DIR");
        if (!out.isEmpty()) {
            doc.model()->setUserLibrary(libraryStore::defaultLibrary());
            partNumber->setText(u"DT06-4S"_s);
            emit partNumber->editingFinished();
            panel.resize(420, 700);
            panel.show();
            QVERIFY(QTest::qWaitForWindowExposed(&panel));
            for (auto *button : panel.findChildren<QToolButton *>()) {
                if (button->accessibleName() == u"Show library housings"_s)
                    button->click();
            }
            QTest::qWait(100);
            partNumber->completer()->popup()->grab().save(out + u"/housing-list.png"_s);
            partNumber->completer()->popup()->hide();
            auto *table = panel.findChild<QTableView *>();
            table->edit(table->model()->index(0, PinTableModel::Terminal));
            QTest::qWait(100);
            auto *editor = table->viewport()->findChild<QLineEdit *>();
            QVERIFY(editor && editor->completer());
            editor->completer()->popup()->grab().save(out + u"/terminal-list.png"_s);
        }
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
