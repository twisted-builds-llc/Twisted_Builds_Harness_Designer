// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/EditOps.h"

#include "commands/EntityCommands.h"
#include "model/Fragment.h"
#include "model/PinLabels.h"
#include "model/Queries.h"

#include <QCoreApplication>

#include <algorithm>

namespace tbh::app {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("tbh::app::EditOps", text);
}

} // namespace

std::vector<QString> wiresRemovedWith(const Harness &harness, const QSet<QString> &connectorIds,
                                      const QSet<QString> &spliceIds, const QSet<QString> &wireIds)
{
    QSet<QString> all = wireIds;
    for (const QString &id : connectorIds) {
        for (const QString &w : wiresOnConnector(harness, id))
            all.insert(w);
    }
    for (const QString &id : spliceIds) {
        for (const QString &w : wiresOnSplice(harness, id))
            all.insert(w);
    }
    // Keep model order so undo restores deterministically.
    std::vector<QString> ordered;
    for (const Wire &w : harness.wires) {
        if (all.contains(w.id))
            ordered.push_back(w.id);
    }
    return ordered;
}

std::unique_ptr<QUndoCommand> removeItems(HarnessModel *model, const QSet<QString> &connectorIds,
                                          const QSet<QString> &spliceIds,
                                          const QSet<QString> &wireIds)
{
    const Harness &h = model->harness();
    auto macro = std::make_unique<QUndoCommand>(tr("Delete"));
    // Children redo in order and undo in reverse: wires go first and come back last.
    // Within each list, remove from the back so the recorded indices restore the order.
    const std::vector<QString> wires = wiresRemovedWith(h, connectorIds, spliceIds, wireIds);
    for (auto it = wires.rbegin(); it != wires.rend(); ++it)
        new RemoveEntityCommand<Wire>(model, *it, {}, macro.get());
    for (auto it = h.splices.rbegin(); it != h.splices.rend(); ++it) {
        if (spliceIds.contains(it->id))
            new RemoveEntityCommand<Splice>(model, it->id, {}, macro.get());
    }
    for (auto it = h.connectors.rbegin(); it != h.connectors.rend(); ++it) {
        if (connectorIds.contains(it->id))
            new RemoveEntityCommand<Connector>(model, it->id, {}, macro.get());
    }
    return macro;
}

std::vector<QString> wiresLostByPinChange(const Harness &harness, const Connector &connector,
                                          const std::vector<Pin> &newPins)
{
    QSet<QString> kept;
    for (const Pin &p : newPins)
        kept.insert(p.id);
    QSet<QString> lost;
    for (const Pin &p : connector.pins) {
        if (!kept.contains(p.id))
            lost.insert(p.id);
    }
    return lost.isEmpty() ? std::vector<QString>{} : wiresOnPins(harness, connector.id, lost);
}

std::unique_ptr<QUndoCommand> setConnectorPins(HarnessModel *model, const QString &connectorId,
                                               std::vector<Pin> newPins, const QString &text)
{
    Connector connector = *model->find<Connector>(connectorId);
    const std::vector<QString> lost = wiresLostByPinChange(model->harness(), connector, newPins);
    connector.pins = std::move(newPins);
    auto macro = std::make_unique<QUndoCommand>(text);
    // Remove from the back so indices recorded for undo stay valid.
    std::vector<QString> ordered = lost;
    std::sort(ordered.begin(), ordered.end(), [&](const QString &a, const QString &b) {
        return model->indexOf<Wire>(a) > model->indexOf<Wire>(b);
    });
    for (const QString &wireId : ordered)
        new RemoveEntityCommand<Wire>(model, wireId, {}, macro.get());
    new UpdateEntityCommand<Connector>(model, std::move(connector), {}, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> pasteFragment(HarnessModel *model, Harness fragment, QPointF offset,
                                            QSet<QString> *newItemIds)
{
    assignNewIds(fragment);
    for (auto &[key, placement] : fragment.schematic.placements)
        placement.position += offset;
    auto placementsOf = [&](EntityKind kind, const QString &id) {
        Placements result;
        for (const QString &key : HarnessModel::placementKeys(kind, id)) {
            const auto it = fragment.schematic.placements.find(key);
            if (it != fragment.schematic.placements.end())
                result[key] = it->second;
        }
        return result;
    };

    auto macro = std::make_unique<QUndoCommand>(tr("Paste"));
    for (Connector &c : fragment.connectors) {
        if (newItemIds)
            newItemIds->insert(c.id);
        Placements p = placementsOf(EntityKind::Connector, c.id);
        new AddEntityCommand<Connector>(model, std::move(c), std::move(p), {}, macro.get());
    }
    for (Splice &s : fragment.splices) {
        if (newItemIds)
            newItemIds->insert(s.id);
        Placements p = placementsOf(EntityKind::Splice, s.id);
        new AddEntityCommand<Splice>(model, std::move(s), std::move(p), {}, macro.get());
    }
    for (Wire &w : fragment.wires) {
        Placements p = placementsOf(EntityKind::Wire, w.id);
        new AddEntityCommand<Wire>(model, std::move(w), std::move(p), {}, macro.get());
    }
    return macro;
}

std::unique_ptr<QUndoCommand> updateWires(HarnessModel *model, const std::vector<Wire> &wires,
                                          const QString &text)
{
    auto macro = std::make_unique<QUndoCommand>(text);
    for (const Wire &w : wires) {
        if (*model->find<Wire>(w.id) != w)
            new UpdateEntityCommand<Wire>(model, w, {}, macro.get());
    }
    return macro;
}

std::vector<Pin> resizedPins(const std::vector<Pin> &pins, int count)
{
    count = std::clamp(count, kMinPins, kMaxPins);
    std::vector<Pin> result(pins.begin(), pins.begin() + std::min<qsizetype>(count, qsizetype(pins.size())));
    while (qsizetype(result.size()) < count) {
        Pin pin;
        pin.id = newId();
        pin.label = result.empty() ? QStringLiteral("1") : nextPinLabel(result.back().label);
        result.push_back(std::move(pin));
    }
    return result;
}

} // namespace tbh::app
