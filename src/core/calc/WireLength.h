// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Wire routing through the layout graph and wire length (PROJECT_PLAN.md 5.5, 8.1).

#include "model/Harness.h"

#include <QHash>

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

// Layout node key of a wire end: connector id, splice id, or freeEndPlacementKey().
// Layout nodes reference ends through LayoutNode::refId with the same key.
QString endKey(const Wire &wire, bool endB);

// Allowance category of a wire end.
EndType endType(const WireEnd &end);

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

private:
    struct Edge
    {
        QString segmentId;
        QString otherNode;
    };
    Route shortestPath(const QString &from, const QString &to) const;
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

struct Problem
{
    enum class Kind { UnroutedWire, SegmentWithoutLength };
    Kind kind;
    QString itemId; // wire or segment id
    QString message;
};

// Issues the user should fix before lengths can be trusted.
std::vector<Problem> findProblems(const Harness &harness);

} // namespace tbh::calc
