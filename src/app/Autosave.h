// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QLockFile>
#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace tbh::app {

class Document;

// Unsaved work left behind by a session that is no longer running (e.g. after a crash).
struct RecoveryInfo
{
    QString id;
    QString recoveryFile; // .tbh with the unsaved state
    QString originalPath; // where the harness was saved before, or empty
    QString harnessName;
    QDateTime savedAt;
};

// Periodically writes unsaved changes to a per-session recovery file in the app data
// folder. Each session holds a lock file, so recovery files are only offered when their
// session is gone. The recovery file is removed when the work is saved or discarded, and
// when the session ends normally.
class Autosave : public QObject
{
    Q_OBJECT

public:
    explicit Autosave(Document *document, QObject *parent = nullptr);
    ~Autosave() override;

    // 0 turns periodic autosave off.
    void setIntervalMinutes(int minutes);
    // Writes the recovery file now if there are unsaved changes; removes it if not.
    bool saveNow(QString *error = nullptr);
    QString recoveryFile() const;

    static QString recoveryDirectory();
    // Recovery files of sessions that are no longer running, newest first.
    static std::vector<RecoveryInfo> orphanedRecoveries();
    static void removeRecovery(const RecoveryInfo &info);

private:
    void discard();

    Document *m_document;
    QString m_id;
    std::unique_ptr<QLockFile> m_lock;
    QTimer *m_timer;
};

} // namespace tbh::app
