// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Twist.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tbh::app {

TwistPoint twistAt(int strand, int strands, qreal turns)
{
    const qreal angle = 2 * std::numbers::pi * (turns + qreal(strand) / std::max(strands, 1));
    return {-std::cos(angle), std::sin(angle) > 1e-9};
}

void drawTwist(const std::vector<TwistStrand> &strands,
               const std::function<void(size_t strand, const QPolygonF &run)> &drawRun)
{
    for (const bool front : {false, true}) {
        for (size_t s = 0; s < strands.size(); ++s) {
            const TwistStrand &strand = strands[s];
            QPolygonF run;
            for (size_t k = 0; k < strand.points.size(); ++k) {
                if (strand.front[k] == front) {
                    // A run starts on the point before it, so runs meet without a gap.
                    if (run.isEmpty() && k > 0)
                        run.append(strand.points[k - 1]);
                    run.append(strand.points[k]);
                } else if (!run.isEmpty()) {
                    run.append(strand.points[k]);
                    drawRun(s, run);
                    run.clear();
                }
            }
            if (run.size() >= 2)
                drawRun(s, run);
        }
    }
}

} // namespace tbh::app
