// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/Highlight.h"

namespace tbh::calc {

QString pinKey(const QString &connectorId, const QString &pinId)
{
    return connectorId + QLatin1Char('/') + pinId;
}

HighlightSet relatedItems(const Harness &harness, const LayoutGraph &graph, const HighlightSeed &seed)
{
    HighlightSet set;
    set.connectors = seed.connectors;
    set.pins = seed.pins;
    set.splices = seed.splices;
    set.wires = seed.wires;
    set.layoutNodes = seed.layoutNodes;
    set.layoutSegments = seed.layoutSegments;

    // Connectors whose selection is narrowed to pins.
    QSet<QString> pinnedConnectors;
    for (const QString &key : seed.pins)
        pinnedConnectors.insert(key.section(QLatin1Char('/'), 0, 0));

    // What seeded layout nodes stand for; branch points contribute their segments.
    QSet<QString> nodeRefs;
    QSet<QString> seedSegments = seed.layoutSegments;
    for (const LayoutNode &n : harness.layoutNodes) {
        if (!seed.layoutNodes.contains(n.id))
            continue;
        if (!n.refId.isEmpty())
            nodeRefs.insert(n.refId);
        else
            for (const LayoutSegment &s : harness.layoutSegments)
                if (s.nodeA == n.id || s.nodeB == n.id)
                    seedSegments.insert(s.id);
    }

    auto endTouchesSeed = [&](const Wire &w, bool endB) {
        const WireEnd &end = endB ? w.endB : w.endA;
        const QString key = endKey(w, endB);
        if (nodeRefs.contains(key))
            return true;
        if (const auto *pin = std::get_if<PinRef>(&end)) {
            if (pinnedConnectors.contains(pin->connectorId))
                return seed.pins.contains(pinKey(pin->connectorId, pin->pinId));
            return seed.connectors.contains(pin->connectorId);
        }
        if (const auto *splice = std::get_if<SpliceRef>(&end))
            return seed.splices.contains(splice->spliceId);
        return false;
    };

    QHash<QString, QString> nodeByRef;
    QHash<QString, std::pair<QString, QString>> segmentNodes;
    for (const LayoutNode &n : harness.layoutNodes)
        if (!n.refId.isEmpty())
            nodeByRef.insert(n.refId, n.id);
    for (const LayoutSegment &s : harness.layoutSegments)
        segmentNodes.insert(s.id, {s.nodeA, s.nodeB});

    for (const Wire &w : harness.wires) {
        const Route route = graph.route(w);
        bool related = seed.wires.contains(w.id) || endTouchesSeed(w, false) || endTouchesSeed(w, true);
        if (!related) {
            for (const QString &segment : route.segmentIds) {
                if (seedSegments.contains(segment)) {
                    related = true;
                    break;
                }
            }
        }
        if (!related)
            continue;

        set.wires.insert(w.id);
        for (bool endB : {false, true}) {
            const WireEnd &end = endB ? w.endB : w.endA;
            if (const auto *pin = std::get_if<PinRef>(&end)) {
                set.connectors.insert(pin->connectorId);
                set.pins.insert(pinKey(pin->connectorId, pin->pinId));
            } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
                set.splices.insert(splice->spliceId);
            } else {
                set.freeEnds.insert(freeEndPlacementKey(w.id, endB));
            }
            const QString node = nodeByRef.value(endKey(w, endB));
            if (!node.isEmpty())
                set.layoutNodes.insert(node);
        }
        if (route.status == RouteStatus::Ok) {
            for (const QString &segment : route.segmentIds) {
                set.layoutSegments.insert(segment);
                const auto nodes = segmentNodes.value(segment);
                set.layoutNodes.insert(nodes.first);
                set.layoutNodes.insert(nodes.second);
            }
        }
    }
    // Seeded segments' own nodes.
    for (const QString &segment : std::as_const(seedSegments)) {
        if (segmentNodes.contains(segment)) {
            set.layoutSegments.insert(segment);
            set.layoutNodes.insert(segmentNodes.value(segment).first);
            set.layoutNodes.insert(segmentNodes.value(segment).second);
        }
    }
    return set;
}

} // namespace tbh::calc
