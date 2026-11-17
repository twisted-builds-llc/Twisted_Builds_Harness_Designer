// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/PartFit.h"

#include <cmath>
#include <numbers>

namespace tbh::calc {

namespace {

constexpr double kTolerance = 0.02;

Fit rangeFit(double value, std::optional<double> min, std::optional<double> max)
{
    if (!min && !max)
        return Fit::Unknown;
    if (min && value < *min * (1 - kTolerance))
        return Fit::TooSmall;
    if (max && value > *max * (1 + kTolerance))
        return Fit::TooLarge;
    return Fit::Fits;
}

std::optional<double> area(const std::optional<Gauge> &gauge)
{
    return gauge ? std::optional<double>(gaugeAreaMm2(*gauge)) : std::nullopt;
}

} // namespace

double gaugeAreaMm2(const Gauge &gauge)
{
    if (gauge.system == GaugeSystem::Mm2)
        return gauge.value;
    const double diameter = 0.127 * std::pow(92.0, (36.0 - gauge.value) / 39.0);
    return std::numbers::pi * diameter * diameter / 4.0;
}

Fit wireSizeFit(const PinPart &part, const Gauge &gauge)
{
    std::optional<double> min = area(part.wireMin);
    std::optional<double> max = area(part.wireMax);
    if (min && max && *min > *max)
        std::swap(min, max); // entered largest first, e.g. "14-16 AWG"
    return rangeFit(gaugeAreaMm2(gauge), min, max);
}

Fit insulationFit(const PinPart &part, double outerDiameterMm)
{
    return rangeFit(outerDiameterMm, part.insulationMinMm, part.insulationMaxMm);
}

} // namespace tbh::calc
