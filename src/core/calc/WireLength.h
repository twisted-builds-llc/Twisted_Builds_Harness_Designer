// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Wire routing through the layout graph and wire length (PROJECT_PLAN.md 5.5, 8.1).

#include "model/Harness.h"

#include <QHash>
#include <QSet>

#include <optional>

namespace tbh::calc {

enum class RouteStatus {
    Ok,
    EndANotInLayout, // no layout node references end A
    EndBNotInLayout,
    NoPath,          // both ends placed, but not connected
    InvalidOverride, // routeSegmentIds is not a connected path between the ends
};

struct Route
{
    RouteStatus status = RouteStatus::NoPath;
    std::vector<QString> segmentIds; // in order from end A
};

// Layout node key of a wire end: connector, splice or free end id.
// Layout nodes reference ends through LayoutNode::refId with the same key.
QString endKey(const Wire &wire, bool endB);

// Allowance category of a wire end.
EndType endType(const Harness &harness, const WireEnd &end);

// Adjacency view of the layout, built once and queried per wire.
class LayoutGraph
{
public:
    explicit LayoutGraph(const Harness &harness);

    // Node referencing the wire end, or empty.
    QString nodeForEnd(const Wire &wire, bool endB) const;
    // Manual override if set (validated), else the shortest path by entered length.
    // Layouts are normally trees, where the path is unique.
    Route route(const Wire &wire) const;
    double segmentLengthMm(const QString &segmentId) const { return m_lengths.value(segmentId); }
    // The two nodes of a segment (empty if there is no such segment).
    std::pair<QString, QString> segmentNodes(const QString &segmentId) const { return m_segmentNodes.value(segmentId); }
    // Node id -> distance from `from` along the loom by entered length, for every node
    // connected to it (`from` itself at 0).
    QHash<QString, double> distancesFrom(const QString &from) const;

private:
    struct Edge
    {
        QString segmentId;
        QString otherNode;
    };
    Route shortestPath(const QString &from, const QString &to) const;
    // Dijkstra on entered lengths from `from`, stopping once `to` (if given) is reached.
    // `previous` maps a node to the node and segment it is reached from.
    QHash<QString, double> search(const QString &from, const QString &to,
                                  QHash<QString, std::pair<QString, QString>> *previous) const;
    bool isPath(const std::vector<QString> &segmentIds, const QString &from, const QString &to) const;

    QHash<QString, std::vector<Edge>> m_adjacency;
    QHash<QString, std::pair<QString, QString>> m_segmentNodes;
    QHash<QString, double> m_lengths;
    QHash<QString, QString> m_nodeByRef;
};

// Rounds up to a multiple of `increment` (no rounding if increment <= 0). Values within
// 1e-9 increments of a multiple count as that multiple, so 100.0 stays 100.
double roundUpToIncrement(double value, double increment);

// Every step of 8.1, kept so the UI can explain the number:
//   base   = sum of route segment lengths
//   ends   = allowance(end A) + allowance(end B)
//   loop   = (base + ends) * serviceLoopPercent / 100
//   total  = base + ends + loop
//   cut    = roundUp(total, lengthRoundingIncrement), or the override if set
struct WireLength
{
    RouteStatus status = RouteStatus::NoPath;
    std::vector<QString> segmentIds;
    double baseMm = 0;
    double endAllowanceAMm = 0;
    double endAllowanceBMm = 0;
    double serviceLoopMm = 0;
    double totalMm = 0;
    double cutMm = 0;
    bool overridden = false;

    // True if cutMm is meaningful (routed, or overridden).
    bool isKnown() const { return overridden || status == RouteStatus::Ok; }
};

WireLength wireLength(const Harness &harness, const LayoutGraph &graph, const Wire &wire);

// Why a wire is not routed, e.g. "SENSOR : 2 is not placed in the layout". Empty for Ok.
QString routeStatusReason(const Harness &harness, const Wire &wire, RouteStatus status);

// Segment id -> ids of the routed wires passing through it.
QHash<QString, std::vector<QString>> wiresBySegment(const Harness &harness, const LayoutGraph &graph);

// Length of the run a splice point sits in: the splice node must have exactly two segments;
// the run extends both ways through further two-segment splice nodes and ends at any other
// node (connector, branch, ...). Returns the summed segment lengths, or nullopt if the node is
// not such a splice or a segment in the run has no length.
std::optional<double> spliceRunLengthMm(const Harness &harness, const QString &spliceNodeId);

// A point along a wire's route, `distanceMm` from one end (see pointAlongRoute()): inside
// `segmentId`, `offsetMm` from its end nearer that wire end (`fromNodeA` says which end of
// the segment that is), or exactly on point `atNodeId` (segmentId then empty).
struct RoutePoint
{
    QString segmentId;
    bool fromNodeA = true;
    double offsetMm = 0;
    QString atNodeId;
};
// Where the point `distanceMm` along the wire's route from end A (or end B) lies. Empty
// when the wire is not routed, a segment on the way has no length, or the distance is not
// strictly between the wire's two ends.
std::optional<RoutePoint> pointAlongRoute(const LayoutGraph &graph, const Wire &wire, bool fromEndB,
                                          double distanceMm);

// Parts of the layout that belong only to the existing (factory) harness: segments whose
// routed wires are all factory wires (at least one), and points whose segments are all such
// segments (at least one), or a factory connector's point with no segments.
struct FactoryParts
{
    QSet<QString> segmentIds;
    QSet<QString> nodeIds;
};
FactoryParts factoryOnlyParts(const Harness &harness, const QHash<QString, std::vector<QString>> &wiresBySegment);

// The factory connector (Connector::factory) nearest to layout node `nodeId` along the loom,
// by entered length, and how far it is: where an installer measures from to find a cut.
// Empty if no factory connector is placed and connected to it.
struct FactoryDistance
{
    QString connectorId;
    double distanceMm = 0;
};
std::optional<FactoryDistance> nearestFactoryConnector(const Harness &harness, const LayoutGraph &graph,
                                                       const QString &nodeId);

struct Problem
{
    // PartMismatch: itemId is the connector or free end whose part does not suit its wire
    // or housing (see calc/PartFit.h).
    enum class Kind { UnroutedWire, SegmentWithoutLength, SleevingTooSmall, DuplicateCircuitId, PartMismatch };
    Kind kind;
    QString itemId; // wire or segment id
    QString message;
};

// Issues the user should fix: unrouted wires, segments without a length, assigned
// sleeving too small for its bundle (needs the effective library), and circuit ids used by
// more than one wire (one problem per id, on its second wire), and connector parts that do
// not suit their wires or housing (partProblems()).
std::vector<Problem> findProblems(const Harness &harness, const Library &library);

} // namespace tbh::calc
