// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/ConnectorPanel.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "ui/ConnectorEdits.h"
#include "ui/PinPatternDialog.h"
#include "ui/properties/PanelWidgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

namespace tbh::app {

// ---- PinTableModel --------------------------------------------------------------

PinTableModel::PinTableModel(Document *document, QObject *parent)
    : QAbstractTableModel(parent)
    , m_document(document)
{
    connect(document->model(), &HarnessModel::entityChanged, this, [this](EntityKind kind, const QString &id) {
        if (kind == EntityKind::Connector)
            onConnectorChanged(id);
    });
    connect(document->model(), &HarnessModel::harnessReset, this, [this] { setConnector({}); });
}

void PinTableModel::setConnector(const QString &id)
{
    beginResetModel();
    m_connectorId = id;
    const Connector *c = m_document->model()->find<Connector>(id);
    m_rows = c ? int(c->pins.size()) : 0;
    endResetModel();
}

void PinTableModel::onConnectorChanged(const QString &id)
{
    if (id != m_connectorId)
        return;
    const Connector *c = m_document->model()->find<Connector>(id);
    const int rows = c ? int(c->pins.size()) : 0;
    if (rows != m_rows) {
        // Keep the view in place: append/remove rows at the end.
        if (rows > m_rows) {
            beginInsertRows({}, m_rows, rows - 1);
            m_rows = rows;
            endInsertRows();
        } else {
            beginRemoveRows({}, rows, m_rows - 1);
            m_rows = rows;
            endRemoveRows();
        }
    }
    if (m_rows > 0)
        emit dataChanged(index(0, 0), index(m_rows - 1, ColumnCount - 1));
}

int PinTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows;
}

int PinTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PinTableModel::data(const QModelIndex &index, int role) const
{
    const Connector *c = m_document->model()->find<Connector>(m_connectorId);
    if (!c || index.row() >= int(c->pins.size()))
        return {};
    const Pin &pin = c->pins[size_t(index.row())];
    if (index.column() == Plug) {
        if (role == Qt::CheckStateRole)
            return pin.isCavityPlug ? Qt::Checked : Qt::Unchecked;
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    switch (index.column()) {
    case Label:
        return pin.label;
    case Circuit:
        return pin.circuitText;
    case Terminal:
        return pin.terminalPartNumber;
    case Seal:
        return pin.sealPartNumber;
    case Notes:
        return pin.notes;
    default:
        return {};
    }
}

bool PinTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const Connector *current = m_document->model()->find<Connector>(m_connectorId);
    if (!current || index.row() >= int(current->pins.size()))
        return false;
    Connector updated = *current;
    Pin &pin = updated.pins[size_t(index.row())];
    if (index.column() == Plug) {
        if (role != Qt::CheckStateRole)
            return false;
        pin.isCavityPlug = Qt::CheckState(value.toInt()) == Qt::Checked;
    } else {
        if (role != Qt::EditRole)
            return false;
        const QString text = value.toString();
        switch (index.column()) {
        case Label:
            pin.label = text;
            break;
        case Circuit:
            pin.circuitText = text;
            break;
        case Terminal:
            pin.terminalPartNumber = text;
            break;
        case Seal:
            pin.sealPartNumber = text;
            break;
        case Notes:
            pin.notes = text;
            break;
        default:
            return false;
        }
    }
    if (updated == *current)
        return false;
    m_document->undoStack()->push(
        new UpdateEntityCommand<Connector>(m_document->model(), updated, tr("Edit pin")));
    return true;
}

Qt::ItemFlags PinTableModel::flags(const QModelIndex &index) const
{
    if (index.column() == Plug)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant PinTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal && section == Seal)
        return tr("Seal part number. For a cavity plug, enter the plug's part number here.");
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    switch (section) {
    case Label:
        return tr("Pin");
    case Circuit:
        return tr("Circuit");
    case Terminal:
        return tr("Terminal P/N");
    case Seal:
        return tr("Seal P/N");
    case Plug:
        return tr("Plug");
    case Notes:
        return tr("Notes");
    default:
        return {};
    }
}

// ---- ConnectorPanel ---------------------------------------------------------------

ConnectorPanel::ConnectorPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_label(new QLineEdit(this))
    , m_partNumber(new QLineEdit(this))
    , m_manufacturer(new QLineEdit(this))
    , m_description(new QLineEdit(this))
    , m_gender(new QComboBox(this))
    , m_notes(new CommitTextEdit(this))
    , m_pinCount(new QSpinBox(this))
    , m_collapsed(new QCheckBox(tr("Collapsed"), this))
    , m_visibleRows(new QSpinBox(this))
    , m_pins(new PinTableModel(document, this))
    , m_table(new QTableView(this))
{
    m_gender->addItem(tr("None"), int(Gender::None));
    m_gender->addItem(tr("Male"), int(Gender::Male));
    m_gender->addItem(tr("Female"), int(Gender::Female));
    m_pinCount->setRange(kMinPins, kMaxPins);
    m_pinCount->setKeyboardTracking(false);
    m_visibleRows->setRange(0, kMaxPins);
    m_visibleRows->setSpecialValueText(tr("All"));
    m_visibleRows->setKeyboardTracking(false);
    m_visibleRows->setToolTip(tr("Rows shown before the connector box scrolls. Useful for large connectors."));

    auto *pattern = new QPushButton(tr("Pin labels..."), this);
    auto *pinsRow = new QHBoxLayout;
    pinsRow->addWidget(m_pinCount, 1);
    pinsRow->addWidget(pattern);
    auto *displayRow = new QHBoxLayout;
    displayRow->addWidget(m_collapsed);
    displayRow->addWidget(new QLabel(tr("Visible rows:"), this));
    displayRow->addWidget(m_visibleRows, 1);

    auto *form = new QFormLayout;
    form->addRow(tr("&Label:"), m_label);
    form->addRow(tr("&Part number:"), m_partNumber);
    form->addRow(tr("&Manufacturer:"), m_manufacturer);
    form->addRow(tr("&Description:"), m_description);
    form->addRow(tr("&Gender:"), m_gender);
    form->addRow(tr("&Notes:"), m_notes);
    form->addRow(tr("Pins:"), pinsRow);
    form->addRow(tr("Display:"), displayRow);

    m_table->setModel(m_pins);
    m_table->setAccessibleName(tr("Pins"));
    m_pinCount->setAccessibleName(tr("Pin count"));
    pattern->setAccessibleDescription(tr("Fill pin labels from a pattern such as A1-A8, B1-B8"));
    m_collapsed->setAccessibleName(tr("Collapse connector box"));
    m_visibleRows->setAccessibleName(tr("Visible rows before the connector box scrolls"));
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_table, 1);

    for (QLineEdit *edit : {m_label, m_partNumber, m_manufacturer, m_description})
        connect(edit, &QLineEdit::editingFinished, this, &ConnectorPanel::commitInfo);
    connect(m_gender, &QComboBox::activated, this, &ConnectorPanel::commitInfo);
    m_notes->onCommit = [this] { commitInfo(); };
    connect(m_pinCount, &QSpinBox::valueChanged, this, [this](int count) {
        if (!changePinCount(this, m_document, m_id, count))
            refresh();
    });
    connect(pattern, &QPushButton::clicked, this, &ConnectorPanel::openPatternDialog);
    connect(m_collapsed, &QCheckBox::clicked, this, [this](bool on) {
        emit displayChangeRequested(m_id, on, m_visibleRows->value());
    });
    connect(m_visibleRows, &QSpinBox::valueChanged, this, [this](int rows) {
        emit displayChangeRequested(m_id, m_collapsed->isChecked(), rows);
    });

    HarnessModel *model = document->model();
    connect(model, &HarnessModel::entityChanged, this, [this](EntityKind kind, const QString &id) {
        if (kind == EntityKind::Connector && id == m_id)
            refresh();
    });
    connect(model, &HarnessModel::placementChanged, this, [this](const QString &key) {
        if (key == m_id)
            refresh();
    });
}

void ConnectorPanel::setConnector(const QString &id)
{
    m_id = id;
    m_pins->setConnector(id);
    refresh();
}

void ConnectorPanel::refresh()
{
    const Connector *c = m_document->model()->find<Connector>(m_id);
    if (!c)
        return;
    const QSignalBlocker b1(m_label), b2(m_partNumber), b3(m_manufacturer), b4(m_description), b5(m_gender),
        b6(m_pinCount), b7(m_collapsed), b8(m_visibleRows);
    m_label->setText(c->label);
    m_partNumber->setText(c->partNumber);
    m_manufacturer->setText(c->manufacturer);
    m_description->setText(c->description);
    m_gender->setCurrentIndex(m_gender->findData(int(c->gender)));
    m_notes->setTextIfDifferent(c->notes);
    m_pinCount->setValue(int(c->pins.size()));
    const SchematicPlacement *p = m_document->model()->placement(m_id);
    m_collapsed->setChecked(p && p->collapsed);
    m_visibleRows->setValue(p ? p->maxVisibleRows : 0);
}

void ConnectorPanel::commitInfo()
{
    const Connector *current = m_document->model()->find<Connector>(m_id);
    if (!current)
        return;
    Connector updated = *current;
    updated.label = m_label->text();
    updated.partNumber = m_partNumber->text();
    updated.manufacturer = m_manufacturer->text();
    updated.description = m_description->text();
    updated.gender = Gender(m_gender->currentData().toInt());
    updated.notes = m_notes->toPlainText();
    if (updated != *current)
        m_document->undoStack()->push(
            new UpdateEntityCommand<Connector>(m_document->model(), updated, tr("Edit connector")));
}

void ConnectorPanel::openPatternDialog()
{
    const Connector *c = m_document->model()->find<Connector>(m_id);
    if (!c)
        return;
    PinPatternDialog dialog(int(c->pins.size()), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList labels = dialog.labels();
    std::vector<Pin> pins = c->pins;
    if (dialog.resizeToFit())
        pins = resizedPins(pins, int(labels.size()));
    for (qsizetype i = 0; i < labels.size() && i < qsizetype(pins.size()); ++i)
        pins[size_t(i)].label = labels[i];
    setPins(this, m_document, m_id, std::move(pins), tr("Apply pin labels"));
}

} // namespace tbh::app
