// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/WirePanel.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "commands/EntityCommands.h"
#include "model/Queries.h"
#include "calc/Bundle.h"
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
    , m_wireType(new QComboBox(this))
    , m_outerDiameter(new QLabel(this))
    , m_length(new QLabel(this))
    , m_lengthDetail(new QLabel(this))
    , m_lengthOverride(new QLineEdit(this))
    , m_overrideUnits(new QLabel(this))
{
    m_title->setWordWrap(true);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    for (QPushButton *button : {m_baseCustom, m_stripeCustom})
        button->setFixedWidth(32);
    m_baseCustom->setToolTip(tr("Pick a custom wire color"));
    m_baseCustom->setAccessibleName(tr("Pick a custom wire color"));
    m_stripeCustom->setToolTip(tr("Pick a custom stripe color"));
    m_stripeCustom->setAccessibleName(tr("Pick a custom stripe color"));
    m_base->setAccessibleName(tr("Wire color"));
    m_stripe->setAccessibleName(tr("Stripe color"));
    m_gauge->setAccessibleName(tr("Gauge"));
    m_lengthOverride->setAccessibleName(tr("Length override"));
    m_length->setAccessibleName(tr("Cut length"));
    m_lengthDetail->setAccessibleName(tr("How the cut length was calculated"));
    m_wireType->setToolTip(tr("Wire types and their outer diameters come from the parts library "
                              "(Edit > Parts Library)."));
    m_outerDiameter->setWordWrap(true);
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
    form->addRow(tr("Outer diameter:"), m_outerDiameter);
    form->addRow(tr("Cut length:"), m_length);
    form->addRow(m_lengthDetail);
    form->addRow(tr("Length override:"), row(m_lengthOverride, m_overrideUnits));

    m_endA = makeEndEditor(tr("End A"));
    m_endB = makeEndEditor(tr("End B"));

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
    connect(m_wireType, &QComboBox::activated, this, [this] {
        const QVariant data = m_wireType->currentData();
        if (!data.isValid())
            return; // "(multiple)" placeholder
        const QString type = data.toString();
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
        if ((kind == EntityKind::Wire && m_ids.contains(id)) || kind == EntityKind::FreeEnd)
            refresh();
    });
    connect(model, &HarnessModel::infoChanged, this, &WirePanel::refresh);
    // Lengths depend on the layout and settings.
    connect(model, &HarnessModel::settingsChanged, this, &WirePanel::refresh);
    connect(model, &HarnessModel::libraryChanged, this, &WirePanel::refresh);
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

WirePanel::EndEditor WirePanel::makeEndEditor(const QString &title)
{
    EndEditor e;
    e.box = new QGroupBox(title, this);
    e.description = new QLabel(e.box);
    e.description->setWordWrap(true);
    e.freeEnd = new FreeEndEditor(m_document, e.box);
    auto *layout = new QVBoxLayout(e.box);
    layout->addWidget(e.description);
    layout->addWidget(e.freeEnd);
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
    const Library &library = m_document->model()->library();
    const bool mixedType = !same([](const Wire &w) { return w.wireTypeId; });
    m_wireType->clear();
    if (mixedType)
        m_wireType->addItem(tr("(multiple)")); // no data: not applicable
    m_wireType->addItem(tr("None"), QString());
    for (const WireType &t : library.wireTypes)
        m_wireType->addItem(t.approximate ? tr("%1 (approx.)").arg(t.name) : t.name, t.id);
    if (!mixedType && !first.wireTypeId.isEmpty() && !library.wireType(first.wireTypeId))
        m_wireType->addItem(tr("%1 (not in library)").arg(first.wireTypeId), first.wireTypeId);
    m_wireType->setCurrentIndex(mixedType ? 0 : std::max(0, m_wireType->findData(first.wireTypeId)));

    if (single) {
        const calc::WireDiameter od = calc::wireOuterDiameter(library, first);
        const WireType *type = library.wireType(first.wireTypeId);
        QString text = formatLength(od.odMm, h.info.units);
        if (od.estimated)
            text += QLatin1Char(' ') + tr("(estimated from the gauge; add this type and gauge to the parts library)");
        else if (type && type->approximate)
            text += QLatin1Char(' ') + tr("(approximate library value)");
        m_outerDiameter->setText(text);
    } else {
        m_outerDiameter->setText(tr("(several wires)"));
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
    const auto *free = std::get_if<FreeEndRef>(&end);
    e.description->setText(describeWireEnd(m_document->model()->harness(), end));
    e.freeEnd->setVisible(free != nullptr);
    if (free)
        e.freeEnd->setFreeEnd(free->freeEndId);
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

// ---- FreeEndEditor -----------------------------------------------------------------

FreeEndEditor::FreeEndEditor(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_kind(new QComboBox(this))
    , m_terminal(new QLineEdit(this))
    , m_label(new QLineEdit(this))
{
    m_kind->addItem(tr("Flying lead"), int(FreeEndKind::FlyingLead));
    m_kind->addItem(tr("Eyelet"), int(FreeEndKind::Eyelet));
    m_kind->addItem(tr("Ring terminal"), int(FreeEndKind::RingTerminal));
    m_kind->addItem(tr("Bare end"), int(FreeEndKind::Bare));
    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("End type:"), m_kind);
    form->addRow(tr("Terminal P/N:"), m_terminal);
    form->addRow(tr("Label:"), m_label);
    connect(m_kind, &QComboBox::activated, this, &FreeEndEditor::commit);
    connect(m_terminal, &QLineEdit::editingFinished, this, &FreeEndEditor::commit);
    connect(m_label, &QLineEdit::editingFinished, this, &FreeEndEditor::commit);
    connect(document->model(), &HarnessModel::entityChanged, this, [this](EntityKind kind, const QString &id) {
        if (kind == EntityKind::FreeEnd && id == m_id)
            refresh();
    });
}

void FreeEndEditor::setFreeEnd(const QString &id)
{
    m_id = id;
    refresh();
}

void FreeEndEditor::refresh()
{
    const FreeEnd *f = m_document->model()->find<FreeEnd>(m_id);
    if (!f)
        return;
    const QSignalBlocker b1(m_kind), b2(m_terminal), b3(m_label);
    m_kind->setCurrentIndex(m_kind->findData(int(f->kind)));
    m_terminal->setText(f->terminalPartNumber);
    m_label->setText(f->label);
}

void FreeEndEditor::commit()
{
    const FreeEnd *current = m_document->model()->find<FreeEnd>(m_id);
    if (!current)
        return;
    FreeEnd updated = *current;
    updated.kind = FreeEndKind(m_kind->currentData().toInt());
    updated.terminalPartNumber = m_terminal->text();
    updated.label = m_label->text();
    if (auto command = updateFreeEnd(m_document->model(), updated, tr("Edit free end")))
        m_document->undoStack()->push(command.release());
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
