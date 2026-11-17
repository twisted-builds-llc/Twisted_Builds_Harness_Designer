// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Library dialog tabs for connector parts: housings, and the terminals, seals and cavity
// plugs that go in them. Each page edits a copy; store() validates and hands it back.

#include "model/Harness.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace tbh::app {

// Housings: a filterable list on the left, the selected housing's fields on the right.
class ConnectorPartsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectorPartsPage(QWidget *parent = nullptr);

    void load(const std::vector<ConnectorPart> &parts);
    // The edited housings. False, with `error`, if a field is invalid.
    bool store(std::vector<ConnectorPart> *parts, QString *error);

private:
    void reloadList(int select);
    void applyFilter();
    void show(int index);
    bool storeCurrent(QString *error);
    void addPart(ConnectorPart part);

    std::vector<ConnectorPart> m_parts;
    int m_current = -1;
    QLineEdit *m_filter;
    QListWidget *m_list;
    QWidget *m_form;
    QLineEdit *m_partNumber;
    QLineEdit *m_manufacturer;
    QLineEdit *m_family;
    QLineEdit *m_description;
    QComboBox *m_gender;
    QLineEdit *m_cavities;
    QLabel *m_cavityCount;
    QLineEdit *m_terminals;
    QLineEdit *m_seals;
    QLineEdit *m_plugs;
};

// Terminals, seals and plugs in one table, filterable.
class PinPartsPage : public QWidget
{
    Q_OBJECT

public:
    enum Column { Kind, PartNumber, Manufacturer, Description, WireMin, WireMax, InsulationMin,
                  InsulationMax, Approximate, ColumnCount };

    explicit PinPartsPage(QWidget *parent = nullptr);

    void load(const std::vector<PinPart> &parts);
    bool store(std::vector<PinPart> *parts, QString *error);

private:
    // `loaded`: index in m_loaded the row starts from, -1 for a new part.
    void addRow(const PinPart &part, int loaded);
    void applyFilter();

    std::vector<PinPart> m_loaded; // as loaded: each row's extra data and approximate flag
    QLineEdit *m_filter;
    QTableWidget *m_table;
};

} // namespace tbh::app
