// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

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
    QSpinBox *m_autosave;
    QComboBox *m_panInput;
    QCheckBox *m_wheelPans;
};

} // namespace tbh::app
