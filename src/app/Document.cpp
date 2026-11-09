// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"

#include "io/TbhFile.h"

namespace tbh::app {

Document::Document(QObject *parent)
    : QObject(parent)
{
    connect(&m_undoStack, &QUndoStack::cleanChanged, this,
            [this](bool clean) { emit modifiedChanged(!clean); });
    newHarness();
}

void Document::newHarness()
{
    Harness harness;
    harness.info.name = tr("Untitled harness");
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
    return true;
}

bool Document::saveAs(const QString &path, QString *error)
{
    if (!io::writeTbhFile(path, m_model.harness(), error))
        return false;
    m_undoStack.setClean();
    setFilePath(path);
    return true;
}

void Document::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit filePathChanged();
}

} // namespace tbh::app
