// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc/PartFit.h"

#include "calc/WireLength.h"
#include "model/Queries.h"

#include <QCoreApplication>
#include <QHash>

#include <cmath>
#include <numbers>

namespace tbh::calc {

namespace {

// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::calc::PartFit)
};

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

namespace {

QString rangeText(const QString &min, const QString &max)
{
    if (!min.isEmpty() && !max.isEmpty())
        return Tr::tr("%1 to %2").arg(min, max);
    return min.isEmpty() ? Tr::tr("up to %1").arg(max) : Tr::tr("%1 or larger").arg(min);
}

QString sizeRangeText(const PinPart &part)
{
    std::optional<Gauge> min = part.wireMin;
    std::optional<Gauge> max = part.wireMax;
    if (min && max && gaugeAreaMm2(*min) > gaugeAreaMm2(*max))
        std::swap(min, max);
    return rangeText(min ? gaugeText(*min) : QString(), max ? gaugeText(*max) : QString());
}

QString millimeters(double value)
{
    return Tr::tr("%1 mm").arg(QString::number(value, 'g', 4));
}

QString diameterRangeText(const PinPart &part)
{
    return rangeText(part.insulationMinMm ? millimeters(*part.insulationMinMm) : QString(),
                     part.insulationMaxMm ? millimeters(*part.insulationMaxMm) : QString());
}

QString kindName(PinPartKind kind)
{
    switch (kind) {
    case PinPartKind::Seal:
        return Tr::tr("seal");
    case PinPartKind::Plug:
        return Tr::tr("plug");
    case PinPartKind::Terminal:
        break;
    }
    return Tr::tr("terminal");
}

QString wireName(const Wire &w)
{
    if (!w.circuitId.isEmpty())
        return w.circuitId;
    return w.name.isEmpty() ? Tr::tr("the wire") : w.name;
}

} // namespace

void addPartProblems(const Harness &harness, const Library &library, std::vector<Problem> &problems)
{
    QHash<QString, std::vector<const Wire *>> wiresAt; // "connectorId/pinId" or free end id
    for (const Wire &w : harness.wires) {
        for (const WireEnd *end : {&w.endA, &w.endB}) {
            if (const auto *pin = std::get_if<PinRef>(end))
                wiresAt[pin->connectorId + u'/' + pin->pinId].push_back(&w);
            else if (const auto *free = std::get_if<FreeEndRef>(end))
                wiresAt[free->freeEndId].push_back(&w);
        }
    }
    auto report = [&](const QString &itemId, const QString &message) {
        problems.push_back({Problem::Kind::PartMismatch, itemId, message});
    };
    auto checkSize = [&](const QString &itemId, const QString &where, const PinPart &part,
                         const std::vector<const Wire *> &wires) {
        if (wires.empty())
            return;
        Gauge gauge = wires.front()->gauge;
        if (wires.size() > 1) {
            double total = 0;
            for (const Wire *w : wires)
                total += gaugeAreaMm2(w->gauge);
            gauge = Gauge{std::round(total * 100) / 100, GaugeSystem::Mm2};
        }
        const Fit fit = wireSizeFit(part, gauge);
        if (fit != Fit::TooSmall && fit != Fit::TooLarge)
            return;
        report(itemId, wires.size() == 1
                           ? Tr::tr("%1: terminal %2 takes %3; its wire %4 is %5.")
                                 .arg(where, part.partNumber, sizeRangeText(part), wireName(*wires.front()),
                                      gaugeText(gauge))
                           : Tr::tr("%1: terminal %2 takes %3; its wires total %4.")
                                 .arg(where, part.partNumber, sizeRangeText(part), gaugeText(gauge)));
    };
    auto checkInsulation = [&](const QString &itemId, const QString &where, const PinPart &part,
                               const std::vector<const Wire *> &wires) {
        if (wires.size() != 1)
            return;
        const WireType *type = library.wireType(wires.front()->wireTypeId);
        const std::optional<double> od = type ? type->outerDiameterMm(wires.front()->gauge) : std::nullopt;
        if (!od)
            return;
        const Fit fit = insulationFit(part, *od);
        if (fit == Fit::TooSmall || fit == Fit::TooLarge)
            report(itemId, Tr::tr("%1: %2 %3 takes insulation %4; wire %5 is %6.")
                               .arg(where, kindName(part.kind), part.partNumber, diameterRangeText(part),
                                    wireName(*wires.front()), millimeters(*od)));
    };

    for (const Connector &c : harness.connectors) {
        const ConnectorPart *housing = library.connectorPart(c.partNumber);
        for (const Pin &pin : c.pins) {
            const QString where = describeWireEnd(harness, PinRef{c.id, pin.id});
            const std::vector<const Wire *> wires = wiresAt.value(c.id + u'/' + pin.id);
            for (const auto &[kind, number] : {std::pair{PinPartKind::Terminal, &pin.terminalPartNumber},
                                               std::pair{PinPartKind::Seal, &pin.sealPartNumber},
                                               std::pair{PinPartKind::Plug, &pin.plugPartNumber}}) {
                const QString partNumber = number->trimmed();
                // A plug part number left on an open cavity (older files) is not fitted.
                if (partNumber.isEmpty() || (kind == PinPartKind::Plug && !pin.isCavityPlug))
                    continue;
                if (housing && !housing->parts(kind).contains(partNumber))
                    report(c.id, Tr::tr("%1: %2 %3 is not listed for housing %4.")
                                     .arg(where, kindName(kind), partNumber, housing->partNumber));
                const PinPart *part = library.pinPart(kind, partNumber);
                if (!part)
                    continue;
                if (kind == PinPartKind::Terminal)
                    checkSize(c.id, where, *part, wires);
                if (kind != PinPartKind::Plug)
                    checkInsulation(c.id, where, *part, wires);
            }
        }
    }
    for (const FreeEnd &f : harness.freeEnds) {
        if (const PinPart *part = library.pinPart(PinPartKind::Terminal, f.terminalPartNumber))
            checkSize(f.id, describeFreeEnd(f), *part, wiresAt.value(f.id));
    }
}

} // namespace tbh::calc
