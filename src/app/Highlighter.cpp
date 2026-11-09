// SPDX-License-Identifier: GPL-3.0-or-later
#include "Highlighter.h"

#include "SelectionModel.h"

#include <QTimer>

namespace tbh::app {

Highlighter::Highlighter(HarnessModel *model, SelectionModel *selection, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_selection(selection)
{
    connect(selection, &SelectionModel::selectionChanged, this, &Highlighter::schedule);
    connect(model, &HarnessModel::harnessReset, this, &Highlighter::schedule);
    connect(model, &HarnessModel::entityAdded, this, &Highlighter::schedule);
    connect(model, &HarnessModel::entityChanged, this, &Highlighter::schedule);
    connect(model, &HarnessModel::entityRemoved, this, &Highlighter::schedule);
}

void Highlighter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    recompute();
}

void Highlighter::schedule()
{
    if (m_pending)
        return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        recompute();
    });
}

void Highlighter::recompute()
{
    const Selection &s = m_selection->selection();
    if (!m_enabled || s.isEmpty()) {
        if (m_current) {
            m_current.reset();
            emit changed();
        }
        return;
    }
    calc::HighlightSeed seed;
    seed.connectors = s.connectors;
    seed.pins = s.pins;
    seed.splices = s.splices;
    seed.wires = s.wires;
    seed.layoutNodes = s.layoutNodes;
    seed.layoutSegments = s.layoutSegments;
    const Harness &h = m_model->harness();
    m_current = calc::relatedItems(h, calc::LayoutGraph(h), seed);
    emit changed();
}

} // namespace tbh::app
