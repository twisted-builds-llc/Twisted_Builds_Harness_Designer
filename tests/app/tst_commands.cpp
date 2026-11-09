// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"
#include "Preferences.h"
#include "Translations.h"
#include "commands/HarnessCommands.h"

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
