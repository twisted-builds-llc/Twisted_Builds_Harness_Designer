// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractTableModel>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableView;

namespace tbh::app {

class CommitTextEdit;
class Document;

// Pins of one connector as a table; edits push undo commands.
class PinTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { Label, Circuit, Terminal, Seal, Plug, Notes, ColumnCount };

    explicit PinTableModel(Document *document, QObject *parent = nullptr);

    void setConnector(const QString &id);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void onConnectorChanged(const QString &id);

    Document *m_document;
    QString m_connectorId;
    int m_rows = 0;
};

// Properties of one connector: fields, pin count, pin label pattern, display, pin table.
class ConnectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectorPanel(Document *document, QWidget *parent = nullptr);

    void setConnector(const QString &id);
    void refresh();

signals:
    // Collapse / scroll settings live in the schematic placement, owned by the view.
    void displayChangeRequested(const QString &connectorId, bool collapsed, int maxVisibleRows);

private:
    void commitInfo();
    void openPatternDialog();

    Document *m_document;
    QString m_id;
    QLineEdit *m_label;
    QLineEdit *m_partNumber;
    QLineEdit *m_manufacturer;
    QLineEdit *m_description;
    QComboBox *m_gender;
    CommitTextEdit *m_notes;
    QSpinBox *m_pinCount;
    QCheckBox *m_collapsed;
    QSpinBox *m_visibleRows;
    PinTableModel *m_pins;
    QTableView *m_table;
};

} // namespace tbh::app
