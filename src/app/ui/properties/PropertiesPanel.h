// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStackedWidget>

class QLabel;

namespace tbh::app {

class CablePanel;
class ConnectorPanel;
class Document;
class FreeEndEditor;
class HarnessPanel;
class AccessoryPanel;
class LayoutNodePanel;
class SegmentPanel;
class SelectionModel;
class SplicePanel;
class WirePanel;

// Shows the editor that fits the current selection: harness (nothing selected),
// connector, splice, free end, cable, or wire(s).
class PropertiesPanel : public QStackedWidget
{
    Q_OBJECT

public:
    PropertiesPanel(Document *document, SelectionModel *selection, QWidget *parent = nullptr);

    ConnectorPanel *connectorPanel() const { return m_connector; }
    CablePanel *cablePanel() const { return m_cable; }

private:
    void update();

    SelectionModel *m_selection;
    HarnessPanel *m_harness;
    ConnectorPanel *m_connector;
    WirePanel *m_wires;
    CablePanel *m_cable;
    SplicePanel *m_splice;
    FreeEndEditor *m_freeEnd;
    SegmentPanel *m_segment;
    LayoutNodePanel *m_node;
    AccessoryPanel *m_accessory;
    QLabel *m_mixed;
};

} // namespace tbh::app
