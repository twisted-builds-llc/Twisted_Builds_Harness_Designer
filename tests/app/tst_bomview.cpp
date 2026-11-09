// SPDX-License-Identifier: GPL-3.0-or-later
#include "../core/SampleHarness.h"
#include "Document.h"
#include "bom/BomView.h"
#include "ui/Format.h"
#include "commands/EntityCommands.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace tbh;
using namespace tbh::app;
using namespace tbh::testing;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QPushButton *buttonNamed(QWidget *root, const QString &text)
{
    for (auto *b : root->findChildren<QPushButton *>()) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

} // namespace

class TestBomView : public QObject
{
    Q_OBJECT

private slots:
    void tablesFollowTheModel()
    {
        Document document;
        document.model()->setUserLibrary(bomLibrary());
        Harness h = bomSample();
        h.info.units = Units::Millimeters;
        document.model()->reset(h);
        BomView view(&document);
        QTRY_COMPARE(view.currentBom().wires.size(), size_t(2));
        auto *wires = view.findChild<QTableWidget *>(u"wireTable"_s);
        QCOMPARE(wires->rowCount(), 2);
        QCOMPARE(view.findChild<QTableWidget *>(u"cutListTable"_s)->rowCount(), 5);
        QCOMPARE(wires->item(0, 5)->text(), formatLength(1333.5, Units::Millimeters));
        QVERIFY(view.findChild<QLabel *>()->text().contains(u"4"_s)); // 4 flagged lines

        // A gauge edit regroups live.
        Wire w2 = *document.model()->find<Wire>(u"w2"_s);
        w2.gauge = Gauge{16, GaugeSystem::Awg};
        document.undoStack()->push(new UpdateEntityCommand<Wire>(document.model(), w2, u"gauge"_s));
        QTRY_COMPARE(view.currentBom().wires.size(), size_t(3));
    }

    void extrasAreUndoable()
    {
        Document document;
        BomView view(&document);
        buttonNamed(&view, u"Add Item"_s)->click();
        QCOMPARE(document.model()->harness().bomExtras.size(), size_t(1));
        QTableWidget *extras = view.findChild<QTableWidget *>(u"extrasTable"_s);
        QTRY_COMPARE(extras->rowCount(), 1);
        extras->item(0, 2)->setText(u"3.5"_s); // quantity
        QCOMPARE(document.model()->harness().bomExtras[0].quantity, 3.5);
        extras->item(0, 2)->setText(u"lots"_s); // invalid: ignored
        QCOMPARE(document.model()->harness().bomExtras[0].quantity, 3.5);
        document.undoStack()->undo();
        QCOMPARE(document.model()->harness().bomExtras[0].quantity, 1.0);
        document.undoStack()->undo();
        QVERIFY(document.model()->harness().bomExtras.empty());
    }

    // M5 acceptance: a complete harness exports a BOM whose wire and sleeving totals
    // match the hand-calculated fixtures (tests/core/tst_bom.cpp).
    void exportMatchesFixtures()
    {
        Document document;
        document.model()->setUserLibrary(bomLibrary());
        Harness h = bomSample();
        h.info.units = Units::Millimeters;
        document.model()->reset(h);
        BomView view(&document);
        QTRY_COMPARE(view.currentBom().wires.size(), size_t(2));

        QTemporaryDir dir;
        QString error;
        const QString combined = dir.filePath(u"bom.csv"_s);
        QVERIFY2(view.writeCsv(combined, false, u"x"_s, &error), qPrintable(error));
        const QByteArray data = readFile(combined);
        QVERIFY(data.startsWith("\xEF\xBB\xBF")); // UTF-8 BOM for spreadsheets
        const QString text = QString::fromUtf8(data.mid(3));
        QVERIFY(text.contains(u"RED,WHT,18 AWG,,2,1270,1333.5,0\r\n"_s));
        QVERIFY(text.contains(u"BLK,,20 AWG,,3,1105,1160.3,1\r\n"_s));
        QVERIFY(text.contains(u"PET,1/4 in,2,750,787.5\r\n"_s));
        QVERIFY(text.contains(u"Shrink,3/8 in,1,120,126\r\n"_s));

        QVERIFY2(view.writeCsv(dir.path(), true, u"harness"_s, &error), qPrintable(error));
        for (const char *section : {"wires", "cut-list", "sleeving", "parts", "other-items"})
            QVERIFY2(QFile::exists(dir.filePath(u"harness-%1.csv"_s.arg(QLatin1StringView(section)))), section);

        QVERIFY(!view.writeCsv(dir.filePath(u"no/such/dir/x.csv"_s), false, u"x"_s, &error));
        QVERIFY(!error.isEmpty());
    }

    void screenshot()
    {
        const QString out = qEnvironmentVariable("TBH_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("TBH_SCREENSHOT_DIR not set");
        Document document;
        document.model()->setUserLibrary(bomLibrary());
        document.model()->reset(bomSample());
        BomView view(&document);
        view.resize(1100, 500);
        view.show();
        QTest::qWait(20);
        view.grab().save(out + u"/m5-bom-wire.png"_s);
        auto *tabs = view.findChild<QTabWidget *>();
        tabs->setCurrentIndex(3);
        QTest::qWait(20);
        view.grab().save(out + u"/m5-bom-parts.png"_s);
    }
};

QTEST_MAIN(TestBomView)
#include "tst_bomview.moc"
