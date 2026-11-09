// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace tbh::app {

// Fills pin labels in bulk from a pattern such as "A1-A8, B1-B8, C1-C12".
class PinPatternDialog : public QDialog
{
    Q_OBJECT

public:
    PinPatternDialog(int currentPinCount, QWidget *parent = nullptr);

    QStringList labels() const { return m_labels; }
    // Whether the connector should get exactly labels().size() pins.
    bool resizeToFit() const;

private:
    void updatePreview();

    int m_currentPinCount;
    QStringList m_labels;
    QLineEdit *m_pattern;
    QSpinBox *m_step;
    QCheckBox *m_reverse;
    QCheckBox *m_resize;
    QLabel *m_preview;
    QDialogButtonBox *m_buttons;
};

} // namespace tbh::app
