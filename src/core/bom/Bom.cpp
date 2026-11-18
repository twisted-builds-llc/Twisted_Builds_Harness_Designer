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
    for (const CableLine &c : cables)
        count += c.partNumber.isEmpty() || c.piecesWithoutLength > 0 ? 1 : 0;
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

    // Wires that are not bought as wire: inside a bought cable, or a cable's shield.
    QSet<QString> notWire;
    QHash<QString, QStringList> cableLabels;
    QSet<QString> shields;
    for (const Cable &c : harness.cables) {
        for (const QString &id : c.wireIds) {
            cableLabels[id].append(c.label);
            if (c.source == CableSource::Bought)
                notWire.insert(id);
        }
        if (c.shielded()) {
            cableLabels[c.shieldWireId].append(c.label);
            shields.insert(c.shieldWireId);
            notWire.insert(c.shieldWireId);
        }
    }

    // Wires and cut list.
    using WireKey = std::tuple<QString, QString, int, double, QString>;
    std::map<WireKey, size_t> wireIndex;
    for (const Wire &w : harness.wires) {
        if (w.factory)
            continue;
        const calc::WireLength length = calc::wireLength(harness, graph, w);
        const WireType *type = library.wireType(w.wireTypeId);
        const QString typeName = type ? type->name : QString();

        CutLine cut;
        cut.wireId = w.id;
        cut.circuitId = w.circuitId;
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
        cut.cables = cableLabels.value(w.id);
        cut.shield = shields.contains(w.id);
        bom.cutList.push_back(cut);
        if (notWire.contains(w.id))
            continue;

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

    // Bought cables and built shield braid.
    using CableKey = std::pair<int, QString>;
    std::map<CableKey, size_t> cableIndex;
    QHash<QString, const Wire *> wireById;
    for (const Wire &w : harness.wires)
        wireById.insert(w.id, &w);
    for (const Cable &c : harness.cables) {
        const bool allFactory = std::all_of(c.wireIds.begin(), c.wireIds.end(), [&](const QString &id) {
            const Wire *w = wireById.value(id);
            return w && w->factory;
        });
        if (allFactory)
            continue;
        CableLine::Kind kind;
        QString partNumber;
        if (c.source == CableSource::Bought) {
            kind = CableLine::Kind::Cable;
            partNumber = c.partNumber.trimmed();
        } else if (c.shielded()) {
            kind = CableLine::Kind::ShieldBraid;
            partNumber = c.shieldPartNumber.trimmed();
        } else {
            continue; // loose wires, counted as wire
        }
        const CableKey key{int(kind), partNumber};
        auto it = cableIndex.find(key);
        if (it == cableIndex.end()) {
            it = cableIndex.emplace(key, bom.cables.size()).first;
            CableLine line;
            line.kind = kind;
            line.partNumber = partNumber;
            bom.cables.push_back(line);
        }
        CableLine &line = bom.cables[it->second];
        ++line.pieces;
        if (!c.label.isEmpty())
            line.references.append(c.label);
        if (const auto run = calc::cableRunLengthMm(graph, harness, c))
            line.lengthMm += calc::roundUpToIncrement(*run, settings.lengthRoundingIncrementMm);
        else
            ++line.piecesWithoutLength;
    }
    std::stable_sort(bom.cables.begin(), bom.cables.end(), [](const CableLine &a, const CableLine &b) {
        if (a.kind != b.kind)
            return a.kind < b.kind;
        if (a.partNumber.isEmpty() != b.partNumber.isEmpty())
            return b.partNumber.isEmpty();
        return a.partNumber < b.partNumber;
    });
    for (CableLine &line : bom.cables)
        line.lengthWithWasteMm = line.lengthMm * (1.0 + settings.wasteFactorWirePercent / 100.0);

    // Sleeving: covered segments and label covers.
    std::map<QString, size_t> sleevingIndex;
    auto addSleeving = [&](const QString &sleevingId, double lengthMm) {
        if (sleevingId.isEmpty())
            return;
        auto it = sleevingIndex.find(sleevingId);
        if (it == sleevingIndex.end()) {
            it = sleevingIndex.emplace(sleevingId, bom.sleeving.size()).first;
            SleevingLine line;
            line.sleevingId = sleevingId;
            if (const Sleeving *entry = library.sleeving(sleevingId)) {
                line.family = entry->family;
                line.sizeLabel = entry->sizeLabel;
            } else {
                line.known = false;
                line.family = sleevingId;
            }
            bom.sleeving.push_back(line);
        }
        SleevingLine &line = bom.sleeving[it->second];
        ++line.pieces;
        line.lengthMm += lengthMm;
    };
    // Sleeving on the factory harness (segments with only factory wires) is already there,
    // unless the user marked it as added by this build. Labels and ties are always placed by
    // the user, so they always count.
    const calc::FactoryParts factory = calc::factoryOnlyParts(harness, calc::wiresBySegment(harness, graph));
    for (const LayoutSegment &s : harness.layoutSegments) {
        if (!factory.segmentIds.contains(s.id) || s.addOnSleeving)
            addSleeving(s.sleevingId, s.lengthMm);
    }
    for (const LayoutAccessory &a : harness.accessories) {
        if (a.kind == AccessoryKind::Label)
            addSleeving(a.coverSleevingId, a.coverLengthMm);
    }
    for (SleevingLine &line : bom.sleeving)
        line.lengthWithWasteMm = line.lengthMm * (1.0 + settings.wasteFactorSleevingPercent / 100.0);

    // Parts. Only wires to be built (not factory wires) need terminals, seals and splices.
    QSet<QString> wiredPins;
    QSet<QString> endsWithFactoryWires; // splices and free ends
    QSet<QString> endsWithBuiltWires;
    for (const Wire &w : harness.wires) {
        for (const WireEnd *end : {&w.endA, &w.endB}) {
            if (const auto *pin = std::get_if<PinRef>(end)) {
                if (!w.factory)
                    wiredPins.insert(pin->connectorId + QLatin1Char('/') + pin->pinId);
            } else {
                (w.factory ? endsWithFactoryWires : endsWithBuiltWires).insert(wireEndItemId(*end));
            }
        }
    }
    // Splices and free ends whose wires are all factory wires already exist.
    auto alreadyExists = [&](const QString &id) {
        return endsWithFactoryWires.contains(id) && !endsWithBuiltWires.contains(id);
    };
    PartCounter parts;
    for (const Connector &c : harness.connectors) {
        if (c.factory) {
            // The housing is already there; add-on wires in its cavities still need terminals.
            for (const Pin &p : c.pins) {
                if (p.isCavityPlug || !wiredPins.contains(c.id + QLatin1Char('/') + p.id))
                    continue;
                const QString reference = QStringLiteral("%1 : %2").arg(c.label, p.label);
                parts.add(PartCategory::Terminal, p.terminalPartNumber, {}, reference);
                if (!p.sealPartNumber.trimmed().isEmpty())
                    parts.add(PartCategory::Seal, p.sealPartNumber, {}, reference);
            }
            continue;
        }
        parts.add(PartCategory::Connector, c.partNumber, c.description.isEmpty() ? c.manufacturer : c.description,
                  c.label);
        if (!c.backshellPartNumber.trimmed().isEmpty())
            parts.add(PartCategory::Backshell, c.backshellPartNumber, {}, c.label);
        if (!c.strainReliefPartNumber.trimmed().isEmpty())
            parts.add(PartCategory::Tie, c.strainReliefPartNumber, {}, c.label);
        for (const Pin &p : c.pins) {
            const QString reference = QStringLiteral("%1 : %2").arg(c.label, p.label);
            if (p.isCavityPlug) {
                parts.add(PartCategory::CavityPlug, p.plugPartNumber, {}, reference);
                if (!p.sealPartNumber.trimmed().isEmpty()) // plugs that need a seal too
                    parts.add(PartCategory::Seal, p.sealPartNumber, {}, reference);
                continue;
            }
            if (!wiredPins.contains(c.id + QLatin1Char('/') + p.id))
                continue;
            parts.add(PartCategory::Terminal, p.terminalPartNumber, {}, reference);
            if (!p.sealPartNumber.trimmed().isEmpty())
                parts.add(PartCategory::Seal, p.sealPartNumber, {}, reference);
        }
    }
    for (const FreeEnd &f : harness.freeEnds) {
        if (f.kind == FreeEndKind::Cut || alreadyExists(f.id))
            continue;
        const bool needsTerminal = f.kind == FreeEndKind::Eyelet || f.kind == FreeEndKind::RingTerminal;
        if (needsTerminal || !f.terminalPartNumber.trimmed().isEmpty())
            parts.add(PartCategory::Terminal, f.terminalPartNumber, {}, describeFreeEnd(f));
    }
    for (const Splice &s : harness.splices) {
        if (alreadyExists(s.id))
            continue;
        parts.add(PartCategory::Splice, s.partNumber, {}, s.label);
    }
    for (const LayoutAccessory &a : harness.accessories) {
        parts.add(a.kind == AccessoryKind::Label ? PartCategory::Label : PartCategory::Tie, a.partNumber, {},
                  a.text);
    }
    bom.parts = parts.take();

    bom.extras = harness.bomExtras;
    return bom;
}

} // namespace tbh::bom
