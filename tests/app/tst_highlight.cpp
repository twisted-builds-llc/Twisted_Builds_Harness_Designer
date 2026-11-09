// SPDX-License-Identifier: GPL-3.0-or-later
#include "../core/SampleHarness.h"
#include "Autosave.h"
#include "Document.h"
#include "Highlighter.h"
#include "LibraryStore.h"
#include "SelectionModel.h"
#include "calc/Bundle.h"
#include "commands/EntityCommands.h"
#include "layout/LayoutScene.h"
#include "layout/LayoutView.h"
#include "schematic/SchematicScene.h"
#include "schematic/SchematicView.h"
#include "io/TbhFile.h"
#include "ui/LibraryDialog.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"
#include "ui/properties/LayoutPanels.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QApplication>
#include <QMessageBox>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

using namespace tbh;
using namespace tbh::app;
using namespace tbh::testing;

namespace {

Library testLibrary()
{
    Library lib;
    lib.wireTypes = {WireType{u"gxl"_s, u"GXL"_s,
                              {{Gauge{18, GaugeSystem::Awg}, 2.0}, {Gauge{20, GaugeSystem::Awg}, 1.5},
                               {Gauge{10, GaugeSystem::Awg}, 4.0}},
                              true, {}}};
    lib.sleevings = {Sleeving{u"pet-3"_s, u"PET"_s, SleevingKind::Braided, u"1/8 in"_s, 3.2, std::nullopt, true, {}},
                     Sleeving{u"pet-6"_s, u"PET"_s, SleevingKind::Braided, u"1/4 in"_s, 6.4, std::nullopt, true, {}},
                     Sleeving{u"pet-9"_s, u"PET"_s, SleevingKind::Braided, u"3/8 in"_s, 9.5, std::nullopt, true, {}}};
    return lib;
}

// sample() with w1, w2, w4 typed GXL (2.0, 2.0, 1.5 mm): seg1 bundle 3.8266 mm.
Harness typedSample()
{
    Harness h = sample();
    h.info.units = Units::Millimeters;
    for (Wire &w : h.wires) {
        if (w.id == u"w1"_s || w.id == u"w2"_s || w.id == u"w4"_s) {
            w.wireTypeId = u"gxl"_s;
            w.gauge = Gauge{w.id == u"w4"_s ? 20.0 : 18.0, GaugeSystem::Awg};
        }
    }
    return h;
}

QString labelContaining(QWidget *root, const QString &needle)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->text().contains(needle))
            return label->text();
    }
    return {};
}

} // namespace

class TestHighlight : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_highlight"_s);
        QSettings().clear();
        QDir(Autosave::recoveryDirectory()).removeRecursively(); // no recovery prompt
        QFile::remove(libraryStore::userLibraryPath());
    }

    void highlightDimsBothViews()
    {
        Document document;
        SelectionModel selection(document.model());
        SchematicView schematic(&document, &selection);
        LayoutView layout(&document, &selection);
        Highlighter highlighter(document.model(), &selection);
        QObject::connect(&highlighter, &Highlighter::changed, [&] {
            schematic.schematicScene()->setHighlight(highlighter.current());
            layout.layoutScene()->setHighlight(highlighter.current());
        });
        document.model()->reset(sample());

        Selection s;
        s.connectors.insert(u"sensor"_s);
        selection.setSelection(s);
        QTRY_VERIFY(highlighter.current().has_value());
        SchematicScene *sc = schematic.schematicScene();
        LayoutScene *ls = layout.layoutScene();
        // Related: ECU, sensor, w1, w4, seg1, seg2. Unrelated: lamp, splice, w2, seg3.
        QCOMPARE(sc->connectorItem(u"ecu"_s)->opacity(), 1.0);
        QVERIFY(sc->connectorItem(u"lamp"_s)->opacity() < 0.5);
        QCOMPARE(sc->wireItem(u"w1"_s)->opacity(), 1.0);
        QVERIFY(sc->wireItem(u"w2"_s)->opacity() < 0.5);
        QVERIFY(sc->spliceItem(u"sp"_s)->opacity() < 0.5);
        QCOMPARE(ls->segmentItem(u"seg2"_s)->opacity(), 1.0);
        QVERIFY(ls->segmentItem(u"seg3"_s)->opacity() < 0.5);
        QVERIFY(ls->nodeItem(u"n4"_s)->opacity() < 0.5);

        // Selecting a layout segment highlights its wires in the schematic.
        Selection seg;
        seg.layoutSegments.insert(u"seg3"_s);
        selection.setSelection(seg);
        QTRY_COMPARE(sc->wireItem(u"w2"_s)->opacity(), 1.0);
        QVERIFY(sc->wireItem(u"w1"_s)->opacity() < 0.5);

        // Turning highlight off, or clearing the selection, restores everything.
        highlighter.setEnabled(false);
        QCOMPARE(sc->wireItem(u"w1"_s)->opacity(), 1.0);
        highlighter.setEnabled(true);
        QTRY_VERIFY(sc->wireItem(u"w1"_s)->opacity() < 0.5);
        selection.setSelection({});
        QTRY_COMPARE(sc->wireItem(u"w1"_s)->opacity(), 1.0);
        QCOMPARE(ls->segmentItem(u"seg3"_s)->opacity(), 1.0);
    }

    void clickingPinNarrowsSelection()
    {
        Document document;
        SelectionModel selection(document.model());
        SchematicView view(&document, &selection);
        view.resize(1200, 800);
        view.show();
        Harness h = sample();
        h.schematic.placements[u"ecu"_s] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        document.model()->reset(h);
        QTest::qWait(1);
        view.resetTransform();
        view.centerOn(150, 100);
        ConnectorItem *ecu = view.schematicScene()->connectorItem(u"ecu"_s);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, {},
                          view.mapFromScene(ecu->mapToScene(ecu->circuitCellRect(1).center())));
        QCOMPARE(selection.selection().pins, (QSet<QString>{u"ecu/ecu2"_s}));
        QCOMPARE(selection.selection().connectors, (QSet<QString>{u"ecu"_s}));
    }

    // M4 acceptance: sleeving recommendation updates live when gauges change.
    void sleevingRecommendationUpdatesLive()
    {
        Document document;
        document.model()->setUserLibrary(testLibrary());
        document.model()->reset(typedSample());
        SegmentPanel panel(&document);
        panel.show();
        panel.setSegment(u"seg1"_s);
        // seg1: D = 3.8266 mm, needs 4.21 mm with 10 % margin -> PET is the only family.
        QVERIFY2(labelContaining(&panel, u"Recommended: 1/4 in"_s).size() > 0,
                 qPrintable(labelContaining(&panel, u"Recommended"_s)));
        QVERIFY(!labelContaining(&panel, u"Ø 3.8 mm"_s).isEmpty());

        // w4 to 10 AWG (4.0 mm): sum 4 + 4 + 16 = 24; / 0.7 = 34.2857; D = 5.8554 mm;
        // needs 6.441 mm -> 3/8 in (9.5) since 1/4 in (6.4) is now too small.
        Wire w4 = *document.model()->find<Wire>(u"w4"_s);
        w4.gauge = Gauge{10, GaugeSystem::Awg};
        document.undoStack()->push(new UpdateEntityCommand<Wire>(document.model(), w4, u"gauge"_s));
        QVERIFY(!labelContaining(&panel, u"Recommended: 3/8 in"_s).isEmpty());
        QVERIFY(!labelContaining(&panel, u"Ø 5.9 mm"_s).isEmpty());

        // Assign 1/4 in and see it reported as too small.
        LayoutSegment seg = *document.model()->find<LayoutSegment>(u"seg1"_s);
        seg.sleevingId = u"pet-6"_s;
        document.undoStack()->push(new UpdateEntityCommand<LayoutSegment>(document.model(), seg, u"sleeve"_s));
        QVERIFY(!labelContaining(&panel, u"too small"_s).isEmpty());
        // Undoing the gauge change makes it fit again.
        document.undoStack()->undo();
        document.undoStack()->undo();
        Wire back = *document.model()->find<Wire>(u"w4"_s);
        QCOMPARE(back.gauge.value, 20.0);
        document.undoStack()->push(new UpdateEntityCommand<LayoutSegment>(document.model(), seg, u"sleeve"_s));
        QVERIFY(!labelContaining(&panel, u"fits"_s).isEmpty());
    }

    void saveEmbedsUsedLibraryEntries()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"embedded.tbh"_s);
        Document author;
        author.model()->setUserLibrary(testLibrary());
        Harness h = typedSample();
        h.layoutSegments[1].sleevingId = u"pet-9"_s;
        author.model()->reset(h);
        QString error;
        QVERIFY2(author.saveAs(path, &error), qPrintable(error));

        // Another machine with an empty library still resolves the parts.
        Document reader;
        QVERIFY2(reader.open(path, &error), qPrintable(error));
        const Library &lib = reader.model()->library();
        QVERIFY(lib.wireType(u"gxl"_s));
        QVERIFY(lib.sleeving(u"pet-9"_s));
        QVERIFY(!lib.sleeving(u"pet-3"_s)); // unused entries are not embedded
        const Harness &loaded = reader.model()->harness();
        const auto usage = calc::wiresBySegment(loaded, calc::LayoutGraph(loaded));
        QVERIFY(std::abs(calc::bundleDiameter(loaded, lib, usage.value(u"seg1"_s)).diameterMm - 3.826599) < 1e-6);
    }

    void libraryDialogClearsApproximateOnEdit()
    {
        Library lib = testLibrary();
        lib.wireTypes.push_back(WireType{u"txl"_s, u"TXL"_s, {{Gauge{18, GaugeSystem::Awg}, 1.57}}, true, {}});
        LibraryDialog dialog(lib);
        auto *types = dialog.findChild<QListWidget *>();
        QTableWidget *ods = nullptr;
        for (auto *table : dialog.findChildren<QTableWidget *>()) {
            if (table->columnCount() == 3)
                ods = table;
        }
        QVERIFY(types && ods);
        types->setCurrentRow(1); // TXL
        QCOMPARE(ods->rowCount(), 1);
        ods->item(0, 2)->setText(u"1.60"_s);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.library().wireType(u"txl"_s)->outerDiameters[0].odMm, 1.60);
        QVERIFY(!dialog.library().wireType(u"txl"_s)->approximate); // edited
        QVERIFY(dialog.library().wireType(u"gxl"_s)->approximate);  // untouched
        QVERIFY(dialog.library().sleeving(u"pet-3"_s)->approximate);

        // Invalid input keeps the dialog open.
        LibraryDialog bad(lib);
        auto *badOds = bad.findChildren<QTableWidget *>().constFirst()->columnCount() == 3
                           ? bad.findChildren<QTableWidget *>().constFirst()
                           : bad.findChildren<QTableWidget *>().constLast();
        badOds->item(0, 2)->setText(u"-1"_s);
        QTimer::singleShot(0, [] {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(w))
                    box->accept();
            }
        });
        bad.accept();
        QCOMPARE(bad.result(), int(QDialog::Rejected));
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("TBH_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("TBH_SCREENSHOT_DIR not set");
        QVERIFY(libraryStore::saveUserLibrary(libraryStore::defaultLibrary(), nullptr));
        Harness h = typedSample();
        for (Wire &w : h.wires)
            if (w.wireTypeId == u"gxl"_s)
                w.wireTypeId = u"seed-gxl"_s;
        h.schematic.placements[u"ecu"_s] = SchematicPlacement{QPointF(0, 0), false, 0, {}};
        h.schematic.placements[u"sensor"_s] = SchematicPlacement{QPointF(600, 0), false, 0, {}};
        h.schematic.placements[u"lamp"_s] = SchematicPlacement{QPointF(600, 200), false, 0, {}};
        h.schematic.placements[u"sp"_s] = SchematicPlacement{QPointF(400, 300), false, 0, {}};
        h.layoutNodes[1].position = QPointF(300, 0);
        h.layoutNodes[2].position = QPointF(600, 0);
        h.layoutNodes[3].position = QPointF(300, 200);
        h.layoutNodes[4].position = QPointF(550, 200);
        const QString file = out + u"/m4.tbh"_s;
        QVERIFY(io::writeTbhFile(file, h, nullptr));
        Theme theme;
        MainWindow window(&theme);
        window.resize(1500, 900);
        window.show();
        QVERIFY(window.openFile(file));
        auto *selection = window.findChild<SelectionModel *>();
        Selection s;
        s.connectors.insert(u"sensor"_s);
        selection->setSelection(s);
        QTest::qWait(20);
        auto *schematic = window.findChild<SchematicView *>();
        schematic->resetTransform();
        schematic->centerOn(450, 150);
        window.grab().save(out + u"/m4-highlight-schematic.png"_s);
        window.findChild<QTabWidget *>()->setCurrentIndex(1);
        QTest::qWait(20);
        window.findChild<LayoutView *>()->zoomToFit();
        window.grab().save(out + u"/m4-highlight-layout.png"_s);
        Selection seg;
        seg.layoutSegments.insert(u"seg1"_s);
        selection->setSelection(seg);
        QTest::qWait(20);
        window.grab().save(out + u"/m4-segment.png"_s);
        QFile::remove(libraryStore::userLibraryPath());
    }

    void libraryStore()
    {
        const Library seed = libraryStore::defaultLibrary();
        QVERIFY(seed.wireTypes.size() >= 4);
        QVERIFY(seed.sleevings.size() >= 10);
        for (const WireType &t : seed.wireTypes)
            QVERIFY(t.approximate); // shipped values must be marked approximate
        for (const Sleeving &s : seed.sleevings)
            QVERIFY(s.approximate);

        QFile::remove(libraryStore::userLibraryPath());
        QString warning;
        QVERIFY(libraryStore::loadUserLibrary(&warning) == seed); // first run
        QVERIFY(warning.isEmpty());

        QString error;
        QVERIFY2(libraryStore::saveUserLibrary(testLibrary(), &error), qPrintable(error));
        QVERIFY(libraryStore::loadUserLibrary(&warning) == testLibrary());

        QFile file(libraryStore::userLibraryPath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{ not json");
        file.close();
        QVERIFY(libraryStore::loadUserLibrary(&warning) == seed);
        QVERIFY(!warning.isEmpty());
        QFile::remove(libraryStore::userLibraryPath());
    }
};

QTEST_MAIN(TestHighlight)
#include "tst_highlight.moc"
