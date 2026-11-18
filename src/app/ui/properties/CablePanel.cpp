// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/CablePanel.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "model/Queries.h"
#include "ui/Format.h"
#include "ui/properties/PanelWidgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace tbh::app {

namespace {

// A length field with its unit after it.
QHBoxLayout *withUnits(QLineEdit *edit, QLabel *units)
{
    auto *row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(units);
    return row;
}

} // namespace

CablePanel::CablePanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_label(new QLineEdit(this))
    , m_twisted(new QCheckBox(tr("&Twisted"), this))
    , m_layLength(new QLineEdit(this))
    , m_layUnits(new QLabel(this))
    , m_source(new QComboBox(this))
    , m_partNumber(new QLineEdit(this))
    , m_jacket(new QLineEdit(this))
    , m_jacketUnits(new QLabel(this))
    , m_wires(new QLabel(this))
    , m_shield(new QLabel(this))
    , m_addShield(new QPushButton(tr("Add Shield Wire"), this))
    , m_removeShield(new QPushButton(tr("Remove Shield Wire"), this))
    , m_shieldPartNumber(new QLineEdit(this))
    , m_notes(new CommitTextEdit(this))
{
    m_source->addItem(tr("Built (loose wires)"), int(CableSource::Built));
    m_source->addItem(tr("Bought cable"), int(CableSource::Bought));
    m_layLength->setPlaceholderText(tr("not set"));
    m_layLength->setAccessibleName(tr("One twist every"));
    m_layLength->setToolTip(tr("Length of one full twist, printed on the build sheets. Empty = not set."));
    m_jacket->setPlaceholderText(tr("not set"));
    m_jacket->setAccessibleName(tr("Jacket diameter"));
    m_jacket->setToolTip(tr("Outer diameter of the cable. Bundle sizes use it where all its wires run."));
    m_partNumber->setToolTip(tr("Part number of the cable, counted in the BOM by length."));
    m_shieldPartNumber->setToolTip(tr("Braid or foil you shield the wires with, counted in the BOM by length."));
    m_wires->setWordWrap(true);
    m_wires->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_shield->setWordWrap(true);
    m_addShield->setToolTip(tr("Adds a shield (drain) wire to this cable on the schematic. Draw its ends to where the shield lands."));

    auto *shieldButtons = new QHBoxLayout;
    shieldButtons->addWidget(m_addShield);
    shieldButtons->addWidget(m_removeShield);
    shieldButtons->addStretch();

    // A row whose field is a layout needs its label's buddy set by hand, or the '&' shows.
    auto buddyLabel = [this](const QString &text, QWidget *buddy) {
        auto *label = new QLabel(text, this);
        label->setBuddy(buddy);
        return label;
    };

    auto *form = new QFormLayout;
    form->addRow(tr("&Label:"), m_label);
    form->addRow(QString(), m_twisted);
    form->addRow(buddyLabel(tr("One twist &every:"), m_layLength), withUnits(m_layLength, m_layUnits));
    form->addRow(tr("&Source:"), m_source);
    form->addRow(tr("Cable &part number:"), m_partNumber);
    form->addRow(buddyLabel(tr("&Jacket diameter:"), m_jacket), withUnits(m_jacket, m_jacketUnits));
    form->addRow(tr("Wires:"), m_wires);

    auto *shieldBox = new QGroupBox(tr("Shield"), this);
    auto *shieldForm = new QFormLayout(shieldBox);
    shieldForm->addRow(tr("Shield wire:"), m_shield);
    shieldForm->addRow(QString(), shieldButtons);
    shieldForm->addRow(tr("P&art number:"), m_shieldPartNumber);

    auto *notesForm = new QFormLayout;
    notesForm->addRow(tr("&Notes:"), m_notes);

    // Rows keep their natural height at the top; only the stretch takes the extra space.
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(shieldBox);
    layout->addLayout(notesForm);
    layout->addStretch(1);

    for (QLineEdit *edit : {m_label, m_layLength, m_partNumber, m_jacket, m_shieldPartNumber})
        connect(edit, &QLineEdit::editingFinished, this, &CablePanel::commit);
    connect(m_twisted, &QCheckBox::toggled, this, &CablePanel::commit);
    connect(m_source, &QComboBox::activated, this, &CablePanel::commit);
    m_notes->onCommit = [this] { commit(); };
    connect(m_addShield, &QPushButton::clicked, this, [this] { emit addShieldRequested(m_id); });
    connect(m_removeShield, &QPushButton::clicked, this, [this] {
        if (auto command = removeShieldWire(m_document->model(), m_id))
            m_document->undoStack()->push(command.release());
    });

    HarnessModel *model = document->model();
    // The wire list names wires; a shield wire's name can change too.
    for (auto signal : {&HarnessModel::entityChanged, &HarnessModel::entityAdded, &HarnessModel::entityRemoved}) {
        connect(model, signal, this, [this](EntityKind kind, const QString &) {
            if (kind == EntityKind::Cable || kind == EntityKind::Wire)
                refresh();
        });
    }
    connect(model, &HarnessModel::infoChanged, this, &CablePanel::refresh); // units
}

void CablePanel::setCable(const QString &id)
{
    m_id = id;
    refresh();
}

void CablePanel::refresh()
{
    const Harness &h = m_document->model()->harness();
    const Cable *c = m_document->model()->find<Cable>(m_id);
    if (!c)
        return;
    const Units units = h.info.units;
    const QSignalBlocker b1(m_label), b2(m_twisted), b3(m_layLength), b4(m_source), b5(m_partNumber), b6(m_jacket),
        b7(m_shieldPartNumber);
    m_label->setText(c->label);
    m_twisted->setChecked(c->twisted);
    m_layLength->setText(c->layLengthMm ? formatLength(*c->layLengthMm, units, false) : QString());
    m_layLength->setEnabled(c->twisted);
    m_layUnits->setText(unitsSymbol(units));
    m_jacketUnits->setText(unitsSymbol(units));
    m_source->setCurrentIndex(m_source->findData(int(c->source)));
    const bool bought = c->source == CableSource::Bought;
    m_partNumber->setText(c->partNumber);
    m_partNumber->setEnabled(bought);
    m_jacket->setText(c->jacketOdMm ? formatLength(*c->jacketOdMm, units, false) : QString());
    m_jacket->setEnabled(bought);
    QStringList wires;
    for (const QString &id : c->wireIds) {
        if (const Wire *w = m_document->model()->find<Wire>(id))
            wires << describeWire(h, *w);
    }
    m_wires->setText(wires.join(QLatin1Char('\n')));
    const Wire *shield = c->shielded() ? m_document->model()->find<Wire>(c->shieldWireId) : nullptr;
    m_shield->setText(shield ? describeWire(h, *shield) : tr("None"));
    m_addShield->setVisible(!shield);
    m_removeShield->setVisible(shield != nullptr);
    m_shieldPartNumber->setText(c->shieldPartNumber);
    m_shieldPartNumber->setEnabled(shield && !bought);
    m_notes->setTextIfDifferent(c->notes);
}

void CablePanel::commit()
{
    HarnessModel *model = m_document->model();
    const Cable *current = model->find<Cable>(m_id);
    if (!current)
        return;
    const Units units = model->harness().info.units;
    // Empty = not set; text that is not a positive length keeps the stored value.
    auto readLength = [&](QLineEdit *edit, std::optional<double> stored) -> std::optional<double> {
        if (edit->text().trimmed().isEmpty())
            return std::nullopt;
        if (stored && edit->text() == formatLength(*stored, units, false))
            return stored;
        const auto mm = parseLength(edit->text(), units);
        return mm && *mm > 0 ? mm : stored;
    };
    Cable updated = *current;
    updated.label = m_label->text().trimmed();
    updated.twisted = m_twisted->isChecked();
    updated.layLengthMm = readLength(m_layLength, current->layLengthMm);
    updated.source = CableSource(m_source->currentData().toInt());
    updated.partNumber = m_partNumber->text().trimmed();
    updated.jacketOdMm = readLength(m_jacket, current->jacketOdMm);
    updated.shieldPartNumber = m_shieldPartNumber->text().trimmed();
    updated.notes = m_notes->toPlainText();
    if (updated != *current)
        m_document->undoStack()->push(new UpdateEntityCommand<Cable>(model, updated, tr("Edit cable")));
    else
        refresh(); // restore text that was not accepted
}

} // namespace tbh::app
