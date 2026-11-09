// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bundle diameter (PROJECT_PLAN.md 8.2) and sleeving recommendation (8.3).

#include "model/Harness.h"

namespace tbh::calc {

struct WireDiameter
{
    double odMm = 0.0;
    // True if the library has no OD for this wire type + gauge and odMm is a rough
    // estimate from the gauge alone (see estimatedOuterDiameterMm).
    bool estimated = false;
};

// Rough insulated OD from the gauge: stranded conductor (solid diameter x 1.15) plus a
// 0.5 mm wall. Only a fallback; it is always flagged as an estimate in the UI.
//   AWG n: solid diameter = 0.127 mm * 92^((36 - n) / 39)
//   mm2 A: solid diameter = sqrt(4A / pi)
double estimatedOuterDiameterMm(const Gauge &gauge);

WireDiameter wireOuterDiameter(const Library &library, const Wire &wire);

struct BundleDiameter
{
    double diameterMm = 0.0;
    int wireCount = 0;
    std::vector<QString> estimatedWireIds; // wires whose OD was estimated
};

// D = sqrt(sum(d_i^2) / k) with k = settings.packingFactor.
BundleDiameter bundleDiameter(const Harness &harness, const Library &library,
                              const std::vector<QString> &wireIds);

enum class SleevingFit {
    Fits,
    TooSmall,       // inner diameter < D * (1 + margin)
    ShrinkTooLoose, // heat shrink fits over, but its recovered diameter is larger than D
};

SleevingFit checkSleeving(const Sleeving &sleeving, double bundleMm, double marginPercent);

struct SleevingRecommendation
{
    const Sleeving *sleeving = nullptr; // null: nothing in the family is big enough
    double requiredInnerDiameterMm = 0.0;
    SleevingFit fit = SleevingFit::Fits;
};

// Smallest entry of `family` whose inner diameter is >= D * (1 + margin).
SleevingRecommendation recommendSleeving(const Library &library, const QString &family, double bundleMm,
                                         double marginPercent);

} // namespace tbh::calc
