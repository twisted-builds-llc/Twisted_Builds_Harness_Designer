// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Format.h"

#include <QCoreApplication>
#include <QLocale>

namespace tbh::app {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::app::Format)
};
} // namespace

QString formatLength(double mm, Units units, bool withSymbol)
{
    const int decimals = units == Units::Inches ? 2 : 1;
    const double value = mmToUnits(mm, units);
    QString text = QLocale().toString(value, 'f', decimals);
    // Drop a trailing ".0" style zero part for whole numbers.
    const QString zero = QLocale().decimalPoint() + QString(decimals, QLatin1Char('0'));
    if (text.endsWith(zero))
        text.chop(zero.size());
    return withSymbol ? text + QLatin1Char(' ') + unitsSymbol(units) : text;
}

std::optional<double> parseLength(const QString &text, Units units)
{
    const QString trimmed = text.trimmed();
    bool ok = false;
    double value = QLocale().toDouble(trimmed, &ok);
    if (!ok)
        value = QLocale::c().toDouble(trimmed, &ok);
    if (!ok || value < 0)
        return std::nullopt;
    return unitsToMm(value, units);
}

WireLengthText describeWireLength(const Harness &harness, const Wire &wire, const calc::WireLength &length)
{
    const Units units = harness.info.units;
    auto f = [units](double mm) { return formatLength(mm, units); };

    QString steps;
    if (length.status == calc::RouteStatus::Ok) {
        steps = Tr::tr("Route %1 (%2 segments) + end allowances %3 + %4")
                    .arg(f(length.baseMm))
                    .arg(length.segmentIds.size())
                    .arg(f(length.endAllowanceAMm), f(length.endAllowanceBMm));
        if (length.serviceLoopMm > 0)
            steps += Tr::tr(" + service loop %1 (%2 %)")
                         .arg(f(length.serviceLoopMm))
                         .arg(QLocale().toString(harness.settings.serviceLoopPercent));
        steps += Tr::tr(" = %1").arg(f(length.totalMm));
        if (!length.overridden && length.cutMm != length.totalMm)
            steps += Tr::tr(", rounded up to %1").arg(f(length.cutMm));
        steps += QLatin1Char('.');
    }

    if (length.overridden) {
        QString detail = Tr::tr("Manual override.");
        if (!steps.isEmpty())
            detail += QLatin1Char(' ') + Tr::tr("Calculated: %1").arg(steps);
        return {Tr::tr("%1 (override)").arg(f(length.cutMm)), detail};
    }
    if (length.status != calc::RouteStatus::Ok)
        return {Tr::tr("Not routed"), calc::routeStatusReason(harness, wire, length.status) + QLatin1Char('.')};
    return {f(length.cutMm), steps};
}

} // namespace tbh::app
