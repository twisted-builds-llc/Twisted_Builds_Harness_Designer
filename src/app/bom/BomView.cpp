// SPDX-License-Identifier: GPL-3.0-or-later
#include "bom/BomView.h"

#include "Document.h"
#include "bom/BomCsv.h"
#include "commands/EntityCommands.h"
#include "ui/Format.h"
#include "ui/WireColors.h"
#include "ui/properties/PanelWidgets.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace tbh::app {

namespace {

enum ExtraColumn { ExPart, ExDescription, ExQuantity, ExUnit, ExNotes };

QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, int(headers.size()), parent);
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    return table;
}

QTableWidgetItem *cell(const QString &text, bool number = false)
{
    auto *item = new QTableWidgetItem(text);
    if (number)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QString colorText(const QString &base, const QString &stripe)
{
    return stripe.isEmpty() ? base : QStringLiteral("%1/%2").arg(base, stripe);
}

QString categoryName(bom::PartCategory c)
{
    switch (c) {
    case bom::PartCategory::Connector:
        return QCoreApplication::translate("tbh::app::BomView", "Connector");
    case bom::PartCategory::Terminal:
        return QCoreApplication::translate("tbh::app::BomView", "Terminal");
    case bom::PartCategory::Seal:
        return QCoreApplication::translate("tbh::app::BomView", "Seal");
    case bom::PartCategory::CavityPlug:
        return QCoreApplication::translate("tbh::app::BomView", "Cavity plug");
    case bom::PartCategory::Splice:
        return QCoreApplication::translate("tbh::app::BomView", "Splice");
    }
    return {};
}

} // namespace

BomView::BomView(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_summary(new QLabel(this))
    , m_wires(makeTable({tr("Color"), tr("Gauge"), tr("Wire type"), tr("Pieces"), tr("Length"),
                         tr("With waste"), tr("Note")},
                        this))
    , m_cutList(makeTable({tr("Wire"), tr("End A"), tr("End B"), tr("Color"), tr("Gauge"), tr("Wire type"),
                           tr("Cut length"), tr("Note")},
                          this))
    , m_sleeving(makeTable({tr("Type"), tr("Size"), tr("Segments"), tr("Length"), tr("With waste")}, this))
    , m_parts(makeTable({tr("Category"), tr("Part number"), tr("Description"), tr("Qty"), tr("Used at")}, this))
    , m_extras(makeTable({tr("Part number"), tr("Description"), tr("Qty"), tr("Unit"), tr("Notes")}, this))
{
    m_wires->setAccessibleName(tr("Wire totals"));
    m_cutList->setAccessibleName(tr("Cut list"));
    m_sleeving->setAccessibleName(tr("Sleeving totals"));
    m_parts->setAccessibleName(tr("Parts"));
    m_extras->setAccessibleName(tr("Other items"));
    m_wires->setObjectName(QStringLiteral("wireTable"));
    m_cutList->setObjectName(QStringLiteral("cutListTable"));
    m_sleeving->setObjectName(QStringLiteral("sleevingTable"));
    m_parts->setObjectName(QStringLiteral("partsTable"));
    m_extras->setObjectName(QStringLiteral("extrasTable"));
    m_extras->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                              | QAbstractItemView::AnyKeyPressed);
    m_extras->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_cutList->setSortingEnabled(true);
    m_cutList->setToolTip(tr("Double-click a wire to select it in the schematic."));

    auto *exportButton = new QPushButton(tr("Export CSV..."), this);
    auto *top = new QHBoxLayout;
    top->addWidget(m_summary, 1);
    top->addWidget(exportButton);

    auto *extrasPage = new QWidget(this);
    auto *addButton = new QPushButton(tr("Add Item"), extrasPage);
    auto *removeButton = new QPushButton(tr("Remove Item"), extrasPage);
    auto *extrasButtons = new QHBoxLayout;
    extrasButtons->addWidget(addButton);
    extrasButtons->addWidget(removeButton);
    extrasButtons->addStretch();
    auto *extrasLayout = new QVBoxLayout(extrasPage);
    extrasLayout->setContentsMargins(0, 0, 0, 0);
    auto *extrasHint = new QLabel(tr("Items the model cannot count, such as tape, zip ties or labels. "
                                     "Double-click a cell to edit."),
                                  extrasPage);
    extrasHint->setEnabled(false);
    extrasLayout->addWidget(extrasHint);
    extrasLayout->addWidget(m_extras, 1);
    extrasLayout->addLayout(extrasButtons);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(m_wires, tr("Wire"));
    tabs->addTab(m_cutList, tr("Cut List"));
    tabs->addTab(m_sleeving, tr("Sleeving"));
    tabs->addTab(m_parts, tr("Parts"));
    tabs->addTab(extrasPage, tr("Other Items"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(tabs, 1);

    connect(exportButton, &QPushButton::clicked, this, &BomView::exportCsv);
    connect(addButton, &QPushButton::clicked, this, &BomView::addExtra);
    connect(removeButton, &QPushButton::clicked, this, &BomView::removeExtra);
    connect(m_extras, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!m_filling)
            commitExtra(item->row());
    });
    connect(m_cutList, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        const QString wireId = m_cutList->item(item->row(), 0)->data(Qt::UserRole).toString();
        if (!wireId.isEmpty())
            emit wireActivated(wireId);
    });

    HarnessModel *model = document->model();
    for (auto signal : {&HarnessModel::entityAdded, &HarnessModel::entityChanged, &HarnessModel::entityRemoved})
        connect(model, signal, this, &BomView::scheduleRefresh);
    connect(model, &HarnessModel::harnessReset, this, &BomView::scheduleRefresh);
    connect(model, &HarnessModel::infoChanged, this, &BomView::scheduleRefresh);
    connect(model, &HarnessModel::settingsChanged, this, &BomView::scheduleRefresh);
    connect(model, &HarnessModel::libraryChanged, this, &BomView::scheduleRefresh);
    refresh();
}

void BomView::scheduleRefresh()
{
    if (m_pending)
        return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        refresh();
    });
}

void BomView::refresh()
{
    const Harness &h = m_document->model()->harness();
    const Units units = h.info.units;
    m_bom = bom::buildBom(h, m_document->model()->library());
    const QIcon warning = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    auto flag = [&](QTableWidgetItem *item, const QString &why) {
        item->setIcon(warning);
        item->setToolTip(why);
    };
    m_filling = true;

    m_wires->setRowCount(int(m_bom.wires.size()));
    for (int r = 0; r < int(m_bom.wires.size()); ++r) {
        const bom::WireLine &w = m_bom.wires[size_t(r)];
        auto *color = cell(colorText(w.baseColor, w.stripeColor));
        const QColor swatch = wireColor(w.baseColor);
        if (swatch.isValid())
            color->setData(Qt::DecorationRole, swatch);
        m_wires->setItem(r, 0, color);
        m_wires->setItem(r, 1, cell(gaugeText(w.gauge)));
        m_wires->setItem(r, 2, cell(w.wireTypeName.isEmpty() ? w.wireTypeId : w.wireTypeName));
        m_wires->setItem(r, 3, cell(QString::number(w.pieces), true));
        m_wires->setItem(r, 4, cell(formatLength(w.lengthMm, units), true));
        m_wires->setItem(r, 5, cell(formatLength(w.lengthWithWasteMm, units), true));
        auto *note = cell(w.piecesWithoutLength ? tr("%n piece(s) not routed: length not included", nullptr,
                                                     w.piecesWithoutLength)
                                                : QString());
        if (w.piecesWithoutLength)
            flag(color, note->text());
        m_wires->setItem(r, 6, note);
    }

    m_cutList->setSortingEnabled(false);
    m_cutList->setRowCount(int(m_bom.cutList.size()));
    for (int r = 0; r < int(m_bom.cutList.size()); ++r) {
        const bom::CutLine &c = m_bom.cutList[size_t(r)];
        auto *name = cell(c.name);
        name->setData(Qt::UserRole, c.wireId);
        m_cutList->setItem(r, 0, name);
        m_cutList->setItem(r, 1, cell(c.endA));
        m_cutList->setItem(r, 2, cell(c.endB));
        m_cutList->setItem(r, 3, cell(colorText(c.baseColor, c.stripeColor)));
        m_cutList->setItem(r, 4, cell(gaugeText(c.gauge)));
        m_cutList->setItem(r, 5, cell(c.wireTypeName));
        auto *length = cell(c.cutMm ? formatLength(*c.cutMm, units) : QString(), true);
        if (c.cutMm)
            length->setData(Qt::UserRole, *c.cutMm);
        m_cutList->setItem(r, 6, length);
        const QString note = !c.cutMm ? tr("Not routed") : (c.overridden ? tr("Manual length") : QString());
        m_cutList->setItem(r, 7, cell(note));
        if (!c.cutMm)
            flag(name, tr("Not routed: place both ends in the layout and connect them."));
    }
    m_cutList->setSortingEnabled(true);

    m_sleeving->setRowCount(int(m_bom.sleeving.size()));
    for (int r = 0; r < int(m_bom.sleeving.size()); ++r) {
        const bom::SleevingLine &s = m_bom.sleeving[size_t(r)];
        auto *family = cell(s.family);
        if (!s.known)
            flag(family, tr("This sleeving is not in the parts library."));
        m_sleeving->setItem(r, 0, family);
        m_sleeving->setItem(r, 1, cell(s.known ? s.sizeLabel : tr("(not in library)")));
        m_sleeving->setItem(r, 2, cell(QString::number(s.segments), true));
        m_sleeving->setItem(r, 3, cell(formatLength(s.lengthMm, units), true));
        m_sleeving->setItem(r, 4, cell(formatLength(s.lengthWithWasteMm, units), true));
    }

    m_parts->setRowCount(int(m_bom.parts.size()));
    for (int r = 0; r < int(m_bom.parts.size()); ++r) {
        const bom::PartLine &p = m_bom.parts[size_t(r)];
        auto *pn = cell(p.partNumber.isEmpty() ? tr("(no part number)") : p.partNumber);
        if (p.partNumber.isEmpty())
            flag(pn, tr("Enter part numbers for these items."));
        m_parts->setItem(r, 0, cell(categoryName(p.category)));
        m_parts->setItem(r, 1, pn);
        m_parts->setItem(r, 2, cell(p.description));
        m_parts->setItem(r, 3, cell(QString::number(p.quantity), true));
        m_parts->setItem(r, 4, cell(p.references.join(QStringLiteral(", "))));
    }

    m_extras->setRowCount(int(m_bom.extras.size()));
    for (int r = 0; r < int(m_bom.extras.size()); ++r) {
        const BomExtra &e = m_bom.extras[size_t(r)];
        auto *pn = cell(e.partNumber);
        pn->setData(Qt::UserRole, e.id);
        m_extras->setItem(r, ExPart, pn);
        m_extras->setItem(r, ExDescription, cell(e.description));
        m_extras->setItem(r, ExQuantity, cell(QLocale().toString(e.quantity), true));
        m_extras->setItem(r, ExUnit, cell(e.unit));
        m_extras->setItem(r, ExNotes, cell(e.notes));
    }
    m_filling = false;

    for (QTableWidget *table : {m_wires, m_cutList, m_sleeving, m_parts})
        table->resizeColumnsToContents();

    const int flagged = m_bom.flaggedCount();
    m_summary->setText(flagged ? tr("%n line(s) need attention (marked with a warning).", nullptr, flagged)
                               : tr("All lines complete."));
}

void BomView::commitExtra(int row)
{
    const QString id = m_extras->item(row, ExPart)->data(Qt::UserRole).toString();
    HarnessModel *model = m_document->model();
    const BomExtra *current = model->find<BomExtra>(id);
    if (!current)
        return;
    auto text = [&](int column) { return m_extras->item(row, column) ? m_extras->item(row, column)->text().trimmed() : QString(); };
    BomExtra updated = *current;
    updated.partNumber = text(ExPart);
    updated.description = text(ExDescription);
    updated.unit = text(ExUnit);
    updated.notes = text(ExNotes);
    bool ok = false;
    double quantity = QLocale().toDouble(text(ExQuantity), &ok);
    if (!ok)
        quantity = QLocale::c().toDouble(text(ExQuantity), &ok);
    if (!ok || quantity < 0) {
        scheduleRefresh(); // restore the stored value
        return;
    }
    updated.quantity = quantity;
    if (updated != *current)
        m_document->undoStack()->push(new UpdateEntityCommand<BomExtra>(model, updated, tr("Edit BOM item")));
}

void BomView::addExtra()
{
    BomExtra extra{newId(), {}, tr("New item"), 1.0, tr("ea"), {}, {}};
    m_document->undoStack()->push(
        new AddEntityCommand<BomExtra>(m_document->model(), std::move(extra), {}, tr("Add BOM item")));
}

void BomView::removeExtra()
{
    const int row = m_extras->currentRow();
    if (row < 0 || !m_extras->item(row, ExPart))
        return;
    const QString id = m_extras->item(row, ExPart)->data(Qt::UserRole).toString();
    m_document->undoStack()->push(
        new RemoveEntityCommand<BomExtra>(m_document->model(), id, tr("Remove BOM item")));
}

bool BomView::writeCsv(const QString &path, bool perSection, const QString &baseName, QString *error) const
{
    const Units units = m_document->model()->harness().info.units;
    // UTF-8 with a byte-order mark so spreadsheet apps read symbols correctly.
    auto write = [&](const QString &file, const QString &csv) {
        QSaveFile out(file);
        const QByteArray data = QByteArrayLiteral("\xEF\xBB\xBF") + csv.toUtf8();
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            if (error)
                *error = tr("Could not write %1: %2").arg(QDir::toNativeSeparators(file), out.errorString());
            return false;
        }
        return true;
    };
    if (!perSection)
        return write(path, bom::bomToCsv(m_bom, units));
    for (bom::CsvSection section : {bom::CsvSection::Wires, bom::CsvSection::CutList, bom::CsvSection::Sleeving,
                                    bom::CsvSection::Parts, bom::CsvSection::Extras}) {
        const QString file = QDir(path).filePath(
            QStringLiteral("%1-%2.csv").arg(baseName, bom::sectionFileName(section)));
        if (!write(file, bom::sectionToCsv(m_bom, section, units)))
            return false;
    }
    return true;
}

void BomView::exportCsv()
{
    QMessageBox choice(QMessageBox::Question, tr("Export BOM"), tr("Export the BOM as:"), QMessageBox::Cancel, this);
    QPushButton *combined = choice.addButton(tr("One File"), QMessageBox::AcceptRole);
    QPushButton *separate = choice.addButton(tr("One File per Section"), QMessageBox::AcceptRole);
    choice.setInformativeText(tr("Lengths are exported in %1.").arg(unitsSymbol(m_document->model()->harness().info.units)));
    choice.exec();
    if (choice.clickedButton() != combined && choice.clickedButton() != separate)
        return;

    QString base = m_document->filePath().isEmpty() ? m_document->model()->harness().info.name
                                                    : QFileInfo(m_document->filePath()).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (base.trimmed().isEmpty())
        base = QStringLiteral("harness");
    const QString startDir = m_document->filePath().isEmpty() ? QDir::homePath()
                                                              : QFileInfo(m_document->filePath()).absolutePath();
    QString error;
    bool ok = false;
    QString target;
    if (choice.clickedButton() == combined) {
        target = QFileDialog::getSaveFileName(this, tr("Export BOM"), QDir(startDir).filePath(base + QStringLiteral("-bom.csv")),
                                              tr("CSV files (*.csv)"));
        if (target.isEmpty())
            return;
        if (!target.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
            target += QStringLiteral(".csv");
        ok = writeCsv(target, false, base, &error);
    } else {
        target = QFileDialog::getExistingDirectory(this, tr("Export BOM to Folder"), startDir);
        if (target.isEmpty())
            return;
        ok = writeCsv(target, true, base, &error);
    }
    if (!ok)
        QMessageBox::warning(this, tr("Export BOM"), error);
}

} // namespace tbh::app
