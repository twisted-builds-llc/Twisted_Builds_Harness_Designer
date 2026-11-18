// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/PropertiesPanel.h"

#include "SelectionModel.h"
#include "ui/properties/CablePanel.h"
#include "ui/properties/HarnessPanel.h"
#include "ui/properties/ConnectorPanel.h"
#include "ui/properties/LayoutPanels.h"
#include "ui/properties/WirePanel.h"

#include <QLabel>

namespace tbh::app {

PropertiesPanel::PropertiesPanel(Document *document, SelectionModel *selection, QWidget *parent)
    : QStackedWidget(parent)
    , m_selection(selection)
    , m_harness(new HarnessPanel(document, this))
    , m_connector(new ConnectorPanel(document, this))
    , m_wires(new WirePanel(document, this))
    , m_cable(new CablePanel(document, this))
    , m_splice(new SplicePanel(document, this))
    , m_freeEnd(new FreeEndEditor(document, this))
    , m_segment(new SegmentPanel(document, this))
    , m_node(new LayoutNodePanel(document, this))
    , m_accessory(new AccessoryPanel(document, this))
    , m_mixed(new QLabel(this))
{
    m_mixed->setAlignment(Qt::AlignCenter);
    m_mixed->setEnabled(false);
    for (QWidget *page : {static_cast<QWidget *>(m_harness), static_cast<QWidget *>(m_connector),
                          static_cast<QWidget *>(m_wires), static_cast<QWidget *>(m_cable),
                          static_cast<QWidget *>(m_splice),
                          static_cast<QWidget *>(m_freeEnd),
                          static_cast<QWidget *>(m_segment), static_cast<QWidget *>(m_node),
                          static_cast<QWidget *>(m_accessory),
                          static_cast<QWidget *>(m_mixed)})
        addWidget(page);
    connect(selection, &SelectionModel::selectionChanged, this, &PropertiesPanel::update);
    update();
}

void PropertiesPanel::update()
{
    const Selection &s = m_selection->selection();
    const qsizetype items = s.connectors.size() + s.splices.size() + s.freeEnds.size();
    const qsizetype layoutItems = s.layoutNodes.size() + s.layoutSegments.size() + s.accessories.size();
    const qsizetype total = items + s.wires.size() + layoutItems;
    if (s.isEmpty()) {
        setCurrentWidget(m_harness);
    } else if (s.cables.size() == 1 && total == 0) {
        m_cable->setCable(*s.cables.begin());
        setCurrentWidget(m_cable);
    } else if (total == 0) { // several cables
        m_mixed->setText(tr("%n item(s) selected", nullptr, int(s.cables.size())));
        setCurrentWidget(m_mixed);
    } else if (s.layoutSegments.size() == 1 && total == 1) {
        m_segment->setSegment(*s.layoutSegments.begin());
        setCurrentWidget(m_segment);
    } else if (s.accessories.size() == 1 && total == 1) {
        m_accessory->setAccessory(*s.accessories.begin());
        setCurrentWidget(m_accessory);
    } else if (s.layoutNodes.size() == 1 && total == 1) {
        m_node->setNode(*s.layoutNodes.begin());
        setCurrentWidget(m_node);
    } else if (layoutItems > 0) {
        m_mixed->setText(tr("%n item(s) selected", nullptr, int(total)));
        setCurrentWidget(m_mixed);
    } else if (s.connectors.size() == 1 && items == 1 && s.wires.isEmpty()) {
        m_connector->setConnector(*s.connectors.begin());
        setCurrentWidget(m_connector);
    } else if (s.splices.size() == 1 && items == 1 && s.wires.isEmpty()) {
        m_splice->setSplice(*s.splices.begin());
        setCurrentWidget(m_splice);
    } else if (s.freeEnds.size() == 1 && items == 1 && s.wires.isEmpty()) {
        m_freeEnd->setFreeEnd(*s.freeEnds.begin());
        setCurrentWidget(m_freeEnd);
    } else if (items == 0) {
        m_wires->setWires(s.wires);
        setCurrentWidget(m_wires);
    } else {
        m_mixed->setText(tr("%n item(s) selected", nullptr, int(items + s.wires.size())));
        setCurrentWidget(m_mixed);
    }
}

} // namespace tbh::app
