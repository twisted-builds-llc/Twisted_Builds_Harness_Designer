// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/LibraryDialog.h"

#include "LibraryStore.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace tbh::app {

namespace {

enum OdColumn { OdGauge, OdSystem, OdValue };
enum SleevingColumn { SlFamily, SlKind, SlSize, SlInside, SlRecovered, SlApprox };

QString number(double value)
{
    return QLocale().toString(value, 'g', 6);
}

std::optional<double> parseNumber(const QString &text)
{
    bool ok = false;
    double value = QLocale().toDouble(text.trimmed(), &ok);
    if (!ok)
        value = QLocale::c().toDouble(text.trimmed(), &ok);
    return ok ? std::optional(value) : std::nullopt;
}

QComboBox *systemCombo(GaugeSystem system)
{
    auto *combo = new QComboBox;
    combo->addItem(QStringLiteral("AWG"), int(GaugeSystem::Awg));
    combo->addItem(QStringLiteral("mm²"), int(GaugeSystem::Mm2));
    combo->setCurrentIndex(combo->findData(int(system)));
    return combo;
}

QComboBox *kindCombo(SleevingKind kind)
{
    auto *combo = new QComboBox;
    combo->addItem(QCoreApplication::translate("tbh::app::LibraryDialog", "Braided"), int(SleevingKind::Braided));
    combo->addItem(QCoreApplication::translate("tbh::app::LibraryDialog", "Split loom"), int(SleevingKind::SplitLoom));
    combo->addItem(QCoreApplication::translate("tbh::app::LibraryDialog", "Heat shrink"), int(SleevingKind::HeatShrink));
    combo->addItem(QCoreApplication::translate("tbh::app::LibraryDialog", "Other"), int(SleevingKind::Other));
    combo->setCurrentIndex(combo->findData(int(kind)));
    return combo;
}

QPushButton *button(const QString &text, QWidget *parent)
{
    return new QPushButton(text, parent);
}

} // namespace

LibraryDialog::LibraryDialog(const Library &library, QWidget *parent)
    : QDialog(parent)
    , m_original(library)
    , m_library(library)
    , m_types(new QListWidget(this))
    , m_typeName(new QLineEdit(this))
    , m_ods(new QTableWidget(0, 3, this))
    , m_sleevings(new QTableWidget(0, 6, this))
{
    setWindowTitle(tr("Parts Library"));
    resize(820, 560);

    auto *notice = new QLabel(tr("<b>Values shipped with the app are approximate</b> (marked \"approx.\"). "
                                 "Check them against your manufacturer's datasheets and correct them here; "
                                 "edited entries are no longer marked approximate. Diameters are in "
                                 "millimeters."),
                              this);
    notice->setWordWrap(true);

    // Wire types.
    auto *typesPage = new QWidget(this);
    auto *addType = button(tr("Add Type"), typesPage);
    auto *removeType = button(tr("Remove Type"), typesPage);
    auto *left = new QWidget(typesPage);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_types, 1);
    auto *typeButtons = new QHBoxLayout;
    typeButtons->addWidget(addType);
    typeButtons->addWidget(removeType);
    leftLayout->addLayout(typeButtons);

    m_ods->setHorizontalHeaderLabels({tr("Gauge"), tr("System"), tr("Outer Ø (mm)")});
    m_ods->horizontalHeader()->setStretchLastSection(true);
    m_ods->verticalHeader()->hide();
    auto *addOd = button(tr("Add Gauge"), typesPage);
    auto *removeOd = button(tr("Remove Gauge"), typesPage);
    auto *right = new QWidget(typesPage);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    auto *nameForm = new QFormLayout;
    nameForm->addRow(tr("Name:"), m_typeName);
    rightLayout->addLayout(nameForm);
    rightLayout->addWidget(m_ods, 1);
    auto *odButtons = new QHBoxLayout;
    odButtons->addWidget(addOd);
    odButtons->addWidget(removeOd);
    odButtons->addStretch();
    rightLayout->addLayout(odButtons);

    auto *splitter = new QSplitter(typesPage);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setSizes({220, 560});
    auto *typesLayout = new QVBoxLayout(typesPage);
    typesLayout->addWidget(splitter);

    // Sleeving.
    auto *sleevingPage = new QWidget(this);
    m_sleevings->setHorizontalHeaderLabels(
        {tr("Type"), tr("Kind"), tr("Size label"), tr("Inside Ø (mm)"), tr("Recovered Ø (mm)"), QString()});
    m_sleevings->horizontalHeader()->setSectionResizeMode(SlFamily, QHeaderView::Stretch);
    m_sleevings->verticalHeader()->hide();
    m_sleevings->setToolTip(tr("Recovered Ø is the fully shrunk diameter of heat shrink; leave it empty "
                               "for other sleeving."));
    auto *addSleeving = button(tr("Add Sleeving"), sleevingPage);
    auto *removeSleeving = button(tr("Remove Sleeving"), sleevingPage);
    auto *sleevingLayout = new QVBoxLayout(sleevingPage);
    sleevingLayout->addWidget(m_sleevings, 1);
    auto *sleevingButtons = new QHBoxLayout;
    sleevingButtons->addWidget(addSleeving);
    sleevingButtons->addWidget(removeSleeving);
    sleevingButtons->addStretch();
    sleevingLayout->addLayout(sleevingButtons);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(typesPage, tr("Wire Types"));
    tabs->addTab(sleevingPage, tr("Sleeving"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                             | QDialogButtonBox::RestoreDefaults,
                                         this);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(notice);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &LibraryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Restore Defaults"),
                                  tr("Replace the whole library with the approximate defaults shipped "
                                     "with the app?"))
            != QMessageBox::Yes)
            return;
        m_library = libraryStore::defaultLibrary();
        m_original = m_library; // restored entries stay marked approximate
        m_currentType = -1;
        loadTypes();
        loadSleevings();
    });
    connect(m_types, &QListWidget::currentRowChanged, this, [this](int row) {
        QString error;
        if (m_currentType >= 0 && !storeType(m_currentType, &error))
            QMessageBox::warning(this, tr("Parts Library"), error);
        showType(row);
    });
    connect(addType, &QPushButton::clicked, this, [this] {
        QString error;
        if (m_currentType >= 0 && !storeType(m_currentType, &error)) {
            QMessageBox::warning(this, tr("Parts Library"), error);
            return;
        }
        m_library.wireTypes.push_back(WireType{newId(), tr("New wire type"), {}, false, {}});
        m_currentType = -1;
        loadTypes();
        m_types->setCurrentRow(m_types->count() - 1);
        m_typeName->setFocus();
        m_typeName->selectAll();
    });
    connect(removeType, &QPushButton::clicked, this, [this] {
        const int row = m_types->currentRow();
        if (row < 0)
            return;
        m_library.wireTypes.erase(m_library.wireTypes.begin() + row);
        m_currentType = -1;
        loadTypes();
    });
    connect(addOd, &QPushButton::clicked, this, [this] {
        if (m_currentType >= 0)
            addOdRow(WireOuterDiameter{Gauge{18, GaugeSystem::Awg}, 0.0});
    });
    connect(removeOd, &QPushButton::clicked, this, [this] {
        if (m_ods->currentRow() >= 0)
            m_ods->removeRow(m_ods->currentRow());
    });
    connect(addSleeving, &QPushButton::clicked, this, [this] {
        addSleevingRow(Sleeving{newId(), tr("New sleeving"), SleevingKind::Braided, {}, 0.0, std::nullopt, false, {}});
        m_sleevings->scrollToBottom();
    });
    connect(removeSleeving, &QPushButton::clicked, this, [this] {
        if (m_sleevings->currentRow() >= 0)
            m_sleevings->removeRow(m_sleevings->currentRow());
    });

    loadTypes();
    loadSleevings();
}

void LibraryDialog::loadTypes()
{
    const QSignalBlocker blocker(m_types);
    m_types->clear();
    for (const WireType &t : m_library.wireTypes)
        m_types->addItem(t.approximate ? tr("%1 (approx.)").arg(t.name) : t.name);
    m_currentType = -1;
    if (!m_library.wireTypes.empty()) {
        m_types->setCurrentRow(0);
        showType(0);
    } else {
        showType(-1);
    }
}

void LibraryDialog::showType(int index)
{
    m_currentType = index;
    m_ods->setRowCount(0);
    const bool valid = index >= 0 && index < int(m_library.wireTypes.size());
    m_typeName->setEnabled(valid);
    m_ods->setEnabled(valid);
    if (!valid) {
        m_typeName->clear();
        return;
    }
    const WireType &t = m_library.wireTypes[size_t(index)];
    m_typeName->setText(t.name);
    for (const WireOuterDiameter &od : t.outerDiameters)
        addOdRow(od);
}

void LibraryDialog::addOdRow(const WireOuterDiameter &od)
{
    const int row = m_ods->rowCount();
    m_ods->insertRow(row);
    m_ods->setItem(row, OdGauge, new QTableWidgetItem(number(od.gauge.value)));
    m_ods->setCellWidget(row, OdSystem, systemCombo(od.gauge.system));
    m_ods->setItem(row, OdValue, new QTableWidgetItem(od.odMm > 0 ? number(od.odMm) : QString()));
}

bool LibraryDialog::storeType(int index, QString *error)
{
    if (index < 0 || index >= int(m_library.wireTypes.size()))
        return true;
    WireType &t = m_library.wireTypes[size_t(index)];
    WireType updated = t;
    updated.name = m_typeName->text().trimmed();
    if (updated.name.isEmpty()) {
        *error = tr("A wire type needs a name.");
        return false;
    }
    updated.outerDiameters.clear();
    for (int row = 0; row < m_ods->rowCount(); ++row) {
        const auto gauge = parseNumber(m_ods->item(row, OdGauge) ? m_ods->item(row, OdGauge)->text() : QString());
        const auto od = parseNumber(m_ods->item(row, OdValue) ? m_ods->item(row, OdValue)->text() : QString());
        if (!gauge || !od || *od <= 0 || *gauge < 0) {
            *error = tr("%1, row %2: enter a gauge and a positive outer diameter.").arg(updated.name).arg(row + 1);
            return false;
        }
        const auto *system = static_cast<QComboBox *>(m_ods->cellWidget(row, OdSystem));
        updated.outerDiameters.push_back(
            WireOuterDiameter{Gauge{*gauge, GaugeSystem(system->currentData().toInt())}, *od});
    }
    t = updated;
    return true;
}

void LibraryDialog::loadSleevings()
{
    m_sleevings->setRowCount(0);
    for (const Sleeving &s : m_library.sleevings)
        addSleevingRow(s);
}

void LibraryDialog::addSleevingRow(const Sleeving &s)
{
    const int row = m_sleevings->rowCount();
    m_sleevings->insertRow(row);
    auto *family = new QTableWidgetItem(s.family);
    family->setData(Qt::UserRole, s.id);
    m_sleevings->setItem(row, SlFamily, family);
    m_sleevings->setCellWidget(row, SlKind, kindCombo(s.kind));
    m_sleevings->setItem(row, SlSize, new QTableWidgetItem(s.sizeLabel));
    m_sleevings->setItem(row, SlInside, new QTableWidgetItem(s.innerDiameterMm > 0 ? number(s.innerDiameterMm) : QString()));
    m_sleevings->setItem(row, SlRecovered,
                         new QTableWidgetItem(s.recoveredDiameterMm ? number(*s.recoveredDiameterMm) : QString()));
    auto *approx = new QTableWidgetItem(s.approximate ? tr("approx.") : QString());
    approx->setFlags(Qt::ItemIsEnabled);
    m_sleevings->setItem(row, SlApprox, approx);
}

bool LibraryDialog::storeSleevings(QString *error)
{
    std::vector<Sleeving> sleevings;
    for (int row = 0; row < m_sleevings->rowCount(); ++row) {
        auto text = [&](int column) { return m_sleevings->item(row, column) ? m_sleevings->item(row, column)->text().trimmed() : QString(); };
        Sleeving s;
        s.id = m_sleevings->item(row, SlFamily)->data(Qt::UserRole).toString();
        if (const Sleeving *old = m_library.sleeving(s.id)) {
            s.extra = old->extra;
            s.approximate = old->approximate;
        }
        s.family = text(SlFamily);
        s.kind = SleevingKind(static_cast<QComboBox *>(m_sleevings->cellWidget(row, SlKind))->currentData().toInt());
        s.sizeLabel = text(SlSize);
        const auto inside = parseNumber(text(SlInside));
        if (s.family.isEmpty() || !inside || *inside <= 0) {
            *error = tr("Sleeving row %1: enter a type and a positive inside diameter.").arg(row + 1);
            return false;
        }
        s.innerDiameterMm = *inside;
        if (!text(SlRecovered).isEmpty()) {
            const auto recovered = parseNumber(text(SlRecovered));
            if (!recovered || *recovered <= 0) {
                *error = tr("Sleeving row %1: the recovered diameter must be a positive number.").arg(row + 1);
                return false;
            }
            s.recoveredDiameterMm = *recovered;
        }
        sleevings.push_back(s);
    }
    m_library.sleevings = std::move(sleevings);
    return true;
}

void LibraryDialog::accept()
{
    QString error;
    if (!storeType(m_currentType, &error) || !storeSleevings(&error)) {
        QMessageBox::warning(this, tr("Parts Library"), error);
        return;
    }
    // Entries the user changed are theirs now: no longer approximate.
    for (WireType &t : m_library.wireTypes) {
        const WireType *old = m_original.wireType(t.id);
        if (old && (old->name != t.name || old->outerDiameters != t.outerDiameters))
            t.approximate = false;
    }
    for (Sleeving &s : m_library.sleevings) {
        const Sleeving *old = m_original.sleeving(s.id);
        if (!old)
            continue;
        Sleeving compare = *old;
        compare.approximate = s.approximate;
        if (compare != s)
            s.approximate = false;
    }
    QDialog::accept();
}

} // namespace tbh::app
