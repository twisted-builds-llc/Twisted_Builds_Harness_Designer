// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QSet>
#include <QWidget>

#include <functional>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace tbh::app {

class Document;
class GaugeEditor;
class WireColorCombo;

// Properties of one or more wires. With several wires selected, color, stripe, gauge and
// wire type apply to all of them (bulk edit); fields that differ show "(multiple)".
class WirePanel : public QWidget
{
    Q_OBJECT

public:
    explicit WirePanel(Document *document, QWidget *parent = nullptr);

    void setWires(const QSet<QString> &ids);
    void refresh();

private:
    struct EndEditor
    {
        QGroupBox *box = nullptr;
        QLabel *description = nullptr;
        QComboBox *kind = nullptr;
        QLineEdit *terminal = nullptr;
        QLineEdit *label = nullptr;
    };

    EndEditor makeEndEditor(const QString &title, bool endB);
    void refreshEnd(EndEditor &editor, const Wire &wire, bool endB);
    void commitEnd(bool endB);
    // Applies `change` to every selected wire as one undo step.
    void apply(const std::function<void(Wire &)> &change, const QString &undoText);
    void rememberStyle();
    std::vector<Wire> selectedWires() const;

    Document *m_document;
    QSet<QString> m_ids;
    QLabel *m_title;
    QLineEdit *m_name;
    WireColorCombo *m_base;
    WireColorCombo *m_stripe;
    QPushButton *m_baseCustom;
    QPushButton *m_stripeCustom;
    GaugeEditor *m_gauge;
    QLineEdit *m_wireType;
    bool m_wireTypeMixed = false;
    QLabel *m_length;
    QLabel *m_lengthDetail;
    void refreshLength(const std::vector<Wire> &wires);
    QLineEdit *m_lengthOverride;
    QLabel *m_overrideUnits;
    EndEditor m_endA;
    EndEditor m_endB;
};

class SplicePanel : public QWidget
{
    Q_OBJECT

public:
    explicit SplicePanel(Document *document, QWidget *parent = nullptr);
    void setSplice(const QString &id);
    void refresh();

private:
    void commit();

    Document *m_document;
    QString m_id;
    QLineEdit *m_label;
    QLineEdit *m_partNumber;
};

} // namespace tbh::app
