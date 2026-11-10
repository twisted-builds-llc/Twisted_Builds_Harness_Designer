// SPDX-License-Identifier: GPL-3.0-or-later
#include "bom/BuildSheets.h"

#include "model/Queries.h"

#include <QCoreApplication>

#include <algorithm>

namespace tbh::bom {

namespace {

class Tr
{
    Q_DECLARE_TR_FUNCTIONS(BuildSheets)
};

// Negative: a before b.
int compareGauge(const Gauge &a, const Gauge &b)
{
    if (a.system != b.system)
        return a.system == GaugeSystem::Awg ? -1 : 1;
    if (a.value == b.value)
        return 0;
    // Thickest first: a lower AWG number, or a larger mm2 area.
    const bool aFirst = a.system == GaugeSystem::Awg ? a.value < b.value : a.value > b.value;
    return aFirst ? -1 : 1;
}

PinoutRow wireRow(const Harness &harness, const Wire &wire, bool atEndA)
{
    PinoutRow row;
    row.wireId = wire.id;
    row.wireName = wire.name;
    row.baseColor = wire.baseColor;
    row.stripeColor = wire.stripeColor;
    row.gauge = wire.gauge;
    row.otherEnd = describeWireEnd(harness, atEndA ? wire.endB : wire.endA);
    return row;
}

// Natural order ("W2" before "W10"), case-insensitive. Done by hand instead of with
// QCollator, whose numeric mode is ignored under the C locale (LANG=C), so the printed
// cut list comes out the same on every system.
int compareNatural(const QString &a, const QString &b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            // Compare the whole digit runs as numbers: skip leading zeros, then the
            // longer run is bigger, then compare digit by digit.
            while (i < a.size() && a[i] == u'0')
                ++i;
            while (j < b.size() && b[j] == u'0')
                ++j;
            const qsizetype startA = i, startB = j;
            while (i < a.size() && a[i].isDigit())
                ++i;
            while (j < b.size() && b[j].isDigit())
                ++j;
            const qsizetype lenA = i - startA, lenB = j - startB;
            if (lenA != lenB)
                return lenA < lenB ? -1 : 1;
            if (const int c = QStringView(a).sliced(startA, lenA).compare(QStringView(b).sliced(startB, lenB)))
                return c;
            continue;
        }
        const QChar ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    // One name is a prefix of the other: the shorter one comes first.
    const bool aDone = i >= a.size(), bDone = j >= b.size();
    return aDone == bDone ? 0 : (aDone ? -1 : 1);
}

} // namespace

std::vector<CutLine> cutListInCuttingOrder(std::vector<CutLine> lines)
{
    std::stable_sort(lines.begin(), lines.end(), [](const CutLine &a, const CutLine &b) {
        if (const int g = compareGauge(a.gauge, b.gauge))
            return g < 0;
        if (const int c = a.baseColor.compare(b.baseColor, Qt::CaseInsensitive))
            return c < 0;
        if (const int s = a.stripeColor.compare(b.stripeColor, Qt::CaseInsensitive))
            return s < 0;
        if (a.name.isEmpty() != b.name.isEmpty())
            return b.name.isEmpty();
        return compareNatural(a.name, b.name) < 0;
    });
    return lines;
}

std::vector<PinoutTable> pinoutTables(const Harness &harness)
{
    std::vector<PinoutTable> tables;
    for (const Connector &c : harness.connectors) {
        PinoutTable table{c.label, c.partNumber, c.description, {}};
        for (const Pin &p : c.pins) {
            const PinRef ref{c.id, p.id};
            const size_t before = table.rows.size();
            for (const Wire &w : harness.wires) {
                for (const bool atEndA : {true, false}) {
                    const auto *end = std::get_if<PinRef>(atEndA ? &w.endA : &w.endB);
                    if (end && *end == ref)
                        table.rows.push_back(wireRow(harness, w, atEndA));
                }
            }
            if (table.rows.size() == before)
                table.rows.emplace_back();
            for (size_t i = before; i < table.rows.size(); ++i) {
                table.rows[i].pin = p.label;
                table.rows[i].circuit = p.circuitText;
                table.rows[i].cavityPlug = p.isCavityPlug;
            }
        }
        tables.push_back(std::move(table));
    }
    for (const Splice &s : harness.splices) {
        PinoutTable table{s.label, s.partNumber, Tr::tr("Splice"), {}};
        for (const Wire &w : harness.wires) {
            for (const bool atEndA : {true, false}) {
                const auto *end = std::get_if<SpliceRef>(atEndA ? &w.endA : &w.endB);
                if (end && end->spliceId == s.id)
                    table.rows.push_back(wireRow(harness, w, atEndA));
            }
        }
        if (!table.rows.empty())
            tables.push_back(std::move(table));
    }
    return tables;
}

} // namespace tbh::bom
