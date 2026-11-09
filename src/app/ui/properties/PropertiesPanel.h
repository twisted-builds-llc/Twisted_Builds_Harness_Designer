// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStackedWidget>

class QLabel;

namespace tbh::app {

class ConnectorPanel;
class Document;
class HarnessPanel;
class LayoutNodePanel;
class SegmentPanel;
class SelectionModel;
class SplicePanel;
class WirePanel;

// Shows the editor that fits the current selection: harness (nothing selected),
// connector, splice, or wire(s).
class PropertiesPanel : public QStackedWidget
{
    Q_OBJECT

public:
    PropertiesPanel(Document *document, SelectionModel *selection, QWidget *parent = nullptr);

    ConnectorPanel *connectorPanel() const { return m_connector; }

private:
    void update();

    SelectionModel *m_selection;
    HarnessPanel *m_harness;
    ConnectorPanel *m_connector;
    WirePanel *m_wires;
    SplicePanel *m_splice;
    SegmentPanel *m_segment;
    LayoutNodePanel *m_node;
    QLabel *m_mixed;
};

} // namespace tbh::app
