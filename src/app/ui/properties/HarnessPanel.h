// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QWidget>

#include <map>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace tbh::app {

class CommitTextEdit;
class Document;

// Harness-level info and settings (shown when nothing is selected). Every committed
// change is pushed as an undo command.
class HarnessPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HarnessPanel(Document *document, QWidget *parent = nullptr);

private:
    void refresh();
    void commitInfo();
    void commitSettings();

    Document *m_document;
    QLineEdit *m_name;
    QLineEdit *m_partNumber;
    QLineEdit *m_revision;
    QLineEdit *m_author;
    QComboBox *m_units;
    CommitTextEdit *m_description;

    QDoubleSpinBox *m_serviceLoop;
    QLineEdit *m_rounding;
    QDoubleSpinBox *m_wasteWire;
    QDoubleSpinBox *m_wasteSleeving;
    QDoubleSpinBox *m_packing;
    QDoubleSpinBox *m_sleevingMargin;
    std::map<EndType, QLineEdit *> m_allowances;
    std::vector<QLabel *> m_unitLabels;
};

} // namespace tbh::app
