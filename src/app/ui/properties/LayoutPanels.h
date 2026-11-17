// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace tbh::app {

class CommitTextEdit;
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
    QLabel *m_bundle;
    QLabel *m_bundleNote;
    QComboBox *m_family;
    QComboBox *m_size;
    QLabel *m_fit;
    QLabel *m_recommendation;
    QPushButton *m_useRecommended;
    // Family the user picked (may differ from the assigned sleeving until a size is chosen).
    // nullopt: not picked, use the assigned sleeving's family or the first one; "": none.
    std::optional<QString> m_familyChoice;
    QString m_recommendedId;

    void assignSleeving(const QString &sleevingId);
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

// One label or tie on the layout: text, part number, protective cover, where it sits.
class AccessoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AccessoryPanel(Document *document, QWidget *parent = nullptr);
    void setAccessory(const QString &id);
    void refresh();

private:
    void commit();

    Document *m_document;
    QString m_id;
    QComboBox *m_kind;
    QLineEdit *m_text;
    QLineEdit *m_partNumber;
    QComboBox *m_cover;
    QLineEdit *m_coverLength;
    QLabel *m_coverUnits;
    QLineEdit *m_distance;
    QLabel *m_distanceUnits;
    QLabel *m_distanceNote;
    CommitTextEdit *m_notes;
    QWidget *m_labelFields; // text and cover rows, labels only
};

} // namespace tbh::app
