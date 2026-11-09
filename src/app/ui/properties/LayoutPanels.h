// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace tbh::app {

class Document;

// One layout segment: entered length, label, and the wires routed through it.
class SegmentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentPanel(Document *document, QWidget *parent = nullptr);
    void setSegment(const QString &id);
    void refresh();

private:
    void commit();

    Document *m_document;
    QString m_id;
    QLineEdit *m_length;
    QLabel *m_units;
    QLineEdit *m_label;
    QLabel *m_wireCount;
    QListWidget *m_wires;
};

// One layout node: what it stands for (connector, splice, wire end, or a branch point).
class LayoutNodePanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayoutNodePanel(Document *document, QWidget *parent = nullptr);
    void setNode(const QString &id);
    void refresh();

private:
    void commit();

    Document *m_document;
    QString m_id;
    QComboBox *m_ref;
    QLabel *m_segments;
};

} // namespace tbh::app
