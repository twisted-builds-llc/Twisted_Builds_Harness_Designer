// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/PinPatternDialog.h"

#include "model/PinLabels.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tbh::app {

PinPatternDialog::PinPatternDialog(int currentPinCount, QWidget *parent)
    : QDialog(parent)
    , m_currentPinCount(currentPinCount)
    , m_pattern(new QLineEdit(this))
    , m_step(new QSpinBox(this))
    , m_reverse(new QCheckBox(tr("Reverse order"), this))
    , m_resize(new QCheckBox(this))
    , m_preview(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Pin Label Pattern"));
    m_pattern->setPlaceholderText(tr("e.g. A1-A8, B1-B8, C1-C12 or 1-120"));
    m_step->setRange(1, 99);
    m_step->setToolTip(tr("Take every Nth number of each range (2 = every other number)."));
    m_resize->setChecked(true);
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *help = new QLabel(tr("Separate entries with commas. \"A1-A8\" or \"A1-8\" is a range; anything "
                               "else is used as a label as-is (e.g. GND, +)."),
                            this);
    help->setWordWrap(true);
    help->setEnabled(false);

    auto *form = new QFormLayout;
    form->addRow(tr("&Pattern:"), m_pattern);
    form->addRow(tr("&Step:"), m_step);
    form->addRow(QString(), m_reverse);
    form->addRow(QString(), m_resize);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(help);
    layout->addLayout(form);
    layout->addWidget(m_preview);
    layout->addWidget(m_buttons);
    resize(460, sizeHint().height());

    connect(m_pattern, &QLineEdit::textChanged, this, &PinPatternDialog::updatePreview);
    connect(m_step, &QSpinBox::valueChanged, this, &PinPatternDialog::updatePreview);
    connect(m_reverse, &QCheckBox::toggled, this, &PinPatternDialog::updatePreview);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updatePreview();
}

bool PinPatternDialog::resizeToFit() const
{
    return m_resize->isChecked();
}

void PinPatternDialog::updatePreview()
{
    QString error;
    const auto labels = expandPinPattern(m_pattern->text(), m_step->value(), m_reverse->isChecked(), &error);
    m_labels = labels.value_or(QStringList());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(labels.has_value());
    if (!labels) {
        m_preview->setText(m_pattern->text().trimmed().isEmpty() ? QString() : error);
        m_resize->setText(tr("Set the pin count to match the pattern"));
        return;
    }
    constexpr qsizetype kShown = 16;
    QString sample = labels->mid(0, kShown).join(QStringLiteral(", "));
    if (labels->size() > kShown)
        sample += QStringLiteral(", … ") + labels->last();
    m_preview->setText(tr("%n label(s): %1", nullptr, int(labels->size())).arg(sample));
    m_resize->setText(tr("Set the pin count to %1 (now %2)").arg(labels->size()).arg(m_currentPinCount));
}

} // namespace tbh::app
