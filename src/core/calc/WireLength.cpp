// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/WireLength.h"

#include "calc/Bundle.h"
#include "calc/PartFit.h"
#include "model/Queries.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace tbh::calc {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::calc::WireLength)
};
} // namespace

namespace {

} // namespace

QString endKey(const Wire &wire, bool endB)
{
    const WireEnd &end = endB ? wire.endB : wire.endA;
    if (const auto *pin = std::get_if<PinRef>(&end))
        return pin->connectorId;
    if (const auto *splice = std::get_if<SpliceRef>(&end))
        return splice->spliceId;
    return std::get<FreeEndRef>(end).freeEndId;
}

EndType endType(const Harness &harness, const WireEnd &end)
{
    if (std::holds_alternative<PinRef>(end))
        return EndType::Pin;
    if (std::holds_alternative<SpliceRef>(end))
        return EndType::Splice;
    const FreeEnd *free = findFreeEnd(harness, std::get<FreeEndRef>(end).freeEndId);
    if (!free)
        return EndType::FlyingLead;
    switch (free->kind) {
    case FreeEndKind::FlyingLead:
        return EndType::FlyingLead;
    case FreeEndKind::Eyelet:
        return EndType::Eyelet;
    case FreeEndKind::RingTerminal:
        return EndType::RingTerminal;
    case FreeEndKind::Bare:
    case FreeEndKind::Cut: // a cut wire end is stripped like a bare end
        return EndType::Bare;
    }
    Q_UNREACHABLE_RETURN(EndType::Bare);
}

LayoutGraph::LayoutGraph(const Harness &harness)
{
    for (const LayoutNode &n : harness.layoutNodes) {
        m_adjacency[n.id];
        if (!n.refId.isEmpty())
            m_nodeByRef.insert(n.refId, n.id);
        for (const QString &id : n.sharedRefIds)
            m_nodeByRef.insert(id, n.id);
    }
    for (const LayoutSegment &s : harness.layoutSegments) {
        m_adjacency[s.nodeA].push_back({s.id, s.nodeB});
        m_adjacency[s.nodeB].push_back({s.id, s.nodeA});
        m_segmentNodes.insert(s.id, {s.nodeA, s.nodeB});
        m_lengths.insert(s.id, s.lengthMm);
    }
}

QString LayoutGraph::nodeForEnd(const Wire &wire, bool endB) const
{
    return m_nodeByRef.value(endKey(wire, endB));
}

Route LayoutGraph::route(const Wire &wire) const
{
    const QString from = nodeForEnd(wire, false);
    const QString to = nodeForEnd(wire, true);
    if (from.isEmpty())
        return {RouteStatus::EndANotInLayout, {}};
    if (to.isEmpty())
        return {RouteStatus::EndBNotInLayout, {}};
    if (!wire.routeSegmentIds.empty()) {
        if (!isPath(wire.routeSegmentIds, from, to))
            return {RouteStatus::InvalidOverride, {}};
        return {RouteStatus::Ok, wire.routeSegmentIds};
    }
    return shortestPath(from, to);
}

bool LayoutGraph::isPath(const std::vector<QString> &segmentIds, const QString &from, const QString &to) const
{
    QString current = from;
    for (const QString &id : segmentIds) {
        const auto it = m_segmentNodes.constFind(id);
        if (it == m_segmentNodes.cend())
            return false;
        if (it->first == current)
            current = it->second;
        else if (it->second == current)
            current = it->first;
        else
            return false;
    }
    return current == to;
}

QHash<QString, double> LayoutGraph::search(const QString &from, const QString &to,
                                          QHash<QString, std::pair<QString, QString>> *previous) const
{
    QHash<QString, double> distance;
    using Item = std::pair<double, QString>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    distance.insert(from, 0);
    queue.push({0, from});
    while (!queue.empty()) {
        const auto [d, node] = queue.top();
        queue.pop();
        if (d > distance.value(node, std::numeric_limits<double>::infinity()))
            continue;
        if (!to.isEmpty() && node == to)
            break;
        for (const Edge &edge : m_adjacency.value(node)) {
            const double next = d + std::max(0.0, m_lengths.value(edge.segmentId));
            if (next < distance.value(edge.otherNode, std::numeric_limits<double>::infinity())) {
                distance.insert(edge.otherNode, next);
                if (previous)
                    previous->insert(edge.otherNode, {node, edge.segmentId});
                queue.push({next, edge.otherNode});
            }
        }
    }
    return distance;
}

QHash<QString, double> LayoutGraph::distancesFrom(const QString &from) const
{
    return search(from, {}, nullptr);
}

Route LayoutGraph::shortestPath(const QString &from, const QString &to) const
{
    if (from == to)
        return {RouteStatus::Ok, {}};
    QHash<QString, std::pair<QString, QString>> previous; // node -> (prev node, segment)
    search(from, to, &previous);
    if (!previous.contains(to))
        return {RouteStatus::NoPath, {}};
    std::vector<QString> segments;
    for (QString node = to; node != from;) {
        const auto &[prev, segment] = previous[node];
        segments.insert(segments.begin(), segment);
        node = prev;
    }
    return {RouteStatus::Ok, segments};
}

std::optional<RoutePoint> pointAlongRoute(const LayoutGraph &graph, const Wire &wire, bool fromEndB,
                                          double distanceMm)
{
    Route route = graph.route(wire);
    if (route.status != RouteStatus::Ok || distanceMm <= 0)
        return std::nullopt;
    if (fromEndB)
        std::reverse(route.segmentIds.begin(), route.segmentIds.end());
    constexpr double kSame = 1e-6; // mm
    QString at = graph.nodeForEnd(wire, fromEndB);
    double rest = distanceMm;
    for (size_t i = 0; i < route.segmentIds.size(); ++i) {
        const QString &id = route.segmentIds[i];
        const double length = graph.segmentLengthMm(id);
        if (length <= 0)
            return std::nullopt;
        const auto [nodeA, nodeB] = graph.segmentNodes(id);
        const QString next = nodeA == at ? nodeB : nodeA;
        if (rest < length - kSame)
            return RoutePoint{id, nodeA == at, rest, {}};
        if (rest <= length + kSame) // on the point at the end of this segment
            return i + 1 < route.segmentIds.size() ? std::optional(RoutePoint{{}, true, 0, next}) : std::nullopt;
        rest -= length;
        at = next;
    }
    return std::nullopt;
}

FactoryParts factoryOnlyParts(const Harness &harness, const QHash<QString, std::vector<QString>> &wiresBySegment)
{
    QSet<QString> factoryWires;
    for (const Wire &w : harness.wires) {
        if (w.factory)
            factoryWires.insert(w.id);
    }
    QSet<QString> factoryConnectorNodes;
    for (const LayoutNode &n : harness.layoutNodes) {
        if (n.type != LayoutNodeType::Connector)
            continue;
        const auto c = std::find_if(harness.connectors.begin(), harness.connectors.end(),
                                    [&](const Connector &c) { return c.id == n.refId; });
        if (c != harness.connectors.end() && c->factory)
            factoryConnectorNodes.insert(n.id);
    }
    FactoryParts parts;
    for (const LayoutSegment &s : harness.layoutSegments) {
        const std::vector<QString> wires = wiresBySegment.value(s.id);
        // No wires routed yet: factory when it runs between two factory connectors.
        const bool factory = wires.empty()
                                 ? factoryConnectorNodes.contains(s.nodeA) && factoryConnectorNodes.contains(s.nodeB)
                                 : std::all_of(wires.begin(), wires.end(),
                                               [&](const QString &id) { return factoryWires.contains(id); });
        if (factory)
            parts.segmentIds.insert(s.id);
    }
    QHash<QString, bool> allFactory; // node -> all its segments are factory-only so far
    for (const LayoutSegment &s : harness.layoutSegments) {
        const bool factory = parts.segmentIds.contains(s.id);
        for (const QString &node : {s.nodeA, s.nodeB})
            allFactory[node] = allFactory.value(node, true) && factory;
    }
    for (const LayoutNode &n : harness.layoutNodes) {
        if (allFactory.contains(n.id)) {
            if (allFactory.value(n.id))
                parts.nodeIds.insert(n.id);
        } else if (factoryConnectorNodes.contains(n.id)) {
            parts.nodeIds.insert(n.id);
        }
    }
    return parts;
}

std::optional<FactoryDistance> nearestFactoryConnector(const Harness &harness, const LayoutGraph &graph,
                                                       const QString &nodeId)
{
    const QHash<QString, double> distances = graph.distancesFrom(nodeId);
    std::optional<FactoryDistance> nearest;
    for (const LayoutNode &n : harness.layoutNodes) {
        if (n.type != LayoutNodeType::Connector || n.id == nodeId || !distances.contains(n.id))
            continue;
        const auto c = std::find_if(harness.connectors.begin(), harness.connectors.end(),
                                    [&](const Connector &c) { return c.id == n.refId; });
        if (c == harness.connectors.end() || !c->factory)
            continue;
        const double d = distances.value(n.id);
        if (!nearest || d < nearest->distanceMm)
            nearest = FactoryDistance{c->id, d};
    }
    return nearest;
}

double roundUpToIncrement(double value, double increment)
{
    if (increment <= 0)
        return value;
    return std::ceil(value / increment - 1e-9) * increment;
}

WireLength wireLength(const Harness &harness, const LayoutGraph &graph, const Wire &wire)
{
    WireLength result;
    const Route route = graph.route(wire);
    result.status = route.status;
    result.segmentIds = route.segmentIds;
    if (route.status == RouteStatus::Ok) {
        const Settings &s = harness.settings;
        auto allowance = [&](const WireEnd &end) {
            const auto it = s.stripAllowanceMm.find(endType(harness, end));
            return it == s.stripAllowanceMm.end() ? 0.0 : it->second;
        };
        for (const QString &id : route.segmentIds)
            result.baseMm += graph.segmentLengthMm(id);
        result.endAllowanceAMm = allowance(wire.endA);
        result.endAllowanceBMm = allowance(wire.endB);
        if (isTwistedByBuilder(harness, wire.id))
            result.twistTakeUpMm = result.baseMm * s.twistTakeUpPercent / 100.0;
        const double beforeLoop =
            result.baseMm + result.endAllowanceAMm + result.endAllowanceBMm + result.twistTakeUpMm;
        result.serviceLoopMm = beforeLoop * s.serviceLoopPercent / 100.0;
        result.totalMm = beforeLoop + result.serviceLoopMm;
        result.cutMm = roundUpToIncrement(result.totalMm, s.lengthRoundingIncrementMm);
    }
    if (wire.lengthOverrideMm) {
        result.overridden = true;
        result.cutMm = *wire.lengthOverrideMm;
    }
    return result;
}

bool isTwistedByBuilder(const Harness &harness, const QString &wireId)
{
    return std::any_of(harness.cables.begin(), harness.cables.end(), [&](const Cable &c) {
        return c.twisted && c.source == CableSource::Built
               && std::find(c.wireIds.begin(), c.wireIds.end(), wireId) != c.wireIds.end();
    });
}

namespace {

// Routes of a cable's conductors, or empty if one is missing or not routed.
std::vector<Route> conductorRoutes(const LayoutGraph &graph, const Harness &harness, const Cable &cable)
{
    std::vector<Route> routes;
    for (const QString &id : cable.wireIds) {
        const auto it = std::find_if(harness.wires.begin(), harness.wires.end(), [&](const Wire &w) { return w.id == id; });
        if (it == harness.wires.end())
            return {};
        Route route = graph.route(*it);
        if (route.status != RouteStatus::Ok)
            return {};
        routes.push_back(std::move(route));
    }
    return routes;
}

} // namespace

std::optional<double> cableRunLengthMm(const LayoutGraph &graph, const Harness &harness, const Cable &cable)
{
    const std::vector<Route> routes = conductorRoutes(graph, harness, cable);
    if (routes.empty())
        return std::nullopt;
    double length = 0;
    for (const QString &segment : routes.front().segmentIds) {
        const bool shared = std::all_of(routes.begin() + 1, routes.end(), [&](const Route &r) {
            return std::find(r.segmentIds.begin(), r.segmentIds.end(), segment) != r.segmentIds.end();
        });
        if (shared)
            length += graph.segmentLengthMm(segment);
    }
    return length;
}

QHash<QString, std::vector<QString>> wiresBySegment(const Harness &harness, const LayoutGraph &graph)
{
    QHash<QString, std::vector<QString>> result;
    for (const Wire &w : harness.wires) {
        const Route route = graph.route(w);
        if (route.status != RouteStatus::Ok)
            continue;
        for (const QString &segment : route.segmentIds)
            result[segment].push_back(w.id);
    }
    return result;
}

std::optional<double> spliceRunLengthMm(const Harness &harness, const QString &spliceNodeId)
{
    QHash<QString, std::vector<const LayoutSegment *>> segmentsAt;
    for (const LayoutSegment &s : harness.layoutSegments) {
        segmentsAt[s.nodeA].push_back(&s);
        segmentsAt[s.nodeB].push_back(&s);
    }
    QHash<QString, const LayoutNode *> nodes;
    for (const LayoutNode &n : harness.layoutNodes)
        nodes.insert(n.id, &n);
    const auto isPassThroughSplice = [&](const QString &id) {
        const LayoutNode *n = nodes.value(id);
        return n && n->type == LayoutNodeType::Splice && segmentsAt.value(id).size() == 2;
    };
    if (!isPassThroughSplice(spliceNodeId))
        return std::nullopt;

    double total = 0;
    QSet<const LayoutSegment *> visited; // a loop of splices would otherwise never end
    for (const LayoutSegment *start : segmentsAt.value(spliceNodeId)) {
        const LayoutSegment *s = start;
        QString from = spliceNodeId;
        while (s && !visited.contains(s)) {
            visited.insert(s);
            if (s->lengthMm <= 0)
                return std::nullopt;
            total += s->lengthMm;
            const QString next = s->nodeA == from ? s->nodeB : s->nodeA;
            if (!isPassThroughSplice(next))
                break;
            const auto &pair = segmentsAt.value(next);
            s = pair[0] == s ? pair[1] : pair[0];
            from = next;
        }
    }
    return total;
}

QString routeStatusReason(const Harness &harness, const Wire &wire, RouteStatus status)
{
    switch (status) {
    case RouteStatus::Ok:
        return {};
    case RouteStatus::EndANotInLayout:
        return Tr::tr("%1 is not placed in the layout").arg(describeWireEnd(harness, wire.endA));
    case RouteStatus::EndBNotInLayout:
        return Tr::tr("%1 is not placed in the layout").arg(describeWireEnd(harness, wire.endB));
    case RouteStatus::NoPath:
        return Tr::tr("no layout path connects its ends");
    case RouteStatus::InvalidOverride:
        return Tr::tr("its manual route is not a connected path between its ends");
    }
    return {};
}

std::vector<Problem> findProblems(const Harness &harness, const Library &library)
{
    std::vector<Problem> problems;
    const LayoutGraph graph(harness);
    for (const Wire &w : harness.wires) {
        const Route route = graph.route(w);
        if (route.status == RouteStatus::Ok)
            continue;
        const QString reason = routeStatusReason(harness, w, route.status);
        problems.push_back({Problem::Kind::UnroutedWire, w.id,
                            Tr::tr("Wire %1 is not routed: %2.").arg(describeWire(harness, w), reason)});
    }
    addPartProblems(harness, library, problems);
    QHash<QString, int> circuitUses;
    for (const Wire &w : harness.wires) {
        const QString id = w.circuitId.trimmed();
        if (!id.isEmpty() && ++circuitUses[id.toCaseFolded()] == 2) {
            // Reported once per id, on the second wire that uses it.
            problems.push_back({Problem::Kind::DuplicateCircuitId, w.id,
                                Tr::tr("Circuit ID %1 is used by more than one wire.").arg(id)});
        }
    }
    for (const LayoutSegment &s : harness.layoutSegments) {
        if (s.lengthMm <= 0) {
            const QString name = s.label.isEmpty() ? Tr::tr("(unlabeled)") : s.label;
            problems.push_back({Problem::Kind::SegmentWithoutLength, s.id,
                                Tr::tr("Layout segment %1 has no length.").arg(name)});
        }
    }
    for (const Cable &c : harness.cables) {
        const QString name = c.label.isEmpty() ? Tr::tr("(unlabeled)") : c.label;
        const std::vector<Route> routes = conductorRoutes(graph, harness, c);
        auto sameSegments = [](std::vector<QString> a, std::vector<QString> b) {
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            return a == b;
        };
        if (c.twisted && !routes.empty()
            && !std::all_of(routes.begin() + 1, routes.end(),
                            [&](const Route &r) { return sameSegments(r.segmentIds, routes.front().segmentIds); })) {
            problems.push_back({Problem::Kind::CableSplit, c.id,
                                Tr::tr("The wires of twisted cable %1 take different routes in the layout, "
                                       "so the twist would split.")
                                    .arg(name)});
        }
        if (c.source == CableSource::Bought && !c.jacketOdMm) {
            problems.push_back({Problem::Kind::CableWithoutDiameter, c.id,
                                Tr::tr("Bought cable %1 has no jacket diameter; bundle sizes count its "
                                       "wires one by one.")
                                    .arg(name)});
        }
    }
    const auto usage = wiresBySegment(harness, graph);
    for (const LayoutSegment &s : harness.layoutSegments) {
        const Sleeving *sleeving = library.sleeving(s.sleevingId);
        if (!sleeving)
            continue;
        const BundleDiameter bundle = bundleDiameter(harness, library, usage.value(s.id));
        if (checkSleeving(*sleeving, bundle.diameterMm, harness.settings.sleevingDiameterMarginPercent)
            == SleevingFit::TooSmall) {
            const QString name = s.label.isEmpty() ? Tr::tr("(unlabeled)") : s.label;
            problems.push_back({Problem::Kind::SleevingTooSmall, s.id,
                                Tr::tr("Sleeving %1 %2 on segment %3 is too small for its bundle.")
                                    .arg(sleeving->family, sleeving->sizeLabel, name)});
        }
    }
    return problems;
}

} // namespace tbh::calc
