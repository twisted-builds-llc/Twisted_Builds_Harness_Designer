// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/LayoutPanels.h"

#include "Document.h"
#include "calc/Bundle.h"
#include "calc/WireLength.h"
#include "commands/EntityCommands.h"
#include "model/Queries.h"
#include "ui/Format.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QLocale>
#include <QVBoxLayout>

#include <algorithm>

namespace tbh::app {

// ---- SegmentPanel -------------------------------------------------------------------

SegmentPanel::SegmentPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_length(new QLineEdit(this))
    , m_units(new QLabel(this))
    , m_label(new QLineEdit(this))
    , m_wireCount(new QLabel(this))
    , m_wires(new QListWidget(this))
    , m_bundle(new QLabel(this))
    , m_bundleNote(new QLabel(this))
    , m_family(new QComboBox(this))
    , m_size(new QComboBox(this))
    , m_fit(new QLabel(this))
    , m_recommendation(new QLabel(this))
    , m_useRecommended(new QPushButton(tr("Use"), this))
{
    m_length->setPlaceholderText(tr("enter the branch length"));
    m_length->setToolTip(tr("Measured branch length. The drawing is not to scale; this value is used "
                            "for wire lengths."));
    m_label->setPlaceholderText(tr("optional, e.g. Trunk"));
    m_length->setAccessibleName(tr("Segment length"));
    m_wires->setAccessibleName(tr("Wires through this segment"));
    m_family->setAccessibleName(tr("Sleeving type"));
    m_size->setAccessibleName(tr("Sleeving size"));
    auto *lengthRow = new QHBoxLayout;
    lengthRow->addWidget(m_length, 1);
    lengthRow->addWidget(m_units);
    auto *form = new QFormLayout;
    form->addRow(tr("Length:"), lengthRow);
    form->addRow(tr("&Label:"), m_label);
    QFont bold = m_bundle->font();
    bold.setBold(true);
    m_bundle->setFont(bold);
    for (QLabel *label : {m_bundleNote, m_fit, m_recommendation})
        label->setWordWrap(true);
    m_bundle->setToolTip(tr("Bundle diameter D = sqrt(sum of wire OD\u00b2 / packing factor)."));
    form->addRow(tr("Bundle:"), m_bundle);
    form->addRow(m_bundleNote);

    auto *sleeving = new QGroupBox(tr("Sleeving"), this);
    auto *sleevingForm = new QFormLayout(sleeving);
    sleevingForm->addRow(tr("Type:"), m_family);
    sleevingForm->addRow(tr("Size:"), m_size);
    sleevingForm->addRow(m_fit);
    sleevingForm->addRow(m_recommendation);
    auto *useRow = new QHBoxLayout;
    useRow->addStretch();
    useRow->addWidget(m_useRecommended);
    sleevingForm->addRow(useRow);
    m_useRecommended->setText(tr("Use Recommended Size"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(sleeving);
    layout->addWidget(m_wireCount);
    layout->addWidget(m_wires, 1);

    connect(m_family, &QComboBox::activated, this, [this] {
        m_familyChoice = m_family->currentData().toString();
        if (m_familyChoice->isEmpty())
            assignSleeving({}); // "None" removes the sleeving
        refresh();
    });
    connect(m_size, &QComboBox::activated, this, [this] {
        const QString id = m_size->currentData().toString();
        if (!id.isEmpty())
            assignSleeving(id);
    });
    connect(m_useRecommended, &QPushButton::clicked, this, [this] { assignSleeving(m_recommendedId); });
    connect(m_length, &QLineEdit::editingFinished, this, &SegmentPanel::commit);
    connect(m_label, &QLineEdit::editingFinished, this, &SegmentPanel::commit);
    HarnessModel *model = document->model();
    // Wire usage depends on the whole graph; refresh on any change while visible.
    for (auto signal : {&HarnessModel::entityAdded, &HarnessModel::entityChanged, &HarnessModel::entityRemoved})
        connect(model, signal, this, [this] {
            if (isVisible())
                refresh();
        });
    connect(model, &HarnessModel::infoChanged, this, &SegmentPanel::refresh);
    connect(model, &HarnessModel::settingsChanged, this, &SegmentPanel::refresh);
    connect(model, &HarnessModel::libraryChanged, this, &SegmentPanel::refresh);
}

void SegmentPanel::assignSleeving(const QString &sleevingId)
{
    HarnessModel *model = m_document->model();
    const LayoutSegment *current = model->find<LayoutSegment>(m_id);
    if (!current || current->sleevingId == sleevingId)
        return;
    LayoutSegment updated = *current;
    updated.sleevingId = sleevingId;
    m_document->undoStack()->push(new UpdateEntityCommand<LayoutSegment>(
        model, updated, sleevingId.isEmpty() ? tr("Remove sleeving") : tr("Assign sleeving")));
}

void SegmentPanel::setSegment(const QString &id)
{
    if (id != m_id)
        m_familyChoice.reset();
    m_id = id;
    refresh();
}

void SegmentPanel::refresh()
{
    const Harness &h = m_document->model()->harness();
    const LayoutSegment *s = m_document->model()->find<LayoutSegment>(m_id);
    if (!s)
        return;
    const QSignalBlocker b1(m_length), b2(m_label);
    m_length->setText(s->lengthMm > 0 ? formatLength(s->lengthMm, h.info.units, false) : QString());
    m_units->setText(unitsSymbol(h.info.units));
    m_label->setText(s->label);
    const auto usage = calc::wiresBySegment(h, calc::LayoutGraph(h)).value(m_id);
    m_wireCount->setText(tr("%n wire(s) pass through this segment:", nullptr, int(usage.size())));
    m_wires->clear();
    for (const QString &wireId : usage) {
        if (const Wire *w = m_document->model()->find<Wire>(wireId))
            m_wires->addItem(describeWire(h, *w));
    }

    // Bundle diameter (plan 8.2).
    const Library &library = m_document->model()->library();
    const Units units = h.info.units;
    const calc::BundleDiameter bundle = calc::bundleDiameter(h, library, usage);
    m_bundle->setText(tr("\u00d8 %1").arg(formatLength(bundle.diameterMm, units)));
    m_bundleNote->setVisible(!bundle.estimatedWireIds.empty());
    m_bundleNote->setText(tr("~ %n wire(s) have no outer diameter in the parts library for their type and "
                             "gauge; their size is estimated from the gauge.",
                             nullptr, int(bundle.estimatedWireIds.size())));

    // Sleeving (plan 8.3). Family: the assigned sleeving's, else the user's pick, else the first.
    const Sleeving *assigned = library.sleeving(s->sleevingId);
    const QStringList families = library.sleevingFamilies();
    QString family;
    if (m_familyChoice && (m_familyChoice->isEmpty() || families.contains(*m_familyChoice)))
        family = *m_familyChoice;
    else if (assigned)
        family = assigned->family;
    else if (!families.isEmpty())
        family = families.first();
    const QSignalBlocker b3(m_family), b4(m_size);
    m_family->clear();
    m_family->addItem(tr("None"), QString());
    for (const QString &f : families)
        m_family->addItem(f, f);
    m_family->setCurrentIndex(std::max(0, m_family->findData(family)));

    m_size->clear();
    std::vector<const Sleeving *> sizes;
    for (const Sleeving &entry : library.sleevings) {
        if (entry.family == family)
            sizes.push_back(&entry);
    }
    std::sort(sizes.begin(), sizes.end(),
              [](const Sleeving *a, const Sleeving *b) { return a->innerDiameterMm < b->innerDiameterMm; });
    m_size->addItem(family.isEmpty() ? QString() : tr("Choose a size"), QString());
    for (const Sleeving *entry : sizes) {
        QString text = tr("%1 (%2 inside)").arg(entry->sizeLabel, formatLength(entry->innerDiameterMm, units));
        if (entry->approximate)
            text += tr(" - approx.");
        m_size->addItem(text, entry->id);
    }
    m_size->setCurrentIndex(std::max(0, m_size->findData(s->sleevingId)));
    m_size->setEnabled(!family.isEmpty());

    const double margin = h.settings.sleevingDiameterMarginPercent;
    m_fit->setVisible(assigned != nullptr);
    if (assigned) {
        switch (calc::checkSleeving(*assigned, bundle.diameterMm, margin)) {
        case calc::SleevingFit::Fits:
            m_fit->setText(tr("Assigned %1 %2 fits.").arg(assigned->family, assigned->sizeLabel));
            break;
        case calc::SleevingFit::TooSmall:
            m_fit->setText(tr("Assigned %1 %2 is too small for this bundle.").arg(assigned->family, assigned->sizeLabel));
            break;
        case calc::SleevingFit::ShrinkTooLoose:
            m_fit->setText(tr("Assigned %1 %2 fits over the bundle but will not shrink tight on it.")
                               .arg(assigned->family, assigned->sizeLabel));
            break;
        }
    }

    m_recommendedId.clear();
    if (family.isEmpty() || usage.empty()) {
        m_recommendation->setText(usage.empty() ? tr("No wires pass through this segment yet.")
                                                : tr("Pick a sleeving type to get a size recommendation."));
    } else {
        const calc::SleevingRecommendation rec = calc::recommendSleeving(library, family, bundle.diameterMm, margin);
        const QString needed = formatLength(rec.requiredInnerDiameterMm, units);
        if (!rec.sleeving) {
            m_recommendation->setText(tr("Nothing in %1 is big enough (needs %2 inside).").arg(family, needed));
        } else {
            m_recommendedId = rec.sleeving->id;
            QString text = tr("Recommended: %1 (needs %2 inside, %3 % margin).")
                               .arg(rec.sleeving->sizeLabel, needed)
                               .arg(QLocale().toString(margin));
            if (rec.fit == calc::SleevingFit::ShrinkTooLoose)
                text += QLatin1Char(' ') + tr("It will not shrink tight on this bundle.");
            if (rec.sleeving->approximate)
                text += QLatin1Char(' ') + tr("Catalog size is approximate.");
            m_recommendation->setText(text);
        }
    }
    m_useRecommended->setEnabled(!m_recommendedId.isEmpty() && m_recommendedId != s->sleevingId);
}

void SegmentPanel::commit()
{
    HarnessModel *model = m_document->model();
    const LayoutSegment *current = model->find<LayoutSegment>(m_id);
    if (!current)
        return;
    const Units units = model->harness().info.units;
    LayoutSegment updated = *current;
    updated.label = m_label->text();
    const QString text = m_length->text();
    const QString shown = current->lengthMm > 0 ? formatLength(current->lengthMm, units, false) : QString();
    if (text != shown) {
        const auto mm = text.trimmed().isEmpty() ? std::optional(0.0) : parseLength(text, units);
        if (!mm) {
            refresh();
            return;
        }
        updated.lengthMm = *mm;
    }
    if (updated != *current)
        m_document->undoStack()->push(new UpdateEntityCommand<LayoutSegment>(model, updated, tr("Edit segment")));
}

// ---- LayoutNodePanel ------------------------------------------------------------------

LayoutNodePanel::LayoutNodePanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_ref(new QComboBox(this))
    , m_segments(new QLabel(this))
{
    m_ref->setToolTip(tr("What this point in the layout is: a connector, a splice, a wire end, or a "
                         "plain branch point."));
    auto *form = new QFormLayout(this);
    form->addRow(tr("Represents:"), m_ref);
    form->addRow(tr("Segments:"), m_segments);
    connect(m_ref, &QComboBox::activated, this, &LayoutNodePanel::commit);
    HarnessModel *model = document->model();
    for (auto signal : {&HarnessModel::entityAdded, &HarnessModel::entityChanged, &HarnessModel::entityRemoved})
        connect(model, signal, this, [this] {
            if (isVisible())
                refresh();
        });
}

void LayoutNodePanel::setNode(const QString &id)
{
    m_id = id;
    refresh();
}

void LayoutNodePanel::refresh()
{
    const Harness &h = m_document->model()->harness();
    const LayoutNode *node = m_document->model()->find<LayoutNode>(m_id);
    if (!node)
        return;
    QSet<QString> taken;
    for (const LayoutNode &n : h.layoutNodes) {
        if (n.id != m_id)
            taken.insert(n.refId);
        for (const QString &id : n.sharedSpliceIds)
            taken.insert(id);
    }
    const QSignalBlocker blocker(m_ref);
    m_ref->clear();
    m_ref->addItem(tr("Branch point (nothing)"), QString());
    for (const LayoutRef &ref : layoutRefs(h)) {
        if (!taken.contains(ref.refId))
            m_ref->addItem(ref.label, ref.refId);
    }
    m_ref->setCurrentIndex(std::max(0, m_ref->findData(node->refId)));
    int segments = 0;
    for (const LayoutSegment &s : h.layoutSegments)
        segments += (s.nodeA == m_id || s.nodeB == m_id) ? 1 : 0;
    m_segments->setText(QString::number(segments));
}

void LayoutNodePanel::commit()
{
    HarnessModel *model = m_document->model();
    const LayoutNode *current = model->find<LayoutNode>(m_id);
    if (!current)
        return;
    LayoutNode updated = *current;
    updated.refId = m_ref->currentData().toString();
    updated.type = LayoutNodeType::Branch;
    for (const LayoutRef &ref : layoutRefs(model->harness())) {
        if (ref.refId == updated.refId)
            updated.type = ref.nodeType;
    }
    if (updated.type != LayoutNodeType::Splice)
        updated.sharedSpliceIds.clear(); // only a splice point holds splices side by side
    if (updated != *current)
        m_document->undoStack()->push(new UpdateEntityCommand<LayoutNode>(model, updated, tr("Edit layout node")));
}

} // namespace tbh::app
