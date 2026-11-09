// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/HarnessModel.h"

#include <QObject>
#include <QUndoStack>

namespace tbh::app {

// Style given to newly drawn wires: the last style the user picked this session.
struct WireStyle {
    QString baseColor = QStringLiteral("BLK");
    QString stripeColor;
    Gauge gauge{18, GaugeSystem::Awg};
    QString wireTypeId;
};

// One open harness: model, undo history and file path. Modified state is the
// undo stack's clean state.
class Document : public QObject
{
    Q_OBJECT

public:
    explicit Document(QObject *parent = nullptr);

    HarnessModel *model() { return &m_model; }
    QUndoStack *undoStack() { return &m_undoStack; }

    QString filePath() const { return m_filePath; }
    bool isModified() const { return !m_undoStack.isClean(); }

    void newHarness();
    bool open(const QString &path, QString *error);
    bool saveAs(const QString &path, QString *error);

    const WireStyle &wireStyle() const { return m_wireStyle; }
    void setWireStyle(const WireStyle &style) { m_wireStyle = style; }

signals:
    void filePathChanged();
    void modifiedChanged(bool modified);

private:
    void setFilePath(const QString &path);

    HarnessModel m_model;
    QUndoStack m_undoStack;
    QString m_filePath;
    WireStyle m_wireStyle;
};

} // namespace tbh::app
