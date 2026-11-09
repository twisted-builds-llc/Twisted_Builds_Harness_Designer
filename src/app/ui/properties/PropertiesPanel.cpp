// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/PropertiesPanel.h"

#include "SelectionModel.h"
#include "ui/HarnessPropertiesPanel.h"
#include "ui/properties/ConnectorPanel.h"
#include "ui/properties/WirePanel.h"

#include <QLabel>

namespace tbh::app {

PropertiesPanel::PropertiesPanel(Document *document, SelectionModel *selection, QWidget *parent)
    : QStackedWidget(parent)
    , m_selection(selection)
    , m_harness(new HarnessPropertiesPanel(document, this))
    , m_connector(new ConnectorPanel(document, this))
    , m_wires(new WirePanel(document, this))
    , m_splice(new SplicePanel(document, this))
    , m_mixed(new QLabel(this))
{
    m_mixed->setAlignment(Qt::AlignCenter);
    m_mixed->setEnabled(false);
    for (QWidget *page : {static_cast<QWidget *>(m_harness), static_cast<QWidget *>(m_connector),
                          static_cast<QWidget *>(m_wires), static_cast<QWidget *>(m_splice),
                          static_cast<QWidget *>(m_mixed)})
        addWidget(page);
    connect(selection, &SelectionModel::selectionChanged, this, &PropertiesPanel::update);
    update();
}

void PropertiesPanel::update()
{
    const Selection &s = m_selection->selection();
    const qsizetype items = s.connectors.size() + s.splices.size();
    if (s.isEmpty()) {
        setCurrentWidget(m_harness);
    } else if (s.connectors.size() == 1 && items == 1 && s.wires.isEmpty()) {
        m_connector->setConnector(*s.connectors.begin());
        setCurrentWidget(m_connector);
    } else if (s.splices.size() == 1 && items == 1 && s.wires.isEmpty()) {
        m_splice->setSplice(*s.splices.begin());
        setCurrentWidget(m_splice);
    } else if (items == 0) {
        m_wires->setWires(s.wires);
        setCurrentWidget(m_wires);
    } else {
        m_mixed->setText(tr("%n item(s) selected", nullptr, int(items + s.wires.size())));
        setCurrentWidget(m_mixed);
    }
}

} // namespace tbh::app
