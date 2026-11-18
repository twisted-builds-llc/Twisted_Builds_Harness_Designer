// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace tbh::app {

class CommitTextEdit;
class Document;

// Properties of one cable: label, twist, built or bought, part numbers, jacket diameter,
// its wires, and its shield wire (added from here, see addShieldRequested()).
class CablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit CablePanel(Document *document, QWidget *parent = nullptr);
    void setCable(const QString &id);
    void refresh();

signals:
    // Add Shield Wire was clicked: the schematic places the shield wire's loose ends.
    void addShieldRequested(const QString &cableId);

private:
    void commit();

    Document *m_document;
    QString m_id;
    QLineEdit *m_label;
    QCheckBox *m_twisted;
    QLineEdit *m_layLength;
    QLabel *m_layUnits;
    QComboBox *m_source;
    QLineEdit *m_partNumber;
    QLineEdit *m_jacket;
    QLabel *m_jacketUnits;
    QLabel *m_wires;
    QLabel *m_shield;
    QPushButton *m_addShield;
    QPushButton *m_removeShield;
    QLineEdit *m_shieldPartNumber;
    CommitTextEdit *m_notes;
};

} // namespace tbh::app
