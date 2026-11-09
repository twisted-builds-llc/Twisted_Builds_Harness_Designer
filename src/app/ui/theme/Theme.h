// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace tbh::app {

// Applies the app's own light/dark palettes on the Fusion style. "System" follows the
// OS color scheme and re-applies when it changes. Choice persists in QSettings.
class Theme : public QObject
{
    Q_OBJECT

public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)

    explicit Theme(QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

private:
    void apply();

    Mode m_mode = Mode::System;
};

} // namespace tbh::app
