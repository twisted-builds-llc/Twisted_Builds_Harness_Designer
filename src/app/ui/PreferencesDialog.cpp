// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/PreferencesDialog.h"

#include "Preferences.h"
#include "ui/UnitsCombo.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tbh::app {

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , m_defaultUnits(new QComboBox(this))
    , m_autosave(new QSpinBox(this))
    , m_panInput(new QComboBox(this))
    , m_wheelPans(new QCheckBox(tr("Scroll &wheel pans (Ctrl+wheel or pinch zooms)"), this))
{
    setWindowTitle(tr("Preferences"));
    fillUnitsCombo(m_defaultUnits);
    setComboUnits(m_defaultUnits, preferences::defaultUnits());

    auto *form = new QFormLayout;
    form->addRow(tr("Default &units for new harnesses:"), m_defaultUnits);
    m_autosave->setRange(0, 60);
    m_autosave->setSuffix(tr(" min"));
    m_autosave->setSpecialValueText(tr("Off"));
    m_autosave->setValue(preferences::autosaveMinutes());
    m_autosave->setToolTip(tr("How often unsaved changes are saved to a recovery file, so they can be "
                              "recovered if the application closes unexpectedly."));
    form->addRow(tr("&Autosave for recovery every:"), m_autosave);
    m_panInput->addItem(tr("Middle-button drag"), int(preferences::PanInput::MiddleDrag));
    m_panInput->addItem(tr("Right-button drag (right-click still opens the menu)"),
                        int(preferences::PanInput::RightDrag));
    m_panInput->addItem(tr("Hold Space + left drag"), int(preferences::PanInput::SpaceDrag));
    m_panInput->setCurrentIndex(m_panInput->findData(int(preferences::panInput())));
    m_panInput->setToolTip(tr("How to pan the schematic and layout. A middle-button drag always pans."));
    form->addRow(tr("&Pan the canvas with:"), m_panInput);
    m_wheelPans->setChecked(preferences::wheelPans());
    m_wheelPans->setToolTip(tr("For touchpads: two-finger scrolling moves the view instead of "
                               "zooming."));
    form->addRow(m_wheelPans);
    auto *hint = new QLabel(tr("Each harness file keeps its own units; change them in the "
                               "harness properties."),
                            this);
    hint->setWordWrap(true);
    hint->setEnabled(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addStretch();
    layout->addWidget(buttons);
}

void PreferencesDialog::accept()
{
    preferences::setDefaultUnits(comboUnits(m_defaultUnits));
    preferences::setAutosaveMinutes(m_autosave->value());
    preferences::setPanInput(preferences::PanInput(m_panInput->currentData().toInt()));
    preferences::setWheelPans(m_wheelPans->isChecked());
    QDialog::accept();
}

} // namespace tbh::app
