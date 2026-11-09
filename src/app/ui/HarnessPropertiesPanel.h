// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

namespace tbh::app {

class Document;

// Edits harness-level info. Every committed change is pushed as an undo command.
class HarnessPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HarnessPropertiesPanel(Document *document, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void commit();

    Document *m_document;
    QLineEdit *m_name;
    QLineEdit *m_revision;
    QLineEdit *m_author;
    QComboBox *m_units;
    QPlainTextEdit *m_description;
};

} // namespace tbh::app
