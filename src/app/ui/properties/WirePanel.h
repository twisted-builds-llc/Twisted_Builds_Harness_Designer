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
class FreeEndEditor;
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
        FreeEndEditor *freeEnd = nullptr; // shown when the end is a free end
    };

    EndEditor makeEndEditor(const QString &title);
    void refreshEnd(EndEditor &editor, const Wire &wire, bool endB);
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
    QComboBox *m_wireType;
    QLabel *m_outerDiameter;
    QLabel *m_length;
    QLabel *m_lengthDetail;
    void refreshLength(const std::vector<Wire> &wires);
    QLineEdit *m_lengthOverride;
    QLabel *m_overrideUnits;
    EndEditor m_endA;
    EndEditor m_endB;
};

// Kind, terminal P/N and label of one free end (flying lead, eyelet...). Used on its own
// when a free end is selected, and inside WirePanel for a wire's free ends.
class FreeEndEditor : public QWidget
{
    Q_OBJECT

public:
    explicit FreeEndEditor(Document *document, QWidget *parent = nullptr);
    void setFreeEnd(const QString &id);
    void refresh();

private:
    void commit();

    Document *m_document;
    QString m_id;
    QComboBox *m_kind;
    QLineEdit *m_terminal;
    QLineEdit *m_label;
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
