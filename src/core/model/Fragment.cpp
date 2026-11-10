// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Fragment.h"

#include <QHash>

namespace tbh {

Harness extractFragment(const Harness &harness, const QSet<QString> &connectorIds,
                        const QSet<QString> &spliceIds, const QSet<QString> &freeEndIds)
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
    // Free ends come along when selected, or when one of their wires has its other end on a
    // copied connector or splice (so copying a connector keeps its flying leads).
    auto onCopiedItem = [&](const WireEnd &end) {
        if (const auto *pin = std::get_if<PinRef>(&end))
            return connectorIds.contains(pin->connectorId);
        if (const auto *splice = std::get_if<SpliceRef>(&end))
            return spliceIds.contains(splice->spliceId);
        return false;
    };
    QSet<QString> freeEnds = freeEndIds;
    for (const Wire &w : harness.wires) {
        if (const auto *a = std::get_if<FreeEndRef>(&w.endA); a && onCopiedItem(w.endB))
            freeEnds.insert(a->freeEndId);
        if (const auto *b = std::get_if<FreeEndRef>(&w.endB); b && onCopiedItem(w.endA))
            freeEnds.insert(b->freeEndId);
    }
    for (const FreeEnd &f : harness.freeEnds) {
        if (freeEnds.contains(f.id)) {
            fragment.freeEnds.push_back(f);
            copyPlacement(f.id);
        }
    }
    // A wire comes along when both ends are on copied items.
    auto inside = [&](const WireEnd &end) {
        if (const auto *free = std::get_if<FreeEndRef>(&end))
            return freeEnds.contains(free->freeEndId);
        return onCopiedItem(end);
    };
    for (const Wire &w : harness.wires) {
        if (inside(w.endA) && inside(w.endB)) {
            Wire copy = w;
            copy.routeSegmentIds.clear();
            fragment.wires.push_back(copy);
            copyPlacement(w.id);
        }
    }
    return fragment;
}

void assignNewIds(Harness &fragment)
{
    QHash<QString, QString> connectorMap, pinMap, spliceMap, freeEndMap;
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
    for (FreeEnd &f : fragment.freeEnds) {
        const QString newFreeEndId = newId();
        movePlacement(f.id, newFreeEndId);
        freeEndMap.insert(f.id, newFreeEndId);
        f.id = newFreeEndId;
    }
    auto remapEnd = [&](WireEnd &end) {
        if (auto *pin = std::get_if<PinRef>(&end)) {
            pin->pinId = pinMap.value(pin->connectorId + QLatin1Char('/') + pin->pinId);
            pin->connectorId = connectorMap.value(pin->connectorId);
        } else if (auto *splice = std::get_if<SpliceRef>(&end)) {
            splice->spliceId = spliceMap.value(splice->spliceId);
        } else {
            auto &free = std::get<FreeEndRef>(end);
            free.freeEndId = freeEndMap.value(free.freeEndId);
        }
    };
    for (Wire &w : fragment.wires) {
        const QString newWireId = newId();
        movePlacement(w.id, newWireId);
        w.id = newWireId;
        remapEnd(w.endA);
        remapEnd(w.endB);
    }
    fragment.schematic.placements = std::move(placements);
}

} // namespace tbh
