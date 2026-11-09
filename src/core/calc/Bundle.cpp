// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/Bundle.h"

#include <QHash>

#include <cmath>
#include <numbers>

namespace tbh::calc {

double estimatedOuterDiameterMm(const Gauge &gauge)
{
    double solid = 0.0;
    if (gauge.system == GaugeSystem::Awg)
        solid = 0.127 * std::pow(92.0, (36.0 - gauge.value) / 39.0);
    else
        solid = std::sqrt(4.0 * std::max(0.0, gauge.value) / std::numbers::pi);
    return solid * 1.15 + 2 * 0.5;
}

WireDiameter wireOuterDiameter(const Library &library, const Wire &wire)
{
    if (const WireType *type = library.wireType(wire.wireTypeId)) {
        if (const auto od = type->outerDiameterMm(wire.gauge))
            return {*od, false};
    }
    return {estimatedOuterDiameterMm(wire.gauge), true};
}

BundleDiameter bundleDiameter(const Harness &harness, const Library &library, const std::vector<QString> &wireIds)
{
    QHash<QString, const Wire *> wires;
    for (const Wire &w : harness.wires)
        wires.insert(w.id, &w);
    BundleDiameter result;
    double sumOfSquares = 0.0;
    for (const QString &id : wireIds) {
        const Wire *w = wires.value(id);
        if (!w)
            continue;
        const WireDiameter d = wireOuterDiameter(library, *w);
        sumOfSquares += d.odMm * d.odMm;
        ++result.wireCount;
        if (d.estimated)
            result.estimatedWireIds.push_back(id);
    }
    const double k = harness.settings.packingFactor > 0 ? harness.settings.packingFactor : 1.0;
    result.diameterMm = std::sqrt(sumOfSquares / k);
    return result;
}

SleevingFit checkSleeving(const Sleeving &sleeving, double bundleMm, double marginPercent)
{
    const double required = bundleMm * (1.0 + marginPercent / 100.0);
    if (sleeving.innerDiameterMm < required)
        return SleevingFit::TooSmall;
    if (sleeving.kind == SleevingKind::HeatShrink && sleeving.recoveredDiameterMm
        && *sleeving.recoveredDiameterMm > bundleMm)
        return SleevingFit::ShrinkTooLoose;
    return SleevingFit::Fits;
}

SleevingRecommendation recommendSleeving(const Library &library, const QString &family, double bundleMm,
                                         double marginPercent)
{
    SleevingRecommendation result;
    result.requiredInnerDiameterMm = bundleMm * (1.0 + marginPercent / 100.0);
    for (const Sleeving &s : library.sleevings) {
        if (s.family != family || s.innerDiameterMm < result.requiredInnerDiameterMm)
            continue;
        if (!result.sleeving || s.innerDiameterMm < result.sleeving->innerDiameterMm)
            result.sleeving = &s;
    }
    if (result.sleeving)
        result.fit = checkSleeving(*result.sleeving, bundleMm, marginPercent);
    else
        result.fit = SleevingFit::TooSmall;
    return result;
}

} // namespace tbh::calc
