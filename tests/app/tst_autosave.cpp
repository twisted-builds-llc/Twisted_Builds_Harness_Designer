// SPDX-License-Identifier: GPL-3.0-or-later
#include "Autosave.h"
#include "Document.h"
#include "commands/HarnessCommands.h"

#include <QApplication>
#include <QDir>
#include <QProcess>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdlib>
#include <QTimer>

using namespace tbh;
using namespace tbh::app;
using namespace Qt::StringLiterals;

namespace {

void rename(Document &document, const QString &name)
{
    HarnessInfo info = document.model()->harness().info;
    info.name = name;
    document.undoStack()->push(new SetHarnessInfoCommand(document.model(), info));
}

// Pretends another session crashed: copies this session's files under a new id without
// a lock, the state a dead process leaves behind.
QString simulateCrashedSession(const Autosave &autosave)
{
    const QString id = u"crashed-session"_s;
    const QDir dir(Autosave::recoveryDirectory());
    const QString base = QFileInfo(autosave.recoveryFile()).completeBaseName();
    QFile::copy(autosave.recoveryFile(), dir.filePath(id + u".tbh"_s));
    QFile::copy(dir.filePath(base + u".json"_s), dir.filePath(id + u".json"_s));
    return id;
}

} // namespace

class TestAutosave : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"tbh-tests"_s);
        QCoreApplication::setApplicationName(u"tst_autosave"_s);
        QSettings().clear();
    }

    void init() { QDir(Autosave::recoveryDirectory()).removeRecursively(); }

    void writesOnlyUnsavedWork()
    {
        Document document;
        Autosave autosave(&document);
        QVERIFY(autosave.saveNow());
        QVERIFY(!QFile::exists(autosave.recoveryFile())); // nothing unsaved

        rename(document, u"Work in progress"_s);
        QVERIFY(autosave.saveNow());
        QVERIFY(QFile::exists(autosave.recoveryFile()));

        // Saving the harness makes the recovery file pointless: it goes away.
        QTemporaryDir dir;
        QVERIFY(document.saveAs(dir.filePath(u"saved.tbh"_s), nullptr));
        QVERIFY(!QFile::exists(autosave.recoveryFile()));
    }

    void ownSessionIsNotOffered()
    {
        Document document;
        Autosave autosave(&document);
        rename(document, u"Mine"_s);
        QVERIFY(autosave.saveNow());
        QVERIFY(Autosave::orphanedRecoveries().empty()); // this session is alive (locked)
    }

    void crashedSessionIsRecovered()
    {
        QTemporaryDir dir;
        const QString original = dir.filePath(u"bench.tbh"_s);
        RecoveryInfo info;
        {
            Document document;
            QVERIFY(document.saveAs(original, nullptr));
            Autosave autosave(&document);
            rename(document, u"Unsaved edits"_s);
            QVERIFY(autosave.saveNow());
            simulateCrashedSession(autosave);
            const auto orphans = Autosave::orphanedRecoveries();
            QCOMPARE(orphans.size(), size_t(1));
            info = orphans.front();
        }
        QCOMPARE(info.harnessName, u"Unsaved edits"_s);
        QCOMPARE(info.originalPath, original);
        QVERIFY(info.savedAt.isValid());

        Document recovered;
        QSignalSpy modified(&recovered, &Document::modifiedChanged);
        QString error;
        QVERIFY2(recovered.openRecovered(info.recoveryFile, info.originalPath, &error), qPrintable(error));
        QCOMPARE(recovered.model()->harness().info.name, u"Unsaved edits"_s);
        QCOMPARE(recovered.filePath(), original); // Save goes back to the original file
        QVERIFY(recovered.isModified());          // until it is saved
        QVERIFY(recovered.saveAs(original, nullptr));
        QVERIFY(!recovered.isModified());

        Autosave::removeRecovery(info);
        QVERIFY(Autosave::orphanedRecoveries().empty());
    }

    // A real crash: a child process autosaves and aborts, leaving a lock held by a dead
    // process. Its work must be offered for recovery.
    void realCrashIsDetected()
    {
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"TBH_AUTOSAVE_CRASH_CHILD"_s, u"1"_s);
        env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(child.waitForFinished(30000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
        const auto orphans = Autosave::orphanedRecoveries();
        QCOMPARE(orphans.size(), size_t(1));
        QCOMPARE(orphans.front().harnessName, u"Before the crash"_s);
        Autosave::removeRecovery(orphans.front());
    }

    void normalExitCleansUp()
    {
        QString file;
        {
            Document document;
            Autosave autosave(&document);
            rename(document, u"Closing"_s);
            QVERIFY(autosave.saveNow());
            file = autosave.recoveryFile();
            QVERIFY(QFile::exists(file));
        }
        QVERIFY(!QFile::exists(file));
        QVERIFY(Autosave::orphanedRecoveries().empty());
    }

    void intervalControlsTheTimer()
    {
        Document document;
        Autosave autosave(&document);
        QTimer *timer = autosave.findChild<QTimer *>();
        QVERIFY(timer);
        QVERIFY(!timer->isActive());
        autosave.setIntervalMinutes(2);
        QVERIFY(timer->isActive());
        QCOMPARE(timer->interval(), 2 * 60 * 1000);
        autosave.setIntervalMinutes(0); // off
        QVERIFY(!timer->isActive());
    }
};

static void setUpPaths()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"tbh-tests"_s);
    QCoreApplication::setApplicationName(u"tst_autosave"_s);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("TBH_AUTOSAVE_CRASH_CHILD")) {
        setUpPaths();
        Document document;
        auto *autosave = new Autosave(&document); // never destroyed: we crash
        rename(document, u"Before the crash"_s);
        autosave->saveNow();
        std::abort();
    }
    TestAutosave test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_autosave.moc"
