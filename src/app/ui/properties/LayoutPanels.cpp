// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/LayoutPanels.h"

#include "Document.h"
#include "calc/WireLength.h"
#include "commands/EntityCommands.h"
#include "model/Queries.h"
#include "ui/Format.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>

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
{
    m_length->setPlaceholderText(tr("enter the branch length"));
    m_length->setToolTip(tr("Measured branch length. The drawing is not to scale; this value is used "
                            "for wire lengths."));
    m_label->setPlaceholderText(tr("optional, e.g. Trunk"));
    auto *lengthRow = new QHBoxLayout;
    lengthRow->addWidget(m_length, 1);
    lengthRow->addWidget(m_units);
    auto *form = new QFormLayout;
    form->addRow(tr("Length:"), lengthRow);
    form->addRow(tr("&Label:"), m_label);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_wireCount);
    layout->addWidget(m_wires, 1);

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
}

void SegmentPanel::setSegment(const QString &id)
{
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
    if (updated != *current)
        m_document->undoStack()->push(new UpdateEntityCommand<LayoutNode>(model, updated, tr("Edit layout node")));
}

} // namespace tbh::app
