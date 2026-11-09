// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"
#include "commands/SetHarnessInfoCommand.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tbh;
using namespace tbh::app;
using namespace Qt::StringLiterals;

class TestCommands : public QObject
{
    Q_OBJECT

private slots:
    void setHarnessInfoUndoRedo()
    {
        Document doc;
        const HarnessInfo before = doc.model()->harness().info;
        HarnessInfo after = before;
        after.name = u"Renamed"_s;
        after.units = Units::Inches;
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
