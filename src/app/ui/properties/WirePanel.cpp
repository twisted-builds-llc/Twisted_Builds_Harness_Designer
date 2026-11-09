// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/WirePanel.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "model/Queries.h"
#include "ui/Format.h"
#include "ui/properties/PanelWidgets.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace tbh::app {

WirePanel::WirePanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_title(new QLabel(this))
    , m_name(new QLineEdit(this))
    , m_base(new WireColorCombo(false, this))
    , m_stripe(new WireColorCombo(true, this))
    , m_baseCustom(new QPushButton(tr("..."), this))
    , m_stripeCustom(new QPushButton(tr("..."), this))
    , m_gauge(new GaugeEditor(this))
    , m_wireType(new QLineEdit(this))
    , m_length(new QLabel(this))
    , m_lengthDetail(new QLabel(this))
    , m_lengthOverride(new QLineEdit(this))
    , m_overrideUnits(new QLabel(this))
{
    m_title->setWordWrap(true);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    for (QPushButton *button : {m_baseCustom, m_stripeCustom}) {
        button->setToolTip(tr("Pick a custom color"));
        button->setFixedWidth(32);
    }
    m_wireType->setPlaceholderText(tr("e.g. GXL, TXL"));
    QFont lengthFont = m_length->font();
    lengthFont.setBold(true);
    m_length->setFont(lengthFont);
    m_length->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lengthDetail->setWordWrap(true);
    m_lengthDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont detailFont = m_lengthDetail->font();
    detailFont.setPointSizeF(detailFont.pointSizeF() * 0.9);
    m_lengthDetail->setFont(detailFont);
    auto *validator = new QDoubleValidator(0, 1e7, 3, this);
    validator->setNotation(QDoubleValidator::StandardNotation);
    m_lengthOverride->setValidator(validator);
    m_lengthOverride->setPlaceholderText(tr("none"));

    auto row = [this](QWidget *a, QWidget *b) {
        auto *layout = new QHBoxLayout;
        layout->addWidget(a, 1);
        layout->addWidget(b);
        return layout;
    };
    auto *form = new QFormLayout;
    form->addRow(m_title);
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("Color:"), row(m_base, m_baseCustom));
    form->addRow(tr("Stripe:"), row(m_stripe, m_stripeCustom));
    form->addRow(tr("Gauge:"), m_gauge);
    form->addRow(tr("Wire &type:"), m_wireType);
    form->addRow(tr("Cut length:"), m_length);
    form->addRow(m_lengthDetail);
    form->addRow(tr("Length override:"), row(m_lengthOverride, m_overrideUnits));

    m_endA = makeEndEditor(tr("End A"), false);
    m_endB = makeEndEditor(tr("End B"), true);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_endA.box);
    layout->addWidget(m_endB.box);
    layout->addStretch();

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString name = m_name->text();
        apply([&](Wire &w) { w.name = name; }, tr("Rename wire"));
    });
    auto commitColor = [this](WireColorCombo *combo, bool stripe) {
        const auto value = combo->value();
        if (!value) {
            refresh(); // invalid or "(multiple)" left empty: restore
            return;
        }
        apply([&](Wire &w) { (stripe ? w.stripeColor : w.baseColor) = *value; },
              stripe ? tr("Set stripe color") : tr("Set wire color"));
        rememberStyle();
    };
    for (auto [combo, stripe] : {std::pair{m_base, false}, std::pair{m_stripe, true}}) {
        connect(combo, &QComboBox::activated, this, [=] { commitColor(combo, stripe); });
        connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [=] { commitColor(combo, stripe); });
    }
    for (auto [button, combo, stripe] :
         {std::tuple{m_baseCustom, m_base, false}, std::tuple{m_stripeCustom, m_stripe, true}}) {
        connect(button, &QPushButton::clicked, this, [=, this] {
            const QColor color = QColorDialog::getColor(Qt::white, this, tr("Wire Color"));
            if (!color.isValid())
                return;
            combo->setValue(color.name());
            commitColor(combo, stripe);
        });
    }
    m_gauge->onCommit = [this] {
        const auto gauge = m_gauge->value();
        if (!gauge) {
            refresh();
            return;
        }
        apply([&](Wire &w) { w.gauge = *gauge; }, tr("Set gauge"));
        rememberStyle();
    };
    connect(m_wireType, &QLineEdit::editingFinished, this, [this] {
        const QString type = m_wireType->text();
        if (type.isEmpty() && m_wireTypeMixed)
            return; // "(multiple)" left untouched
        apply([&](Wire &w) { w.wireTypeId = type; }, tr("Set wire type"));
        rememberStyle();
    });
    connect(m_lengthOverride, &QLineEdit::editingFinished, this, [this] {
        const QString text = m_lengthOverride->text().trimmed();
        std::optional<double> mm;
        if (!text.isEmpty())
            mm = unitsToMm(QLocale().toDouble(text), m_document->model()->harness().info.units);
        apply([&](Wire &w) { w.lengthOverrideMm = mm; }, tr("Set length override"));
    });

    HarnessModel *model = document->model();
    connect(model, &HarnessModel::entityChanged, this, [this](EntityKind kind, const QString &id) {
        if (kind == EntityKind::Wire && m_ids.contains(id))
            refresh();
    });
    connect(model, &HarnessModel::infoChanged, this, &WirePanel::refresh);
    // Lengths depend on the layout and settings.
    connect(model, &HarnessModel::settingsChanged, this, &WirePanel::refresh);
    for (auto signal : {&HarnessModel::entityAdded, &HarnessModel::entityChanged, &HarnessModel::entityRemoved})
        connect(model, signal, this, [this](EntityKind kind) {
            if (isVisible() && (kind == EntityKind::LayoutNode || kind == EntityKind::LayoutSegment))
                refresh();
        });
}

void WirePanel::refreshLength(const std::vector<Wire> &wires)
{
    const Harness &h = m_document->model()->harness();
    const calc::LayoutGraph graph(h);
    if (wires.size() == 1) {
        const WireLengthText text = describeWireLength(h, wires.front(), calc::wireLength(h, graph, wires.front()));
        m_length->setText(text.headline);
        m_lengthDetail->setText(text.detail);
        return;
    }
    double total = 0;
    int unknown = 0;
    for (const Wire &w : wires) {
        const calc::WireLength l = calc::wireLength(h, graph, w);
        if (l.isKnown())
            total += l.cutMm;
        else
            ++unknown;
    }
    m_length->setText(tr("%1 total").arg(formatLength(total, h.info.units)));
    m_lengthDetail->setText(unknown ? tr("%n selected wire(s) not routed and not counted.", nullptr, unknown)
                                    : QString());
}

WirePanel::EndEditor WirePanel::makeEndEditor(const QString &title, bool endB)
{
    EndEditor e;
    e.box = new QGroupBox(title, this);
    e.description = new QLabel(e.box);
    e.description->setWordWrap(true);
    e.kind = new QComboBox(e.box);
    e.kind->addItem(tr("Flying lead"), int(FreeEndKind::FlyingLead));
    e.kind->addItem(tr("Eyelet"), int(FreeEndKind::Eyelet));
    e.kind->addItem(tr("Ring terminal"), int(FreeEndKind::RingTerminal));
    e.kind->addItem(tr("Bare end"), int(FreeEndKind::Bare));
    e.terminal = new QLineEdit(e.box);
    e.label = new QLineEdit(e.box);
    auto *form = new QFormLayout(e.box);
    form->addRow(e.description);
    form->addRow(tr("End type:"), e.kind);
    form->addRow(tr("Terminal P/N:"), e.terminal);
    form->addRow(tr("Label:"), e.label);
    connect(e.kind, &QComboBox::activated, this, [this, endB] { commitEnd(endB); });
    connect(e.terminal, &QLineEdit::editingFinished, this, [this, endB] { commitEnd(endB); });
    connect(e.label, &QLineEdit::editingFinished, this, [this, endB] { commitEnd(endB); });
    return e;
}

std::vector<Wire> WirePanel::selectedWires() const
{
    std::vector<Wire> wires;
    for (const Wire &w : m_document->model()->harness().wires) {
        if (m_ids.contains(w.id))
            wires.push_back(w);
    }
    return wires;
}

void WirePanel::setWires(const QSet<QString> &ids)
{
    m_ids = ids;
    refresh();
}

void WirePanel::refresh()
{
    const std::vector<Wire> wires = selectedWires();
    if (wires.empty())
        return;
    const Harness &h = m_document->model()->harness();
    const Wire &first = wires.front();
    const bool single = wires.size() == 1;
    auto same = [&](auto getter) {
        return std::all_of(wires.begin(), wires.end(), [&](const Wire &w) { return getter(w) == getter(first); });
    };

    const QSignalBlocker b1(m_name), b2(m_base), b3(m_stripe), b4(m_wireType), b5(m_lengthOverride);
    m_title->setText(single ? describeWire(h, first) : tr("%n wires selected", nullptr, int(wires.size())));
    m_name->setEnabled(single);
    m_name->setText(single ? first.name : QString());

    if (same([](const Wire &w) { return w.baseColor; }))
        m_base->setValue(first.baseColor);
    else
        m_base->setMixed();
    if (same([](const Wire &w) { return w.stripeColor; }))
        m_stripe->setValue(first.stripeColor);
    else
        m_stripe->setMixed();
    if (same([](const Wire &w) { return w.gauge; }))
        m_gauge->setValue(first.gauge);
    else
        m_gauge->setMixed();
    m_wireTypeMixed = !same([](const Wire &w) { return w.wireTypeId; });
    if (!m_wireTypeMixed) {
        m_wireType->setText(first.wireTypeId);
        m_wireType->setPlaceholderText(tr("e.g. GXL, TXL"));
    } else {
        m_wireType->clear();
        m_wireType->setPlaceholderText(tr("(multiple)"));
    }

    const Units units = h.info.units;
    m_overrideUnits->setText(unitsSymbol(units));
    if (same([](const Wire &w) { return w.lengthOverrideMm; })) {
        m_lengthOverride->setText(first.lengthOverrideMm
                                      ? QLocale().toString(mmToUnits(*first.lengthOverrideMm, units), 'f', 2)
                                      : QString());
        m_lengthOverride->setPlaceholderText(tr("none"));
    } else {
        m_lengthOverride->clear();
        m_lengthOverride->setPlaceholderText(tr("(multiple)"));
    }

    refreshLength(wires);

    m_endA.box->setVisible(single);
    m_endB.box->setVisible(single);
    if (single) {
        refreshEnd(m_endA, first, false);
        refreshEnd(m_endB, first, true);
    }
}

void WirePanel::refreshEnd(EndEditor &e, const Wire &wire, bool endB)
{
    const WireEnd &end = endB ? wire.endB : wire.endA;
    const auto *free = std::get_if<FreeEnd>(&end);
    const QSignalBlocker b1(e.kind), b2(e.terminal), b3(e.label);
    e.description->setText(describeWireEnd(m_document->model()->harness(), end));
    auto *form = static_cast<QFormLayout *>(e.box->layout());
    for (QWidget *w : {static_cast<QWidget *>(e.kind), static_cast<QWidget *>(e.terminal),
                       static_cast<QWidget *>(e.label)})
        form->setRowVisible(w, free != nullptr);
    if (free) {
        e.kind->setCurrentIndex(e.kind->findData(int(free->kind)));
        e.terminal->setText(free->terminalPartNumber);
        e.label->setText(free->label);
    }
}

void WirePanel::commitEnd(bool endB)
{
    const EndEditor &e = endB ? m_endB : m_endA;
    const FreeEnd value{FreeEndKind(e.kind->currentData().toInt()), e.terminal->text(), e.label->text()};
    apply(
        [&](Wire &w) {
            WireEnd &end = endB ? w.endB : w.endA;
            if (std::holds_alternative<FreeEnd>(end))
                end = value;
        },
        tr("Edit wire end"));
}

void WirePanel::apply(const std::function<void(Wire &)> &change, const QString &undoText)
{
    std::vector<Wire> wires = selectedWires();
    for (Wire &w : wires)
        change(w);
    auto command = updateWires(m_document->model(), wires, undoText);
    if (command->childCount() > 0)
        m_document->undoStack()->push(command.release());
}

void WirePanel::rememberStyle()
{
    const std::vector<Wire> wires = selectedWires();
    if (wires.empty())
        return;
    const Wire &w = wires.front();
    m_document->setWireStyle({w.baseColor, w.stripeColor, w.gauge, w.wireTypeId});
}

// ---- SplicePanel -------------------------------------------------------------------

SplicePanel::SplicePanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_label(new QLineEdit(this))
    , m_partNumber(new QLineEdit(this))
{
    auto *form = new QFormLayout(this);
    form->addRow(tr("&Label:"), m_label);
    form->addRow(tr("&Part number:"), m_partNumber);
    connect(m_label, &QLineEdit::editingFinished, this, &SplicePanel::commit);
    connect(m_partNumber, &QLineEdit::editingFinished, this, &SplicePanel::commit);
    connect(document->model(), &HarnessModel::entityChanged, this, [this](EntityKind kind, const QString &id) {
        if (kind == EntityKind::Splice && id == m_id)
            refresh();
    });
}

void SplicePanel::setSplice(const QString &id)
{
    m_id = id;
    refresh();
}

void SplicePanel::refresh()
{
    const Splice *s = m_document->model()->find<Splice>(m_id);
    if (!s)
        return;
    const QSignalBlocker b1(m_label), b2(m_partNumber);
    m_label->setText(s->label);
    m_partNumber->setText(s->partNumber);
}

void SplicePanel::commit()
{
    const Splice *current = m_document->model()->find<Splice>(m_id);
    if (!current)
        return;
    Splice updated = *current;
    updated.label = m_label->text();
    updated.partNumber = m_partNumber->text();
    if (updated != *current)
        m_document->undoStack()->push(
            new UpdateEntityCommand<Splice>(m_document->model(), updated, tr("Edit splice")));
}

} // namespace tbh::app
