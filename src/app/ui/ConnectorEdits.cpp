// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ConnectorEdits.h"

#include "Document.h"
#include "commands/EditOps.h"
#include "model/Queries.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace tbh::app {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::app::ConnectorEdits)
};
} // namespace

namespace {

} // namespace

bool confirmWireRemoval(QWidget *parent, const Harness &harness, const std::vector<QString> &wireIds,
                        const QString &action)
{
    if (wireIds.empty())
        return true;
    constexpr size_t kShown = 25;
    QStringList lines;
    for (size_t i = 0; i < wireIds.size() && i < kShown; ++i) {
        for (const Wire &w : harness.wires) {
            if (w.id == wireIds[i])
                lines << QStringLiteral("• ") + describeWire(harness, w);
        }
    }
    if (wireIds.size() > kShown)
        lines << Tr::tr("...and %n more", nullptr, int(wireIds.size() - kShown));

    QMessageBox box(QMessageBox::Warning, Tr::tr("Delete wires?"),
                    Tr::tr("%1 also deletes %n wire(s):", nullptr, int(wireIds.size())).arg(action),
                    QMessageBox::Ok | QMessageBox::Cancel, parent);
    box.setInformativeText(lines.join(QLatin1Char('\n')));
    box.setDefaultButton(QMessageBox::Cancel);
    box.button(QMessageBox::Ok)->setText(Tr::tr("Delete"));
    return box.exec() == QMessageBox::Ok;
}

bool setPins(QWidget *parent, Document *document, const QString &connectorId, std::vector<Pin> pins,
             const QString &undoText)
{
    HarnessModel *model = document->model();
    const Connector *connector = model->find<Connector>(connectorId);
    if (!connector || connector->pins == pins)
        return false;
    const auto lost = wiresLostByPinChange(model->harness(), *connector, pins);
    if (!confirmWireRemoval(parent, model->harness(), lost, Tr::tr("Removing these pins")))
        return false;
    document->undoStack()->push(setConnectorPins(model, connectorId, std::move(pins), undoText).release());
    return true;
}

bool changePinCount(QWidget *parent, Document *document, const QString &connectorId, int count)
{
    const Connector *connector = document->model()->find<Connector>(connectorId);
    if (!connector)
        return false;
    return setPins(parent, document, connectorId, resizedPins(connector->pins, count),
                   Tr::tr("Change pin count"));
}

bool deleteItems(QWidget *parent, Document *document, const Selection &selection)
{
    if (selection.isEmpty())
        return false;
    HarnessModel *model = document->model();
    // Explicitly selected wires need no confirmation; wires removed as a side effect do.
    std::vector<QString> implicit;
    for (const QString &id : wiresRemovedWith(model->harness(), selection.connectors, selection.splices, {})) {
        if (!selection.wires.contains(id))
            implicit.push_back(id);
    }
    if (!confirmWireRemoval(parent, model->harness(), implicit, Tr::tr("Deleting the selection")))
        return false;
    document->undoStack()->push(
        removeItems(model, selection.connectors, selection.splices, selection.wires, selection.notes).release());
    return true;
}

} // namespace tbh::app
