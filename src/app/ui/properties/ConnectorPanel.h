// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Library.h"

#include <QAbstractTableModel>
#include <QWidget>

#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableView;

namespace tbh {
struct Connector;
struct Pin;
} // namespace tbh

namespace tbh::app {

class CommitTextEdit;
class Document;

// Pins of one connector as a table; edits push undo commands.
class PinTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Wired: the far ends of the pin's wires; typing names there wires the pin (see
    // planPinConnections()). Plug is the cavity plug check box, PlugPart that plug's part number.
    enum Column { Label, Circuit, Wired, Terminal, Seal, Plug, PlugPart, Notes, ColumnCount };

    explicit PinTableModel(Document *document, QObject *parent = nullptr);

    void setConnector(const QString &id);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // A cell as text, for copying. The Plug check box is "Yes" or empty.
    QString cellText(const QModelIndex &index) const;
    // Sets several cells as one undo step. Plug check box cells are checked by any text
    // except empty, "0", "no" or "false". Returns false if nothing changed.
    bool setCellTexts(const std::vector<std::pair<QModelIndex, QString>> &cells);

    // Names a Wired cell can take: every wire end except that row's own pin.
    QStringList wiredChoices(int row) const;
    // Parts a Terminal, Seal or PlugPart cell can take: those made for the connector's
    // housing first (pinPartChoices()); empty for other columns.
    std::vector<PartChoice> partChoices(int column) const;
    QString housingPartNumber() const;

signals:
    // Typed Wired names that match no pin, splice or free end (nothing was changed).
    void connectionsNotFound(const QStringList &names);

private:
    // Applies `text` to `column` of `pin`.
    static void applyText(Pin &pin, int column, const QString &text);

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
    // Uses library housing `housing` for this connector: part number, maker, description,
    // contacts, and pins from its cavities (asks before wires would be lost). One undo step.
    // `edited` carries the other fields as typed.
    void applyHousing(Connector edited, const ConnectorPart &housing);
    void refreshHousings();
    void openPatternDialog();

    Document *m_document;
    QString m_id;
    QLineEdit *m_label;
    QLineEdit *m_partNumber;
    QCompleter *m_housings; // library housings for m_partNumber
    QLineEdit *m_manufacturer;
    QLineEdit *m_backshell;
    QLineEdit *m_strainRelief;
    QLineEdit *m_description;
    QComboBox *m_gender;
    CommitTextEdit *m_notes;
    QSpinBox *m_pinCount;
    QCheckBox *m_collapsed;
    QSpinBox *m_visibleRows;
    PinTableModel *m_pins;
    QTableView *m_table;
    QLabel *m_wiredMessage; // names typed in the Wired column that matched nothing
};

} // namespace tbh::app
