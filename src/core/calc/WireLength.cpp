// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/WireLength.h"

#include "calc/Bundle.h"
#include "model/Queries.h"

#include <QCoreApplication>
#include <QSet>

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
    return freeEndPlacementKey(wire.id, endB);
}

EndType endType(const WireEnd &end)
{
    if (std::holds_alternative<PinRef>(end))
        return EndType::Pin;
    if (std::holds_alternative<SpliceRef>(end))
        return EndType::Splice;
    switch (std::get<FreeEnd>(end).kind) {
    case FreeEndKind::FlyingLead:
        return EndType::FlyingLead;
    case FreeEndKind::Eyelet:
        return EndType::Eyelet;
    case FreeEndKind::RingTerminal:
        return EndType::RingTerminal;
    case FreeEndKind::Bare:
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
        for (const QString &id : n.sharedSpliceIds)
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

Route LayoutGraph::shortestPath(const QString &from, const QString &to) const
{
    if (from == to)
        return {RouteStatus::Ok, {}};
    // Dijkstra on entered segment lengths.
    QHash<QString, double> distance;
    QHash<QString, std::pair<QString, QString>> previous; // node -> (prev node, segment)
    using Item = std::pair<double, QString>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    distance.insert(from, 0);
    queue.push({0, from});
    while (!queue.empty()) {
        const auto [d, node] = queue.top();
        queue.pop();
        if (d > distance.value(node, std::numeric_limits<double>::infinity()))
            continue;
        if (node == to)
            break;
        for (const Edge &edge : m_adjacency.value(node)) {
            const double next = d + std::max(0.0, m_lengths.value(edge.segmentId));
            if (next < distance.value(edge.otherNode, std::numeric_limits<double>::infinity())) {
                distance.insert(edge.otherNode, next);
                previous.insert(edge.otherNode, {node, edge.segmentId});
                queue.push({next, edge.otherNode});
            }
        }
    }
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
            const auto it = s.stripAllowanceMm.find(endType(end));
            return it == s.stripAllowanceMm.end() ? 0.0 : it->second;
        };
        for (const QString &id : route.segmentIds)
            result.baseMm += graph.segmentLengthMm(id);
        result.endAllowanceAMm = allowance(wire.endA);
        result.endAllowanceBMm = allowance(wire.endB);
        const double beforeLoop = result.baseMm + result.endAllowanceAMm + result.endAllowanceBMm;
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
    for (const LayoutSegment &s : harness.layoutSegments) {
        if (s.lengthMm <= 0) {
            const QString name = s.label.isEmpty() ? Tr::tr("(unlabeled)") : s.label;
            problems.push_back({Problem::Kind::SegmentWithoutLength, s.id,
                                Tr::tr("Layout segment %1 has no length.").arg(name)});
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
