// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/HarnessPanel.h"

#include "Document.h"
#include "commands/HarnessCommands.h"
#include "ui/Format.h"
#include "ui/UnitsCombo.h"
#include "ui/properties/PanelWidgets.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace tbh::app {

HarnessPanel::HarnessPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_name(new QLineEdit(this))
    , m_revision(new QLineEdit(this))
    , m_author(new QLineEdit(this))
    , m_units(new QComboBox(this))
    , m_description(new CommitTextEdit(this))
    , m_serviceLoop(new QDoubleSpinBox(this))
    , m_rounding(new QLineEdit(this))
    , m_wasteWire(new QDoubleSpinBox(this))
    , m_wasteSleeving(new QDoubleSpinBox(this))
    , m_packing(new QDoubleSpinBox(this))
    , m_sleevingMargin(new QDoubleSpinBox(this))
{
    fillUnitsCombo(m_units);
    auto *info = new QFormLayout;
    info->addRow(tr("&Name:"), m_name);
    info->addRow(tr("&Revision:"), m_revision);
    info->addRow(tr("&Author:"), m_author);
    info->addRow(tr("&Units:"), m_units);
    info->addRow(tr("&Description:"), m_description);

    for (QDoubleSpinBox *percent : {m_serviceLoop, m_wasteWire, m_wasteSleeving, m_sleevingMargin}) {
        percent->setRange(0, 100);
        percent->setDecimals(1);
        percent->setSuffix(QStringLiteral(" %"));
        percent->setKeyboardTracking(false);
    }
    m_packing->setRange(0.1, 1.0);
    m_packing->setDecimals(2);
    m_packing->setSingleStep(0.05);
    m_packing->setKeyboardTracking(false);

    auto lengthRow = [this](QLineEdit *edit) {
        auto *unit = new QLabel(this);
        m_unitLabels.push_back(unit);
        auto *row = new QHBoxLayout;
        row->addWidget(edit, 1);
        row->addWidget(unit);
        return row;
    };

    auto *lengths = new QGroupBox(tr("Wire length"), this);
    auto *lengthsForm = new QFormLayout(lengths);
    lengthsForm->addRow(tr("Service loop:"), m_serviceLoop);
    lengthsForm->addRow(tr("Round cut length up to:"), lengthRow(m_rounding));
    m_rounding->setToolTip(tr("Cut lengths are rounded up to a multiple of this. 0 = no rounding."));

    auto *allowances = new QGroupBox(tr("End allowances (strip, terminal, slack)"), this);
    auto *allowanceForm = new QFormLayout(allowances);
    const std::pair<EndType, QString> endTypes[] = {
        {EndType::Pin, tr("Pin / terminal:")}, {EndType::Splice, tr("Splice:")},
        {EndType::FlyingLead, tr("Flying lead:")}, {EndType::Eyelet, tr("Eyelet:")},
        {EndType::RingTerminal, tr("Ring terminal:")}, {EndType::Bare, tr("Bare end:")}};
    for (const auto &[type, label] : endTypes) {
        auto *edit = new QLineEdit(this);
        m_allowances[type] = edit;
        allowanceForm->addRow(label, lengthRow(edit));
        connect(edit, &QLineEdit::editingFinished, this, &HarnessPanel::commitSettings);
    }

    auto *bom = new QGroupBox(tr("BOM and bundles"), this);
    auto *bomForm = new QFormLayout(bom);
    bomForm->addRow(tr("Wire waste factor:"), m_wasteWire);
    bomForm->addRow(tr("Sleeving waste factor:"), m_wasteSleeving);
    bomForm->addRow(tr("Bundle packing factor:"), m_packing);
    bomForm->addRow(tr("Sleeving size margin:"), m_sleevingMargin);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(info);
    layout->addWidget(lengths);
    layout->addWidget(allowances);
    layout->addWidget(bom);
    layout->addStretch();

    for (QLineEdit *edit : {m_name, m_revision, m_author})
        connect(edit, &QLineEdit::editingFinished, this, &HarnessPanel::commitInfo);
    connect(m_units, &QComboBox::activated, this, &HarnessPanel::commitInfo);
    m_description->onCommit = [this] { commitInfo(); };
    connect(m_rounding, &QLineEdit::editingFinished, this, &HarnessPanel::commitSettings);
    for (QDoubleSpinBox *spin : {m_serviceLoop, m_wasteWire, m_wasteSleeving, m_packing, m_sleevingMargin})
        connect(spin, &QDoubleSpinBox::valueChanged, this, &HarnessPanel::commitSettings);

    HarnessModel *model = m_document->model();
    connect(model, &HarnessModel::harnessReset, this, &HarnessPanel::refresh);
    connect(model, &HarnessModel::infoChanged, this, &HarnessPanel::refresh);
    connect(model, &HarnessModel::settingsChanged, this, &HarnessPanel::refresh);
    refresh();
}

void HarnessPanel::refresh()
{
    const Harness &h = m_document->model()->harness();
    const HarnessInfo &info = h.info;
    const Settings &s = h.settings;
    const QSignalBlocker b1(m_name), b2(m_revision), b3(m_author), b4(m_units), b5(m_serviceLoop),
        b6(m_wasteWire), b7(m_wasteSleeving), b8(m_packing), b9(m_sleevingMargin);
    m_name->setText(info.name);
    m_revision->setText(info.revision);
    m_author->setText(info.author);
    setComboUnits(m_units, info.units);
    m_description->setTextIfDifferent(info.description);

    m_serviceLoop->setValue(s.serviceLoopPercent);
    m_rounding->setText(formatLength(s.lengthRoundingIncrementMm, info.units, false));
    m_wasteWire->setValue(s.wasteFactorWirePercent);
    m_wasteSleeving->setValue(s.wasteFactorSleevingPercent);
    m_packing->setValue(s.packingFactor);
    m_sleevingMargin->setValue(s.sleevingDiameterMarginPercent);
    for (const auto &[type, edit] : m_allowances) {
        const auto it = s.stripAllowanceMm.find(type);
        edit->setText(formatLength(it == s.stripAllowanceMm.end() ? 0.0 : it->second, info.units, false));
    }
    for (QLabel *label : m_unitLabels)
        label->setText(unitsSymbol(info.units));
}

void HarnessPanel::commitInfo()
{
    HarnessModel *model = m_document->model();
    HarnessInfo info = model->harness().info;
    info.name = m_name->text();
    info.revision = m_revision->text();
    info.author = m_author->text();
    info.units = comboUnits(m_units);
    info.description = m_description->toPlainText();
    if (info != model->harness().info)
        m_document->undoStack()->push(new SetHarnessInfoCommand(model, std::move(info)));
}

void HarnessPanel::commitSettings()
{
    HarnessModel *model = m_document->model();
    const Units units = model->harness().info.units;
    const Settings &current = model->harness().settings;
    Settings s = current;
    s.serviceLoopPercent = m_serviceLoop->value();
    s.wasteFactorWirePercent = m_wasteWire->value();
    s.wasteFactorSleevingPercent = m_wasteSleeving->value();
    s.packingFactor = m_packing->value();
    s.sleevingDiameterMarginPercent = m_sleevingMargin->value();

    // Keep stored values when the displayed text was not edited (avoids unit round-off).
    auto readLength = [&](QLineEdit *edit, double storedMm) -> std::optional<double> {
        if (edit->text() == formatLength(storedMm, units, false))
            return storedMm;
        return parseLength(edit->text(), units);
    };
    const auto rounding = readLength(m_rounding, current.lengthRoundingIncrementMm);
    if (!rounding) {
        refresh();
        return;
    }
    s.lengthRoundingIncrementMm = *rounding;
    for (const auto &[type, edit] : m_allowances) {
        const auto it = current.stripAllowanceMm.find(type);
        const double stored = it == current.stripAllowanceMm.end() ? 0.0 : it->second;
        const auto value = readLength(edit, stored);
        if (!value) {
            refresh();
            return;
        }
        if (*value == 0.0 && it == current.stripAllowanceMm.end())
            continue; // keep "unset" (0) as unset
        s.stripAllowanceMm[type] = *value;
    }
    if (s != current)
        m_document->undoStack()->push(new SetHarnessSettingsCommand(model, std::move(s)));
}

} // namespace tbh::app
