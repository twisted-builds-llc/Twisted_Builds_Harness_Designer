// SPDX-License-Identifier: GPL-3.0-or-later
#include "bom/Bom.h"

#include "calc/WireLength.h"
#include "model/Queries.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <map>
#include <tuple>

namespace tbh::bom {

int Bom::flaggedCount() const
{
    int count = 0;
    for (const WireLine &w : wires)
        count += w.piecesWithoutLength > 0 ? 1 : 0;
    for (const PartLine &p : parts)
        count += p.partNumber.isEmpty() ? 1 : 0;
    for (const SleevingLine &s : sleeving)
        count += s.known ? 0 : 1;
    return count;
}

namespace {

// Groups part lines by (category, part number), keeping first-seen order.
class PartCounter
{
public:
    void add(PartCategory category, const QString &partNumber, const QString &description,
             const QString &reference)
    {
        const auto key = std::make_pair(int(category), partNumber.trimmed());
        auto it = m_index.find(key);
        if (it == m_index.end()) {
            it = m_index.emplace(key, m_lines.size()).first;
            m_lines.push_back(PartLine{category, partNumber.trimmed(), description, 0, {}});
        }
        PartLine &line = m_lines[it->second];
        ++line.quantity;
        if (line.description.isEmpty())
            line.description = description;
        if (!reference.isEmpty())
            line.references.append(reference);
    }

    std::vector<PartLine> take()
    {
        // Category order, then part number, with "(no part number)" last in each category.
        std::stable_sort(m_lines.begin(), m_lines.end(), [](const PartLine &a, const PartLine &b) {
            if (a.category != b.category)
                return a.category < b.category;
            if (a.partNumber.isEmpty() != b.partNumber.isEmpty())
                return b.partNumber.isEmpty();
            return a.partNumber < b.partNumber;
        });
        return std::move(m_lines);
    }

private:
    std::map<std::pair<int, QString>, size_t> m_index;
    std::vector<PartLine> m_lines;
};

} // namespace

Bom buildBom(const Harness &harness, const Library &library)
{
    Bom bom;
    const Settings &settings = harness.settings;
    const calc::LayoutGraph graph(harness);

    // Wires and cut list.
    using WireKey = std::tuple<QString, QString, int, double, QString>;
    std::map<WireKey, size_t> wireIndex;
    for (const Wire &w : harness.wires) {
        const calc::WireLength length = calc::wireLength(harness, graph, w);
        const WireType *type = library.wireType(w.wireTypeId);
        const QString typeName = type ? type->name : QString();

        CutLine cut;
        cut.wireId = w.id;
        cut.name = w.name;
        cut.endA = describeWireEnd(harness, w.endA);
        cut.endB = describeWireEnd(harness, w.endB);
        cut.baseColor = w.baseColor;
        cut.stripeColor = w.stripeColor;
        cut.gauge = w.gauge;
        cut.wireTypeName = typeName.isEmpty() ? w.wireTypeId : typeName;
        cut.overridden = length.overridden;
        if (length.isKnown())
            cut.cutMm = length.cutMm;
        bom.cutList.push_back(cut);

        const WireKey key{w.baseColor.toUpper(), w.stripeColor.toUpper(), int(w.gauge.system), w.gauge.value,
                          w.wireTypeId};
        auto it = wireIndex.find(key);
        if (it == wireIndex.end()) {
            it = wireIndex.emplace(key, bom.wires.size()).first;
            WireLine line;
            line.baseColor = w.baseColor;
            line.stripeColor = w.stripeColor;
            line.gauge = w.gauge;
            line.wireTypeId = w.wireTypeId;
            line.wireTypeName = typeName;
            bom.wires.push_back(line);
        }
        WireLine &line = bom.wires[it->second];
        ++line.pieces;
        if (cut.cutMm)
            line.lengthMm += *cut.cutMm;
        else
            ++line.piecesWithoutLength;
    }
    for (WireLine &line : bom.wires)
        line.lengthWithWasteMm = line.lengthMm * (1.0 + settings.wasteFactorWirePercent / 100.0);

    // Sleeving.
    std::map<QString, size_t> sleevingIndex;
    for (const LayoutSegment &s : harness.layoutSegments) {
        if (s.sleevingId.isEmpty())
            continue;
        auto it = sleevingIndex.find(s.sleevingId);
        if (it == sleevingIndex.end()) {
            it = sleevingIndex.emplace(s.sleevingId, bom.sleeving.size()).first;
            SleevingLine line;
            line.sleevingId = s.sleevingId;
            if (const Sleeving *entry = library.sleeving(s.sleevingId)) {
                line.family = entry->family;
                line.sizeLabel = entry->sizeLabel;
            } else {
                line.known = false;
                line.family = s.sleevingId;
            }
            bom.sleeving.push_back(line);
        }
        SleevingLine &line = bom.sleeving[it->second];
        ++line.segments;
        line.lengthMm += s.lengthMm;
    }
    for (SleevingLine &line : bom.sleeving)
        line.lengthWithWasteMm = line.lengthMm * (1.0 + settings.wasteFactorSleevingPercent / 100.0);

    // Parts.
    QSet<QString> wiredPins;
    for (const Wire &w : harness.wires) {
        for (const WireEnd *end : {&w.endA, &w.endB}) {
            if (const auto *pin = std::get_if<PinRef>(end))
                wiredPins.insert(pin->connectorId + QLatin1Char('/') + pin->pinId);
        }
    }
    PartCounter parts;
    for (const Connector &c : harness.connectors) {
        parts.add(PartCategory::Connector, c.partNumber, c.description.isEmpty() ? c.manufacturer : c.description,
                  c.label);
        for (const Pin &p : c.pins) {
            const QString reference = QStringLiteral("%1 : %2").arg(c.label, p.label);
            if (p.isCavityPlug) {
                parts.add(PartCategory::CavityPlug, p.sealPartNumber, {}, reference);
                continue;
            }
            if (!wiredPins.contains(c.id + QLatin1Char('/') + p.id))
                continue;
            parts.add(PartCategory::Terminal, p.terminalPartNumber, {}, reference);
            if (!p.sealPartNumber.trimmed().isEmpty())
                parts.add(PartCategory::Seal, p.sealPartNumber, {}, reference);
        }
    }
    for (const Wire &w : harness.wires) {
        for (const WireEnd *end : {&w.endA, &w.endB}) {
            const auto *free = std::get_if<FreeEnd>(end);
            if (!free)
                continue;
            const bool needsTerminal = free->kind == FreeEndKind::Eyelet || free->kind == FreeEndKind::RingTerminal;
            if (needsTerminal || !free->terminalPartNumber.trimmed().isEmpty())
                parts.add(PartCategory::Terminal, free->terminalPartNumber, {}, describeWireEnd(harness, *end));
        }
    }
    for (const Splice &s : harness.splices)
        parts.add(PartCategory::Splice, s.partNumber, {}, s.label);
    bom.parts = parts.take();

    bom.extras = harness.bomExtras;
    return bom;
}

} // namespace tbh::bom
