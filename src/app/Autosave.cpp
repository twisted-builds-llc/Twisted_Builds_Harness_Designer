// SPDX-License-Identifier: GPL-3.0-or-later
#include "Autosave.h"

#include "Document.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace tbh::app {

namespace {

QString pathFor(const QString &id, const char *suffix)
{
    return QDir(Autosave::recoveryDirectory()).filePath(id + QLatin1StringView(suffix));
}

} // namespace

Autosave::Autosave(Document *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_timer(new QTimer(this))
{
    QDir().mkpath(recoveryDirectory());
    m_lock = std::make_unique<QLockFile>(pathFor(m_id, ".lock"));
    m_lock->setStaleLockTime(0); // a lock is stale only when its process is gone
    m_lock->tryLock(0);

    connect(m_timer, &QTimer::timeout, this, [this] { saveNow(); });
    // Saved, new or opened: the recovery file no longer holds anything unsaved.
    connect(document, &Document::modifiedChanged, this, [this](bool modified) {
        if (!modified)
            discard();
    });
}

Autosave::~Autosave()
{
    discard();
    m_lock->unlock();
}

void Autosave::setIntervalMinutes(int minutes)
{
    if (minutes <= 0)
        m_timer->stop();
    else
        m_timer->start(minutes * 60 * 1000);
}

QString Autosave::recoveryFile() const
{
    return pathFor(m_id, ".tbh");
}

bool Autosave::saveNow(QString *error)
{
    if (!m_document->isModified()) {
        discard();
        return true;
    }
    if (!m_document->writeCopy(recoveryFile(), error))
        return false;
    const QJsonObject meta{
        {QStringLiteral("originalPath"), m_document->filePath()},
        {QStringLiteral("harnessName"), m_document->model()->harness().info.name},
        {QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    };
    QSaveFile file(pathFor(m_id, ".json"));
    const QByteArray data = QJsonDocument(meta).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void Autosave::discard()
{
    QFile::remove(pathFor(m_id, ".json"));
    QFile::remove(recoveryFile());
}

QString Autosave::recoveryDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recovery");
}

std::vector<RecoveryInfo> Autosave::orphanedRecoveries()
{
    std::vector<RecoveryInfo> result;
    const QDir dir(recoveryDirectory());
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const QString id = name.chopped(5);
        QLockFile lock(pathFor(id, ".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue; // its session is still running
        lock.unlock();
        if (!QFile::exists(pathFor(id, ".tbh")))
            continue;
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject meta = QJsonDocument::fromJson(file.readAll()).object();
        result.push_back(RecoveryInfo{id, pathFor(id, ".tbh"), meta.value(QStringLiteral("originalPath")).toString(),
                                      meta.value(QStringLiteral("harnessName")).toString(),
                                      QDateTime::fromString(meta.value(QStringLiteral("savedAt")).toString(),
                                                            Qt::ISODateWithMs)});
    }
    std::sort(result.begin(), result.end(),
              [](const RecoveryInfo &a, const RecoveryInfo &b) { return a.savedAt > b.savedAt; });
    return result;
}

void Autosave::removeRecovery(const RecoveryInfo &info)
{
    QFile::remove(pathFor(info.id, ".json"));
    QFile::remove(pathFor(info.id, ".tbh"));
    QFile::remove(pathFor(info.id, ".lock"));
}

} // namespace tbh::app
