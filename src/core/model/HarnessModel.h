// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QObject>

namespace tbh {

// Owns the Harness being edited. Views read from it and react to its signals;
// they never mutate it directly but push QUndoCommands that call the mutators.
class HarnessModel : public QObject
{
    Q_OBJECT

public:
    explicit HarnessModel(QObject *parent = nullptr);

    const Harness &harness() const { return m_harness; }

    // Replace the whole harness (new/open). Not undoable.
    void reset(Harness harness);

    void setInfo(const HarnessInfo &info);

signals:
    void harnessReset();
    void infoChanged();

private:
    Harness m_harness;
};

} // namespace tbh
