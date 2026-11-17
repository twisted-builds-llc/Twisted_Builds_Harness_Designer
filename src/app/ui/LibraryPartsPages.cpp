// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/LibraryPartsPages.h"

#include "model/PinLabels.h"
#include "model/Queries.h"
#include "ui/properties/PanelWidgets.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace tbh::app {

using namespace Qt::StringLiterals;

namespace {

// "a, b, c" <-> {"a", "b", "c"}; empty items dropped.
QString joinList(const QStringList &list)
{
    return list.join(u", "_s);
}

QStringList splitList(const QString &text)
{
    QStringList list;
    for (const QString &item : text.split(u',', Qt::SkipEmptyParts)) {
        if (!item.trimmed().isEmpty())
            list << item.trimmed();
    }
    return list;
}

QString number(std::optional<double> value)
{
    return value ? QLocale().toString(*value, 'g', 6) : QString();
}

// Empty text: nullopt, ok. Otherwise a positive number or not ok.
std::optional<double> parseOptionalNumber(const QString &text, bool *ok)
{
    *ok = true;
    if (text.trimmed().isEmpty())
        return std::nullopt;
    double value = QLocale().toDouble(text.trimmed(), ok);
    if (!*ok)
        value = QLocale::c().toDouble(text.trimmed(), ok);
    if (!*ok || value <= 0) {
        *ok = false;
        return std::nullopt;
    }
    return value;
}

QString pinPartKindName(PinPartKind kind)
{
    switch (kind) {
    case PinPartKind::Seal:
        return PinPartsPage::tr("Seal");
    case PinPartKind::Plug:
        return PinPartsPage::tr("Plug");
    case PinPartKind::Terminal:
        break;
    }
    return PinPartsPage::tr("Terminal");
}

bool matchesFilter(const QString &filter, const QStringList &fields)
{
    if (filter.trimmed().isEmpty())
        return true;
    for (const QString &field : fields) {
        if (field.contains(filter.trimmed(), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace

// ---- ConnectorPartsPage -----------------------------------------------------------

ConnectorPartsPage::ConnectorPartsPage(QWidget *parent)
    : QWidget(parent)
    , m_filter(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_form(new QWidget(this))
    , m_partNumber(new QLineEdit(m_form))
    , m_manufacturer(new QLineEdit(m_form))
    , m_family(new QLineEdit(m_form))
    , m_description(new QLineEdit(m_form))
    , m_gender(new QComboBox(m_form))
    , m_cavities(new QLineEdit(m_form))
    , m_cavityCount(new QLabel(m_form))
    , m_terminals(new QLineEdit(m_form))
    , m_seals(new QLineEdit(m_form))
    , m_plugs(new QLineEdit(m_form))
{
    m_filter->setPlaceholderText(tr("Filter by part number, family, maker..."));
    m_filter->setClearButtonEnabled(true);
    m_filter->setAccessibleName(tr("Filter connector housings"));
    m_list->setAccessibleName(tr("Connector housings"));
    m_gender->addItem(tr("None"), int(Gender::None));
    m_gender->addItem(tr("Male (pins)"), int(Gender::Male));
    m_gender->addItem(tr("Female (sockets)"), int(Gender::Female));
    m_partNumber->setAccessibleName(tr("Housing part number"));
    m_manufacturer->setAccessibleName(tr("Housing manufacturer"));
    m_family->setAccessibleName(tr("Housing family"));
    m_description->setAccessibleName(tr("Housing description"));
    m_gender->setAccessibleName(tr("Housing contacts"));
    m_cavities->setAccessibleName(tr("Cavity labels"));
    m_terminals->setAccessibleName(tr("Compatible terminals"));
    m_seals->setAccessibleName(tr("Compatible seals"));
    m_plugs->setAccessibleName(tr("Compatible cavity plugs"));
    m_cavities->setToolTip(tr("Cavity labels in order, as a pattern: \"1-12\", \"A1-A8, B1-B8\" or a list."));
    for (QLineEdit *edit : {m_terminals, m_seals, m_plugs})
        edit->setToolTip(tr("Part numbers made for this housing, separated by commas."));

    auto *add = new QPushButton(tr("Add"), this);
    auto *duplicate = new QPushButton(tr("Duplicate"), this);
    auto *remove = new QPushButton(tr("Remove"), this);
    duplicate->setToolTip(tr("Copy the selected housing, e.g. for another keying or color."));
    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_filter);
    leftLayout->addWidget(m_list, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(duplicate);
    buttons->addWidget(remove);
    leftLayout->addLayout(buttons);

    auto *cavityRow = new QHBoxLayout;
    cavityRow->addWidget(m_cavities, 1);
    cavityRow->addWidget(m_cavityCount);
    auto *form = new QFormLayout(m_form);
    form->addRow(tr("Part number:"), m_partNumber);
    form->addRow(tr("Manufacturer:"), m_manufacturer);
    form->addRow(tr("Family:"), m_family);
    form->addRow(tr("Description:"), m_description);
    form->addRow(tr("Contacts:"), m_gender);
    form->addRow(tr("Cavities:"), cavityRow);
    form->addRow(tr("Terminals:"), m_terminals);
    form->addRow(tr("Seals:"), m_seals);
    form->addRow(tr("Cavity plugs:"), m_plugs);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(m_form);
    splitter->setSizes({320, 460});
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(m_filter, &QLineEdit::textChanged, this, &ConnectorPartsPage::applyFilter);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        QString error;
        if (!storeCurrent(&error)) {
            // Keep the invalid entry selected so it can be fixed.
            const QSignalBlocker blocker(m_list);
            m_list->setCurrentRow(m_current);
            m_cavityCount->setText(error);
            return;
        }
        show(row);
    });
    connect(m_cavities, &QLineEdit::textChanged, this, [this](const QString &text) {
        QString error;
        const auto labels = expandPinPattern(text, 1, false, &error);
        m_cavityCount->setText(labels ? tr("%n cavity(s)", nullptr, int(labels->size())) : error);
    });
    connect(add, &QPushButton::clicked, this, [this] {
        ConnectorPart part;
        part.partNumber = tr("NEW-PART");
        part.cavityLabels = {u"1"_s, u"2"_s};
        addPart(part);
    });
    connect(duplicate, &QPushButton::clicked, this, [this] {
        QString error;
        if (m_current < 0 || !storeCurrent(&error))
            return;
        ConnectorPart copy = m_parts[size_t(m_current)];
        copy.partNumber += tr(" (copy)");
        copy.approximate = false;
        addPart(copy);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_current < 0)
            return;
        m_parts.erase(m_parts.begin() + m_current);
        m_current = -1;
        reloadList(-1);
    });
    show(-1);
}

void ConnectorPartsPage::addPart(ConnectorPart part)
{
    QString error;
    if (!storeCurrent(&error))
        return;
    m_filter->clear();
    m_parts.push_back(std::move(part));
    m_current = -1;
    reloadList(int(m_parts.size()) - 1);
    m_partNumber->setFocus();
    m_partNumber->selectAll();
}

void ConnectorPartsPage::load(const std::vector<ConnectorPart> &parts)
{
    m_parts = parts;
    m_current = -1;
    reloadList(m_parts.empty() ? -1 : 0);
}

void ConnectorPartsPage::reloadList(int select)
{
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
        for (const ConnectorPart &p : m_parts) {
            QString text = p.partNumber;
            const QString detail = QStringList{p.family, p.description}.filter(QRegularExpression(u"\\S"_s)).join(u", "_s);
            if (!detail.isEmpty())
                text += u" — "_s + detail;
            if (p.approximate)
                text = tr("%1 (approx.)").arg(text);
            m_list->addItem(text);
        }
        m_list->setCurrentRow(select);
    }
    applyFilter();
    show(select);
}

void ConnectorPartsPage::applyFilter()
{
    for (int row = 0; row < m_list->count(); ++row) {
        const ConnectorPart &p = m_parts[size_t(row)];
        m_list->item(row)->setHidden(!matchesFilter(m_filter->text(),
                                                    {p.partNumber, p.family, p.manufacturer, p.description}));
    }
}

void ConnectorPartsPage::show(int index)
{
    m_current = index >= 0 && index < int(m_parts.size()) ? index : -1;
    m_form->setEnabled(m_current >= 0);
    const ConnectorPart part = m_current >= 0 ? m_parts[size_t(m_current)] : ConnectorPart{};
    m_partNumber->setText(part.partNumber);
    m_manufacturer->setText(part.manufacturer);
    m_family->setText(part.family);
    m_description->setText(part.description);
    m_gender->setCurrentIndex(m_gender->findData(int(part.gender)));
    m_cavities->setText(compressPinLabels(part.cavityLabels));
    m_terminals->setText(joinList(part.terminals));
    m_seals->setText(joinList(part.seals));
    m_plugs->setText(joinList(part.plugs));
}

bool ConnectorPartsPage::storeCurrent(QString *error)
{
    if (m_current < 0)
        return true;
    ConnectorPart &part = m_parts[size_t(m_current)];
    const QString partNumber = m_partNumber->text().trimmed();
    if (partNumber.isEmpty()) {
        *error = tr("A connector housing needs a part number.");
        return false;
    }
    QString patternError;
    const auto labels = expandPinPattern(m_cavities->text(), 1, false, &patternError);
    if (!labels) {
        *error = tr("%1: cavities: %2").arg(partNumber, patternError);
        return false;
    }
    part.partNumber = partNumber;
    part.manufacturer = m_manufacturer->text().trimmed();
    part.family = m_family->text().trimmed();
    part.description = m_description->text().trimmed();
    part.gender = Gender(m_gender->currentData().toInt());
    part.cavityLabels = *labels;
    part.terminals = splitList(m_terminals->text());
    part.seals = splitList(m_seals->text());
    part.plugs = splitList(m_plugs->text());
    return true;
}

bool ConnectorPartsPage::store(std::vector<ConnectorPart> *parts, QString *error)
{
    if (!storeCurrent(error))
        return false;
    QSet<QString> seen;
    for (const ConnectorPart &p : m_parts) {
        if (seen.contains(p.partNumber)) {
            *error = tr("Connector part number %1 is listed twice.").arg(p.partNumber);
            return false;
        }
        seen.insert(p.partNumber);
    }
    *parts = m_parts;
    return true;
}

// ---- PinPartsPage -----------------------------------------------------------------

PinPartsPage::PinPartsPage(QWidget *parent)
    : QWidget(parent)
    , m_filter(new QLineEdit(this))
    , m_table(new QTableWidget(0, ColumnCount, this))
{
    m_filter->setPlaceholderText(tr("Filter by part number, maker, description..."));
    m_filter->setClearButtonEnabled(true);
    m_filter->setAccessibleName(tr("Filter terminals, seals and plugs"));
    m_table->setAccessibleName(tr("Terminals, seals and plugs"));
    m_table->setHorizontalHeaderLabels({tr("Kind"), tr("Part number"), tr("Manufacturer"), tr("Description"),
                                        tr("Wire from"), tr("Wire to"), tr("Insulation Ø min (mm)"),
                                        tr("Insulation Ø max (mm)"), QString()});
    m_table->horizontalHeaderItem(WireMin)->setToolTip(
        tr("Smallest and largest conductor the terminal takes, e.g. \"20 AWG\" and \"16 AWG\", or "
           "\"0.5 mm²\". A number without a unit is AWG."));
    m_table->horizontalHeaderItem(WireMax)->setToolTip(m_table->horizontalHeaderItem(WireMin)->toolTip());
    m_table->horizontalHeaderItem(InsulationMin)->setToolTip(tr("Insulation outer diameter range the "
                                                                "terminal or seal takes."));
    m_table->horizontalHeader()->setSectionResizeMode(Description, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *add = new QPushButton(tr("Add"), this);
    auto *remove = new QPushButton(tr("Remove"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, &PinPartsPage::applyFilter);
    connect(add, &QPushButton::clicked, this, [this] {
        m_filter->clear();
        PinPart part;
        part.partNumber = tr("NEW-PART");
        addRow(part, -1);
        m_table->setCurrentCell(m_table->rowCount() - 1, PartNumber);
        m_table->scrollToBottom();
        m_table->editItem(m_table->item(m_table->rowCount() - 1, PartNumber));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        for (const QModelIndex &index : m_table->selectionModel()->selectedRows())
            rows << index.row();
        std::sort(rows.begin(), rows.end(), std::greater<>());
        for (int row : rows)
            m_table->removeRow(row);
    });
}

void PinPartsPage::load(const std::vector<PinPart> &parts)
{
    m_loaded = parts;
    m_table->setRowCount(0);
    for (size_t i = 0; i < m_loaded.size(); ++i)
        addRow(m_loaded[i], int(i));
    applyFilter();
    m_table->resizeColumnsToContents();
    m_table->resizeRowsToContents();
}

void PinPartsPage::addRow(const PinPart &p, int loaded)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *kind = new QComboBox;
    for (PinPartKind k : {PinPartKind::Terminal, PinPartKind::Seal, PinPartKind::Plug})
        kind->addItem(pinPartKindName(k), int(k));
    kind->setCurrentIndex(kind->findData(int(p.kind)));
    m_table->setCellWidget(row, Kind, kind);
    auto *partNumber = new QTableWidgetItem(p.partNumber);
    partNumber->setData(Qt::UserRole, loaded);
    m_table->setItem(row, PartNumber, partNumber);
    m_table->setItem(row, Manufacturer, new QTableWidgetItem(p.manufacturer));
    m_table->setItem(row, Description, new QTableWidgetItem(p.description));
    m_table->setItem(row, WireMin, new QTableWidgetItem(p.wireMin ? gaugeText(*p.wireMin) : QString()));
    m_table->setItem(row, WireMax, new QTableWidgetItem(p.wireMax ? gaugeText(*p.wireMax) : QString()));
    m_table->setItem(row, InsulationMin, new QTableWidgetItem(number(p.insulationMinMm)));
    m_table->setItem(row, InsulationMax, new QTableWidgetItem(number(p.insulationMaxMm)));
    auto *approx = new QTableWidgetItem(p.approximate ? tr("approx.") : QString());
    approx->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(row, Approximate, approx);
}

void PinPartsPage::applyFilter()
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QStringList fields;
        for (int column : {PartNumber, Manufacturer, Description})
            fields << m_table->item(row, column)->text();
        fields << static_cast<QComboBox *>(m_table->cellWidget(row, Kind))->currentText();
        m_table->setRowHidden(row, !matchesFilter(m_filter->text(), fields));
    }
}

bool PinPartsPage::store(std::vector<PinPart> *parts, QString *error)
{
    std::vector<PinPart> result;
    QSet<QString> seen;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto text = [&](int column) { return m_table->item(row, column)->text().trimmed(); };
        const int loaded = m_table->item(row, PartNumber)->data(Qt::UserRole).toInt();
        PinPart p;
        if (loaded >= 0 && loaded < int(m_loaded.size())) {
            p.extra = m_loaded[size_t(loaded)].extra;
            p.approximate = m_loaded[size_t(loaded)].approximate;
        }
        p.kind = PinPartKind(static_cast<QComboBox *>(m_table->cellWidget(row, Kind))->currentData().toInt());
        p.partNumber = text(PartNumber);
        p.manufacturer = text(Manufacturer);
        p.description = text(Description);
        if (p.partNumber.isEmpty()) {
            *error = tr("Terminals, seals and plugs, row %1: enter a part number.").arg(row + 1);
            return false;
        }
        const QString key = QString::number(int(p.kind)) + u'/' + p.partNumber;
        if (seen.contains(key)) {
            *error = tr("%1 %2 is listed twice.").arg(pinPartKindName(p.kind), p.partNumber);
            return false;
        }
        seen.insert(key);
        for (const auto &[column, gauge] : {std::pair{WireMin, &p.wireMin}, std::pair{WireMax, &p.wireMax}}) {
            if (text(column).isEmpty())
                continue;
            *gauge = parseGauge(text(column));
            if (!*gauge) {
                *error = tr("%1: \"%2\" is not a wire size such as 18 AWG or 0.75 mm².").arg(p.partNumber, text(column));
                return false;
            }
        }
        bool okMin = false, okMax = false;
        p.insulationMinMm = parseOptionalNumber(text(InsulationMin), &okMin);
        p.insulationMaxMm = parseOptionalNumber(text(InsulationMax), &okMax);
        if (!okMin || !okMax) {
            *error = tr("%1: insulation diameters must be positive numbers in millimeters.").arg(p.partNumber);
            return false;
        }
        result.push_back(std::move(p));
    }
    *parts = std::move(result);
    return true;
}

} // namespace tbh::app
