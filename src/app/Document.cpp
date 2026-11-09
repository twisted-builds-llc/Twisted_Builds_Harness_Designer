// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"

#include "Preferences.h"
#include "io/TbhFile.h"
#include "model/Library.h"

namespace tbh::app {

Document::Document(QObject *parent)
    : QObject(parent)
{
    connect(&m_undoStack, &QUndoStack::cleanChanged, this,
            [this](bool clean) { emit modifiedChanged(!clean || m_recovered); });
    newHarness();
}

void Document::newHarness()
{
    setRecovered(false);
    Harness harness;
    harness.info.name = tr("Untitled harness");
    harness.info.units = preferences::defaultUnits();
    harness.createdAt = QDateTime::currentDateTimeUtc();
    m_undoStack.clear();
    m_model.reset(std::move(harness));
    setFilePath({});
}

bool Document::open(const QString &path, QString *error)
{
    auto harness = io::readTbhFile(path, error);
    if (!harness)
        return false;
    m_undoStack.clear();
    m_model.reset(std::move(*harness));
    setFilePath(path);
    setRecovered(false);
    return true;
}

bool Document::openRecovered(const QString &recoveryPath, const QString &originalPath, QString *error)
{
    if (!open(recoveryPath, error))
        return false;
    setFilePath(originalPath);
    setRecovered(true);
    return true;
}

bool Document::writeCopy(const QString &path, QString *error) const
{
    // Embed the library entries this harness uses so it opens the same elsewhere.
    Harness harness = m_model.harness();
    harness.library = usedLibraryEntries(harness, m_model.library());
    return io::writeTbhFile(path, harness, error);
}

bool Document::saveAs(const QString &path, QString *error)
{
    if (!writeCopy(path, error))
        return false;
    m_undoStack.setClean();
    setFilePath(path);
    setRecovered(false);
    return true;
}

void Document::setRecovered(bool recovered)
{
    if (m_recovered == recovered)
        return;
    const bool wasModified = isModified();
    m_recovered = recovered;
    if (isModified() != wasModified)
        emit modifiedChanged(isModified());
}

void Document::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit filePathChanged();
}

} // namespace tbh::app
