// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Fragment.h"

#include <QHash>

namespace tbh {

Harness extractFragment(const Harness &harness, const QSet<QString> &connectorIds,
                        const QSet<QString> &spliceIds)
{
    Harness fragment;
    fragment.info = harness.info;
    auto copyPlacement = [&](const QString &key) {
        const auto it = harness.schematic.placements.find(key);
        if (it != harness.schematic.placements.end())
            fragment.schematic.placements[key] = it->second;
    };
    for (const Connector &c : harness.connectors) {
        if (connectorIds.contains(c.id)) {
            fragment.connectors.push_back(c);
            copyPlacement(c.id);
        }
    }
    for (const Splice &s : harness.splices) {
        if (spliceIds.contains(s.id)) {
            fragment.splices.push_back(s);
            copyPlacement(s.id);
        }
    }
    // A wire comes along when every end is free or on a copied item, and at least one is not free.
    auto inside = [&](const WireEnd &end) {
        if (const auto *pin = std::get_if<PinRef>(&end))
            return connectorIds.contains(pin->connectorId);
        if (const auto *splice = std::get_if<SpliceRef>(&end))
            return spliceIds.contains(splice->spliceId);
        return true;
    };
    auto isFree = [](const WireEnd &end) { return std::holds_alternative<FreeEnd>(end); };
    for (const Wire &w : harness.wires) {
        if (inside(w.endA) && inside(w.endB) && !(isFree(w.endA) && isFree(w.endB))) {
            Wire copy = w;
            copy.routeSegmentIds.clear();
            fragment.wires.push_back(copy);
            copyPlacement(w.id);
            copyPlacement(freeEndPlacementKey(w.id, false));
            copyPlacement(freeEndPlacementKey(w.id, true));
        }
    }
    return fragment;
}

void assignNewIds(Harness &fragment)
{
    QHash<QString, QString> connectorMap, pinMap, spliceMap;
    std::map<QString, SchematicPlacement> placements;
    auto movePlacement = [&](const QString &oldKey, const QString &newKey) {
        const auto it = fragment.schematic.placements.find(oldKey);
        if (it != fragment.schematic.placements.end())
            placements[newKey] = it->second;
    };

    for (Connector &c : fragment.connectors) {
        const QString oldConnectorId = c.id;
        c.id = newId();
        movePlacement(oldConnectorId, c.id);
        connectorMap.insert(oldConnectorId, c.id);
        for (Pin &p : c.pins) {
            const QString newPinId = newId();
            // Pin ids are only unique per connector, so key by both.
            pinMap.insert(oldConnectorId + QLatin1Char('/') + p.id, newPinId);
            p.id = newPinId;
        }
    }
    for (Splice &s : fragment.splices) {
        const QString newSpliceId = newId();
        movePlacement(s.id, newSpliceId);
        spliceMap.insert(s.id, newSpliceId);
        s.id = newSpliceId;
    }
    auto remapEnd = [&](WireEnd &end) {
        if (auto *pin = std::get_if<PinRef>(&end)) {
            pin->pinId = pinMap.value(pin->connectorId + QLatin1Char('/') + pin->pinId);
            pin->connectorId = connectorMap.value(pin->connectorId);
        } else if (auto *splice = std::get_if<SpliceRef>(&end)) {
            splice->spliceId = spliceMap.value(splice->spliceId);
        }
    };
    for (Wire &w : fragment.wires) {
        const QString newWireId = newId();
        movePlacement(w.id, newWireId);
        movePlacement(freeEndPlacementKey(w.id, false), freeEndPlacementKey(newWireId, false));
        movePlacement(freeEndPlacementKey(w.id, true), freeEndPlacementKey(newWireId, true));
        w.id = newWireId;
        remapEnd(w.endA);
        remapEnd(w.endB);
    }
    fragment.schematic.placements = std::move(placements);
}

} // namespace tbh
