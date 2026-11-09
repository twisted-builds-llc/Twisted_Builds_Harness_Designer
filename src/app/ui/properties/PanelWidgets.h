// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Small editor widgets shared by the properties panels.

#include "model/Harness.h"

#include <QComboBox>
#include <QPlainTextEdit>

#include <functional>

namespace tbh::app {

// Multi-line text that commits when it loses focus.
class CommitTextEdit : public QPlainTextEdit
{
public:
    explicit CommitTextEdit(QWidget *parent = nullptr);
    std::function<void()> onCommit;
    // Sets text without moving the cursor if unchanged.
    void setTextIfDifferent(const QString &text);

protected:
    void focusOutEvent(QFocusEvent *event) override;
};

// Editable wire-color picker: standard codes with swatches, or any #rrggbb.
// With allowNone, the first entry is "None" (empty value).
class WireColorCombo : public QComboBox
{
    Q_OBJECT

public:
    WireColorCombo(bool allowNone, QWidget *parent = nullptr);
    // Stored value for the current text: a code, "#rrggbb", "" (none). nullopt if invalid.
    std::optional<QString> value() const;
    void setValue(const QString &value);
    // Shows an empty field with a "(multiple)" placeholder.
    void setMixed();

private:
    bool m_allowNone;
};

// Gauge value (editable, with standard sizes) plus system (AWG / mm²).
class GaugeEditor : public QWidget
{
    Q_OBJECT

public:
    explicit GaugeEditor(QWidget *parent = nullptr);
    std::optional<Gauge> value() const;
    void setValue(const Gauge &gauge);
    void setMixed();
    std::function<void()> onCommit;

private:
    void fillValues();

    QComboBox *m_value;
    QComboBox *m_system;
};

QString gaugeText(const Gauge &gauge);

} // namespace tbh::app
