// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointF>
#include <QPolygonF>

#include <functional>
#include <vector>

namespace tbh::app {

// Twisted strands drawn as a helix seen from the side. Strand `strand` of `strands` sits at
// angle 2π * (turns + strand / strands) around the bundle's axis; `offset` is where that puts
// it across the bundle (-1 = first side, +1 = the other) and `front` whether it faces the
// viewer. Two strands only cross where one is in front and the other behind, so drawing the
// back runs first and the front runs over them shows which wire goes over.
struct TwistPoint
{
    qreal offset = 0;
    bool front = false;
};
TwistPoint twistAt(int strand, int strands, qreal turns);

// A strand as points along its path, each with whether it is in front there.
struct TwistStrand
{
    std::vector<QPointF> points;
    std::vector<bool> front; // one per point
};
// Splits each strand into runs behind and in front and calls `drawRun` for every run behind,
// then every run in front.
void drawTwist(const std::vector<TwistStrand> &strands,
               const std::function<void(size_t strand, const QPolygonF &run)> &drawRun);

} // namespace tbh::app
