// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QComboBox;

namespace tbh::app {

// Edit > Preferences. Settings here persist across sessions.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    void accept() override;

private:
    QComboBox *m_defaultUnits;
};

} // namespace tbh::app
