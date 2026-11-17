// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/ConnectorPanel.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "commands/UndoBatch.h"
#include "model/Queries.h"
#include "ui/ConnectorEdits.h"
#include "ui/PinPatternDialog.h"
#include "ui/properties/PanelWidgets.h"

#include <QCheckBox>
#include <QClipboard>
#include <QAbstractItemView>
#include <QCompleter>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QStyledItemDelegate>
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
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

namespace tbh::app {

using namespace Qt::StringLiterals;

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
    // The Wired column shows wires and the labels of what they go to.
    auto refreshWired = [this] {
        if (m_rows > 0)
            emit dataChanged(index(0, Wired), index(m_rows - 1, Wired));
    };
    connect(document->model(), &HarnessModel::entityAdded, this, refreshWired);
    connect(document->model(), &HarnessModel::entityRemoved, this, refreshWired);
    connect(document->model(), &HarnessModel::entityChanged, this, refreshWired);
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
    case Wired:
        return pinConnectionsText(m_document->model()->harness(), PinRef{c->id, pin.id});
    case Terminal:
        return pin.terminalPartNumber;
    case Seal:
        return pin.sealPartNumber;
    case PlugPart:
        return pin.plugPartNumber;
    case Notes:
        return pin.notes;
    default:
        return {};
    }
}

QString PinTableModel::cellText(const QModelIndex &index) const
{
    if (index.column() == Plug)
        return data(index, Qt::CheckStateRole) == Qt::Checked ? tr("Yes") : QString();
    return data(index, Qt::EditRole).toString();
}

void PinTableModel::applyText(Pin &pin, int column, const QString &text)
{
    switch (column) {
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
    case Plug: {
        const QString t = text.trimmed().toLower();
        pin.isCavityPlug = !(t.isEmpty() || t == u"0" || t == u"no" || t == u"false"
                             || t == tr("No").toLower());
        break;
    }
    case PlugPart:
        pin.plugPartNumber = text;
        if (!text.trimmed().isEmpty())
            pin.isCavityPlug = true; // a plug part number means the cavity is plugged
        break;
    case Notes:
        pin.notes = text;
        break;
    default:
        break;
    }
}

bool PinTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const Connector *current = m_document->model()->find<Connector>(m_connectorId);
    if (!current || index.row() >= int(current->pins.size()))
        return false;
    if (role != (index.column() == Plug ? Qt::CheckStateRole : Qt::EditRole))
        return false;
    QString text = value.toString();
    if (index.column() == Plug)
        text = Qt::CheckState(value.toInt()) == Qt::Checked ? u"1"_s : QString();
    return setCellTexts({{index, text}});
}

bool PinTableModel::setCellTexts(const std::vector<std::pair<QModelIndex, QString>> &cells)
{
    const Connector *current = m_document->model()->find<Connector>(m_connectorId);
    if (!current)
        return false;
    Connector updated = *current;
    PinConnections connections;
    for (const auto &[index, text] : cells) {
        if (!index.isValid() || index.row() >= int(updated.pins.size()))
            continue;
        Pin &pin = updated.pins[size_t(index.row())];
        if (index.column() == Wired) {
            if (text != data(index, Qt::EditRole).toString())
                connections.emplace_back(PinRef{updated.id, pin.id}, text);
        } else {
            applyText(pin, index.column(), text);
        }
    }
    if (!connections.empty()) {
        QStringList unknown;
        if (!checkPinConnections(qobject_cast<QWidget *>(QObject::parent()), m_document, connections, &unknown)) {
            if (!unknown.isEmpty())
                emit connectionsNotFound(unknown);
            return false;
        }
    }
    UndoBatch batch(m_document->undoStack(), cells.size() == 1 ? tr("Edit pin") : tr("Edit pins"));
    // The connector first: new wires may then fill empty circuit text on its pins.
    batch.push(updateConnector(m_document->model(), updated, tr("Edit pin")));
    applyPinConnections(m_document, connections, batch);
    return batch.pushedAny();
}

QString PinTableModel::housingPartNumber() const
{
    const Connector *c = m_document->model()->find<Connector>(m_connectorId);
    return c ? c->partNumber : QString();
}

std::vector<PartChoice> PinTableModel::partChoices(int column) const
{
    PinPartKind kind;
    switch (column) {
    case Terminal:
        kind = PinPartKind::Terminal;
        break;
    case Seal:
        kind = PinPartKind::Seal;
        break;
    case PlugPart:
        kind = PinPartKind::Plug;
        break;
    default:
        return {};
    }
    return pinPartChoices(m_document->model()->library(), housingPartNumber(), kind);
}

QStringList PinTableModel::wiredChoices(int row) const
{
    const Harness &harness = m_document->model()->harness();
    const Connector *c = m_document->model()->find<Connector>(m_connectorId);
    const WireEnd self = c && row >= 0 && row < int(c->pins.size())
                             ? WireEnd(PinRef{c->id, c->pins[size_t(row)].id})
                             : WireEnd(SpliceRef{});
    QStringList choices;
    for (const WireEndChoice &choice : wireEndChoices(harness)) {
        if (choice.end != self)
            choices << choice.text;
    }
    return choices;
}

Qt::ItemFlags PinTableModel::flags(const QModelIndex &index) const
{
    if (index.column() == Plug)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant PinTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal && section == Wired)
        return tr("Where the pin's wires go, e.g. \"ECU : 12\" or a splice. Type a pin, splice or flying "
                  "lead to wire it there; separate several with commas. New wires are numbered and "
                  "use the current wire color and gauge.");
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal && section == Plug)
        return tr("Unused cavity fitted with a blind plug.");
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal && section == PlugPart)
        return tr("Cavity plug part number. Entering one marks the cavity as plugged.");
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    switch (section) {
    case Label:
        return tr("Pin");
    case Circuit:
        return tr("Circuit");
    case Wired:
        return tr("Wired to");
    case Terminal:
        return tr("Terminal P/N");
    case Seal:
        return tr("Seal P/N");
    case Plug:
        return tr("Plug");
    case PlugPart:
        return tr("Plug P/N");
    case Notes:
        return tr("Notes");
    default:
        return {};
    }
}

// ---- PinTableView -----------------------------------------------------------------

namespace {

// Selected cells, top to bottom then left to right.
QModelIndexList sortedSelection(const QItemSelectionModel *selection)
{
    QModelIndexList cells = selection->selectedIndexes();
    std::sort(cells.begin(), cells.end(), [](const QModelIndex &a, const QModelIndex &b) {
        return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column();
    });
    return cells;
}

// An item for a part completer: shown as "PN — description", completing to the part number
// (Qt::UserRole, see partCompleter()).
QStandardItem *partItem(const PartChoice &choice, const QString &housing)
{
    QStringList details;
    if (choice.part && !choice.part->description.isEmpty())
        details << choice.part->description;
    if (choice.madeForHousing)
        details << PinTableModel::tr("made for %1").arg(housing);
    QString text = choice.partNumber;
    if (!details.isEmpty())
        text += u" — "_s + details.join(u", "_s);
    if (choice.part && choice.part->approximate)
        text = PinTableModel::tr("%1 (approx.)").arg(text);
    auto *item = new QStandardItem(text);
    item->setData(choice.partNumber, Qt::UserRole);
    if (choice.madeForHousing) {
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
    }
    return item;
}

// Opens the completer's list for `prefix`, wide enough for its text (lists are otherwise
// only as wide as the field, cutting off descriptions).
void showCompletions(QCompleter *completer, const QString &prefix)
{
    QAbstractItemView *popup = completer->popup();
    popup->setMinimumWidth(std::min(popup->sizeHintForColumn(0) + 40, 640));
    completer->setCompletionPrefix(prefix);
    completer->complete();
}

QCompleter *partCompleter(QStandardItemModel *items, QWidget *parent)
{
    auto *completer = new QCompleter(items, parent);
    completer->setCompletionRole(Qt::UserRole);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setMaxVisibleItems(15);
    return completer;
}

// Typing in a cell while several cells of its column are selected fills them all.
class PinCellDelegate : public QStyledItemDelegate
{
public:
    PinCellDelegate(QTableView *view, PinTableModel *pins)
        : QStyledItemDelegate(view)
        , m_view(view)
        , m_pins(pins)
    {
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        auto *line = qobject_cast<QLineEdit *>(editor);
        if (line && index.column() == PinTableModel::Wired) {
            auto *completer = new QCompleter(m_pins->wiredChoices(index.row()), line);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            line->setCompleter(completer);
        }
        const std::vector<PartChoice> parts = line ? m_pins->partChoices(index.column()) : std::vector<PartChoice>{};
        if (!parts.empty()) {
            // Library parts, those made for the housing first (in bold); the list opens with
            // the editor so they can be picked without typing.
            auto *items = new QStandardItemModel(line);
            const QString housing = m_pins->housingPartNumber();
            for (const PartChoice &choice : parts)
                items->appendRow(partItem(choice, housing));
            QCompleter *completer = partCompleter(items, line);
            line->setCompleter(completer);
            QTimer::singleShot(0, line, [line, completer] { showCompletions(completer, line->text()); });
        }
        return editor;
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *line = qobject_cast<QLineEdit *>(editor);
        if (!line) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }
        std::vector<std::pair<QModelIndex, QString>> cells{{index, line->text()}};
        for (const QModelIndex &cell : m_view->selectionModel()->selectedIndexes()) {
            if (cell.column() == index.column() && cell != index)
                cells.emplace_back(cell, line->text());
        }
        m_pins->setCellTexts(cells);
    }

private:
    QTableView *m_view;
    PinTableModel *m_pins;
};

// Pin table with spreadsheet-style keys: Ctrl+C copies the selected cells (tab separated),
// Ctrl+V pastes (one value fills every selected cell, a block pastes from the top left
// selected cell), Delete clears the selected cells and Space toggles every selected Plug box.
class PinTableView : public QTableView
{
public:
    explicit PinTableView(QWidget *parent)
        : QTableView(parent)
    {
    }

    PinTableModel *pins() const { return static_cast<PinTableModel *>(model()); }

protected:
    bool event(QEvent *event) override
    {
        // Keep these keys from window shortcuts while the table has focus.
        if (event->type() == QEvent::ShortcutOverride && state() != EditingState) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key == QKeySequence::Copy || key == QKeySequence::Paste || key == QKeySequence::Delete
                || key->key() == Qt::Key_Backspace) {
                event->accept();
                return true;
            }
        }
        return QTableView::event(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (state() != EditingState && selectionModel() && pins()) {
            if (event == QKeySequence::Copy) {
                copy();
                return;
            }
            if (event == QKeySequence::Paste) {
                paste();
                return;
            }
            if (event == QKeySequence::Delete || event->key() == Qt::Key_Backspace) {
                std::vector<std::pair<QModelIndex, QString>> cells;
                for (const QModelIndex &cell : selectionModel()->selectedIndexes())
                    cells.emplace_back(cell, QString());
                pins()->setCellTexts(cells);
                return;
            }
            if (event->key() == Qt::Key_Space && currentIndex().column() == PinTableModel::Plug) {
                const bool plug = pins()->cellText(currentIndex()).isEmpty();
                std::vector<std::pair<QModelIndex, QString>> cells;
                for (const QModelIndex &cell : selectionModel()->selectedIndexes()) {
                    if (cell.column() == PinTableModel::Plug)
                        cells.emplace_back(cell, plug ? u"1"_s : QString());
                }
                if (cells.empty())
                    cells.emplace_back(currentIndex(), plug ? u"1"_s : QString());
                pins()->setCellTexts(cells);
                return;
            }
        }
        QTableView::keyPressEvent(event);
    }

private:
    void copy()
    {
        const QModelIndexList cells = sortedSelection(selectionModel());
        if (cells.isEmpty())
            return;
        int top = cells.first().row(), bottom = cells.last().row();
        int left = cells.first().column(), right = left;
        for (const QModelIndex &cell : cells) {
            left = std::min(left, cell.column());
            right = std::max(right, cell.column());
        }
        QStringList rows;
        for (int row = top; row <= bottom; ++row) {
            QStringList values;
            for (int column = left; column <= right; ++column) {
                const QModelIndex cell = model()->index(row, column);
                values << (selectionModel()->isSelected(cell) ? pins()->cellText(cell) : QString());
            }
            rows << values.join(u'\t');
        }
        QGuiApplication::clipboard()->setText(rows.join(u'\n'));
    }

    void paste()
    {
        QString text = QGuiApplication::clipboard()->text();
        text.remove(u'\r');
        if (text.endsWith(u'\n'))
            text.chop(1);
        QList<QStringList> block;
        for (const QString &row : text.split(u'\n'))
            block << row.split(u'\t');
        std::vector<std::pair<QModelIndex, QString>> cells;
        const QModelIndexList selected = sortedSelection(selectionModel());
        if (block.size() == 1 && block.first().size() == 1) {
            for (const QModelIndex &cell : selected)
                cells.emplace_back(cell, block.first().first());
        } else {
            QModelIndex start = selected.isEmpty() ? currentIndex() : selected.first();
            for (const QModelIndex &cell : selected) {
                if (cell.column() < start.column())
                    start = model()->index(start.row(), cell.column());
            }
            if (!start.isValid())
                return;
            for (qsizetype r = 0; r < block.size(); ++r) {
                for (qsizetype c = 0; c < block[r].size(); ++c) {
                    const QModelIndex cell = model()->index(start.row() + int(r), start.column() + int(c));
                    if (cell.isValid())
                        cells.emplace_back(cell, block[r][c]);
                }
            }
        }
        if (cells.empty())
            return;
        pins()->setCellTexts(cells);
        // Select what was pasted, so it can be pasted again or cleared.
        QItemSelection pasted;
        for (const auto &cell : cells)
            pasted.select(cell.first, cell.first);
        selectionModel()->select(pasted, QItemSelectionModel::ClearAndSelect);
    }
};

} // namespace

// ---- ConnectorPanel ---------------------------------------------------------------

ConnectorPanel::ConnectorPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_label(new QLineEdit(this))
    , m_partNumber(new QLineEdit(this))
    , m_housings(partCompleter(new QStandardItemModel(this), m_partNumber))
    , m_manufacturer(new QLineEdit(this))
    , m_backshell(new QLineEdit(this))
    , m_strainRelief(new QLineEdit(this))
    , m_description(new QLineEdit(this))
    , m_gender(new QComboBox(this))
    , m_notes(new CommitTextEdit(this))
    , m_pinCount(new QSpinBox(this))
    , m_collapsed(new QCheckBox(tr("Collapsed"), this))
    , m_visibleRows(new QSpinBox(this))
    , m_pins(new PinTableModel(document, this))
    , m_table(new PinTableView(this))
    , m_wiredMessage(new QLabel(this))
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

    m_partNumber->setCompleter(m_housings);
    m_partNumber->setAccessibleName(tr("Connector part number"));
    m_partNumber->setToolTip(tr("Type or pick a housing from the parts library to fill the maker, "
                                "description and pins. Any other part number is kept as typed."));
    auto *browse = new QToolButton(this);
    browse->setArrowType(Qt::DownArrow);
    browse->setToolTip(tr("Show the connector housings in the parts library"));
    browse->setAccessibleName(tr("Show library housings"));
    auto *partNumberRow = new QHBoxLayout;
    partNumberRow->addWidget(m_partNumber, 1);
    partNumberRow->addWidget(browse);
    connect(browse, &QToolButton::clicked, this, [this] {
        m_partNumber->setFocus();
        showCompletions(m_housings, QString());
    });
    connect(m_housings, qOverload<const QString &>(&QCompleter::activated), this, [this] { commitInfo(); });
    connect(document->model(), &HarnessModel::libraryChanged, this, &ConnectorPanel::refreshHousings);
    connect(document->model(), &HarnessModel::harnessReset, this, &ConnectorPanel::refreshHousings);
    refreshHousings();

    auto *form = new QFormLayout;
    form->addRow(tr("&Label:"), m_label);
    form->addRow(tr("&Part number:"), partNumberRow);
    form->addRow(tr("&Manufacturer:"), m_manufacturer);
    form->addRow(tr("&Description:"), m_description);
    form->addRow(tr("&Gender:"), m_gender);
    m_backshell->setAccessibleName(tr("Backshell part number"));
    m_backshell->setPlaceholderText(tr("none"));
    m_strainRelief->setAccessibleName(tr("Strain relief part number"));
    m_strainRelief->setPlaceholderText(tr("none"));
    m_strainRelief->setToolTip(tr("Strain relief at the connector, e.g. the zip tie or clamp on its backshell. "
                                  "Counted in the BOM with the other ties."));
    form->addRow(tr("&Backshell:"), m_backshell);
    form->addRow(tr("&Strain relief:"), m_strainRelief);
    form->addRow(tr("&Notes:"), m_notes);
    form->addRow(tr("Pins:"), pinsRow);
    form->addRow(tr("Display:"), displayRow);

    m_table->setModel(m_pins);
    m_table->setItemDelegate(new PinCellDelegate(m_table, m_pins));
    m_table->setToolTip(tr("Select several cells to fill them at once: type a value, paste one with "
                           "Ctrl+V, or press Delete to clear them. Ctrl+C copies the selected cells."));
    m_wiredMessage->setWordWrap(true);
    m_wiredMessage->hide();
    connect(m_pins, &PinTableModel::connectionsNotFound, this, [this](const QStringList &names) {
        m_wiredMessage->setText(tr("Not wired: nothing is called %1. Use a pin as \"Connector : Pin\", "
                                   "a splice or a flying lead name.")
                                    .arg(names.join(tr(", "))));
        m_wiredMessage->show();
    });
    connect(m_document->undoStack(), &QUndoStack::indexChanged, m_wiredMessage, &QWidget::hide);
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
    layout->addWidget(m_wiredMessage);

    for (QLineEdit *edit : {m_label, m_partNumber, m_manufacturer, m_description, m_backshell, m_strainRelief})
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
    const QSignalBlocker b0(m_backshell), b00(m_strainRelief), b1(m_label), b2(m_partNumber), b3(m_manufacturer), b4(m_description), b5(m_gender),
        b6(m_pinCount), b7(m_collapsed), b8(m_visibleRows);
    m_label->setText(c->label);
    m_partNumber->setText(c->partNumber);
    m_manufacturer->setText(c->manufacturer);
    m_backshell->setText(c->backshellPartNumber);
    m_strainRelief->setText(c->strainReliefPartNumber);
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
    updated.backshellPartNumber = m_backshell->text().trimmed();
    updated.strainReliefPartNumber = m_strainRelief->text().trimmed();
    updated.description = m_description->text();
    updated.gender = Gender(m_gender->currentData().toInt());
    updated.notes = m_notes->toPlainText();
    if (updated.partNumber.trimmed() != current->partNumber.trimmed()) {
        if (const ConnectorPart *housing = m_document->model()->library().connectorPart(updated.partNumber)) {
            applyHousing(updated, *housing);
            return;
        }
    }
    if (updated != *current)
        m_document->undoStack()->push(
            new UpdateEntityCommand<Connector>(m_document->model(), updated, tr("Edit connector")));
}

void ConnectorPanel::applyHousing(Connector edited, const ConnectorPart &housing)
{
    HarnessModel *model = m_document->model();
    const Connector *current = model->find<Connector>(m_id);
    if (!current)
        return;
    edited.partNumber = housing.partNumber;
    if (!housing.manufacturer.isEmpty())
        edited.manufacturer = housing.manufacturer;
    const QString description = QStringList{housing.family, housing.description}
                                    .filter(QRegularExpression(u"\\S"_s))
                                    .join(u", "_s);
    if (!description.isEmpty())
        edited.description = description;
    if (housing.gender != Gender::None)
        edited.gender = housing.gender;
    edited.pins = current->pins;
    std::vector<Pin> pins = resizedPins(current->pins, int(housing.cavityLabels.size()));
    for (size_t i = 0; i < pins.size(); ++i)
        pins[i].label = housing.cavityLabels[qsizetype(i)];
    const auto lost = wiresLostByPinChange(model->harness(), *current, pins);
    if (!confirmWireRemoval(this, model->harness(), lost, tr("Using housing %1").arg(housing.partNumber))) {
        refresh(); // back to the connector as it is
        return;
    }
    UndoBatch batch(m_document->undoStack(), tr("Use housing %1").arg(housing.partNumber));
    if (edited != *current)
        batch.push(std::make_unique<UpdateEntityCommand<Connector>>(model, edited, tr("Edit connector")));
    if (pins != current->pins)
        batch.push(setConnectorPins(model, m_id, std::move(pins), tr("Use housing pins")));
}

void ConnectorPanel::refreshHousings()
{
    auto *items = static_cast<QStandardItemModel *>(m_housings->model());
    items->clear();
    for (const ConnectorPart &housing : m_document->model()->library().connectorParts) {
        QString text = housing.partNumber;
        const QString detail = QStringList{housing.family, housing.description}
                                   .filter(QRegularExpression(u"\\S"_s))
                                   .join(u", "_s);
        if (!detail.isEmpty())
            text += u" — "_s + detail;
        if (housing.approximate)
            text = tr("%1 (approx.)").arg(text);
        auto *item = new QStandardItem(text);
        item->setData(housing.partNumber, Qt::UserRole);
        items->appendRow(item);
    }
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
