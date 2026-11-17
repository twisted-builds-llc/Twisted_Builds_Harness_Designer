// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Queries.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

namespace tbh {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::Queries)
};
} // namespace

namespace {

template<typename Pred>
std::vector<QString> wiresWhere(const Harness &harness, Pred touches)
{
    std::vector<QString> ids;
    for (const Wire &w : harness.wires) {
        if (touches(w.endA) || touches(w.endB))
            ids.push_back(w.id);
    }
    return ids;
}

} // namespace

std::vector<QString> wiresOnConnector(const Harness &harness, const QString &connectorId)
{
    return wiresWhere(harness, [&](const WireEnd &end) {
        const auto *pin = std::get_if<PinRef>(&end);
        return pin && pin->connectorId == connectorId;
    });
}

std::vector<QString> wiresOnPins(const Harness &harness, const QString &connectorId,
                                 const QSet<QString> &pinIds)
{
    return wiresWhere(harness, [&](const WireEnd &end) {
        const auto *pin = std::get_if<PinRef>(&end);
        return pin && pin->connectorId == connectorId && pinIds.contains(pin->pinId);
    });
}

std::vector<QString> wiresOnSplice(const Harness &harness, const QString &spliceId)
{
    return wiresWhere(harness, [&](const WireEnd &end) {
        const auto *splice = std::get_if<SpliceRef>(&end);
        return splice && splice->spliceId == spliceId;
    });
}

std::vector<QString> wiresOnFreeEnd(const Harness &harness, const QString &freeEndId)
{
    return wiresWhere(harness, [&](const WireEnd &end) {
        const auto *free = std::get_if<FreeEndRef>(&end);
        return free && free->freeEndId == freeEndId;
    });
}

const FreeEnd *findFreeEnd(const Harness &harness, const QString &id)
{
    const auto it = std::find_if(harness.freeEnds.begin(), harness.freeEnds.end(),
                                 [&](const FreeEnd &f) { return f.id == id; });
    return it == harness.freeEnds.end() ? nullptr : &*it;
}

QString freeEndKindName(FreeEndKind kind)
{
    switch (kind) {
    case FreeEndKind::FlyingLead:
        return Tr::tr("Flying lead");
    case FreeEndKind::Eyelet:
        return Tr::tr("Eyelet");
    case FreeEndKind::RingTerminal:
        return Tr::tr("Ring terminal");
    case FreeEndKind::Bare:
        return Tr::tr("Bare end");
    }
    return {};
}

QString describeFreeEnd(const FreeEnd &end)
{
    const QString kind = freeEndKindName(end.kind);
    return end.label.isEmpty() ? kind : QStringLiteral("%1 %2").arg(kind, end.label);
}

LayoutNodeType layoutNodeTypeFor(FreeEndKind kind)
{
    return kind == FreeEndKind::Eyelet || kind == FreeEndKind::RingTerminal ? LayoutNodeType::Eyelet
                                                                            : LayoutNodeType::FlyingLead;
}

namespace {

Pin *findPin(Harness &harness, const WireEnd &end)
{
    const auto *ref = std::get_if<PinRef>(&end);
    if (!ref)
        return nullptr;
    for (Connector &c : harness.connectors) {
        if (c.id != ref->connectorId)
            continue;
        for (Pin &p : c.pins) {
            if (p.id == ref->pinId)
                return &p;
        }
    }
    return nullptr;
}

} // namespace

CircuitTextFill fillEmptyCircuitText(Harness &harness, const std::vector<QString> &wireIds)
{
    CircuitTextFill changed;
    QSet<QString> done; // wires already handled as part of an earlier net
    for (const QString &id : wireIds) {
        if (done.contains(id))
            continue;
        // Collect the net: the seed wire plus every wire reachable through splices.
        // Connector pins are leaves; text only flows through a splice.
        std::vector<Wire *> net;
        std::vector<QString> pendingSplices;
        QSet<QString> seenSplices;
        auto addWire = [&](Wire &w) {
            if (done.contains(w.id))
                return;
            done.insert(w.id);
            net.push_back(&w);
            for (const WireEnd *end : {&w.endA, &w.endB}) {
                if (const auto *s = std::get_if<SpliceRef>(end)) {
                    if (!seenSplices.contains(s->spliceId)) {
                        seenSplices.insert(s->spliceId);
                        pendingSplices.push_back(s->spliceId);
                    }
                }
            }
        };
        const auto seed = std::find_if(harness.wires.begin(), harness.wires.end(),
                                       [&](const Wire &w) { return w.id == id; });
        if (seed == harness.wires.end())
            continue;
        addWire(*seed);
        while (!pendingSplices.empty()) {
            const QString spliceId = pendingSplices.back();
            pendingSplices.pop_back();
            for (Wire &w : harness.wires) {
                const auto *a = std::get_if<SpliceRef>(&w.endA);
                const auto *b = std::get_if<SpliceRef>(&w.endB);
                if ((a && a->spliceId == spliceId) || (b && b->spliceId == spliceId))
                    addWire(w);
            }
        }

        // The net's text: the first non-empty pin text, seed wire's own pins first.
        QString text;
        for (const Wire *w : net) {
            for (const WireEnd *end : {&w->endA, &w->endB}) {
                const Pin *p = findPin(harness, *end);
                if (p && !p->circuitText.isEmpty() && text.isEmpty())
                    text = p->circuitText;
            }
        }
        if (text.isEmpty())
            continue;

        for (Wire *w : net) {
            for (const WireEnd *end : {&w->endA, &w->endB}) {
                Pin *p = findPin(harness, *end);
                if (p && p->circuitText.isEmpty()) {
                    p->circuitText = text;
                    changed.connectorIds.insert(std::get<PinRef>(*end).connectorId);
                }
            }
            if (w->name.isEmpty()) {
                // Prefer the wire's own pin text (may differ from the net's text when a
                // pin was renamed); fall back to the net's text.
                QString name = text;
                for (const WireEnd *end : {&w->endA, &w->endB}) {
                    if (const Pin *p = findPin(harness, *end)) {
                        name = p->circuitText;
                        break;
                    }
                }
                w->name = name;
                changed.wireIds.insert(w->id);
            }
        }
    }
    return changed;
}

QString describeWireEnd(const Harness &harness, const WireEnd &end)
{
    if (const auto *pinRef = std::get_if<PinRef>(&end)) {
        for (const Connector &c : harness.connectors) {
            if (c.id != pinRef->connectorId)
                continue;
            for (const Pin &p : c.pins) {
                if (p.id == pinRef->pinId)
                    return QStringLiteral("%1 : %2").arg(c.label, p.label);
            }
            return QStringLiteral("%1 : ?").arg(c.label);
        }
        return Tr::tr("(missing connector)");
    }
    if (const auto *spliceRef = std::get_if<SpliceRef>(&end)) {
        for (const Splice &s : harness.splices) {
            if (s.id == spliceRef->spliceId)
                return s.label;
        }
        return Tr::tr("(missing splice)");
    }
    const auto &freeRef = std::get<FreeEndRef>(end);
    if (const FreeEnd *free = findFreeEnd(harness, freeRef.freeEndId))
        return describeFreeEnd(*free);
    return Tr::tr("(missing free end)");
}

QString describeWire(const Harness &harness, const Wire &wire)
{
    const QString ends = QStringLiteral("%1 - %2").arg(describeWireEnd(harness, wire.endA),
                                                       describeWireEnd(harness, wire.endB));
    return wire.name.isEmpty() ? ends : QStringLiteral("%1 (%2)").arg(wire.name, ends);
}

std::optional<Gauge> parseGauge(const QString &text)
{
    QString t = text.trimmed().toLower();
    GaugeSystem system = GaugeSystem::Awg;
    for (const QString &suffix : {QStringLiteral("mm²"), QStringLiteral("mm2"), QStringLiteral("mm")}) {
        if (t.endsWith(suffix)) {
            t.chop(suffix.size());
            system = GaugeSystem::Mm2;
            break;
        }
    }
    if (system == GaugeSystem::Awg && t.endsWith(QLatin1StringView("awg")))
        t.chop(3);
    t = t.trimmed();
    bool ok = false;
    double value = QLocale().toDouble(t, &ok);
    if (!ok)
        value = QLocale::c().toDouble(t, &ok);
    if (!ok || (system == GaugeSystem::Mm2 && value <= 0))
        return std::nullopt;
    return Gauge{value, system};
}

QString circuitIdForNumber(int number)
{
    return QStringLiteral("C%1").arg(number, 3, 10, QLatin1Char('0'));
}

QString nextCircuitId(const Harness &harness)
{
    return circuitIdForNumber(nextCircuitNumber(harness));
}

int nextCircuitNumber(const Harness &harness)
{
    int highest = 0;
    for (const Wire &w : harness.wires) {
        if (w.circuitId.size() < 2 || w.circuitId[0] != QLatin1Char('C'))
            continue;
        bool ok = false;
        const int number = QStringView(w.circuitId).mid(1).toInt(&ok);
        if (ok && number > 0 && std::all_of(w.circuitId.begin() + 1, w.circuitId.end(),
                                            [](QChar c) { return c.isDigit(); }))
            highest = std::max(highest, number);
    }
    return highest + 1;
}

std::vector<WireEndChoice> wireEndChoices(const Harness &harness)
{
    std::vector<WireEndChoice> choices;
    for (const Connector &c : harness.connectors) {
        for (const Pin &p : c.pins)
            choices.push_back({describeWireEnd(harness, PinRef{c.id, p.id}), PinRef{c.id, p.id}});
    }
    for (const Splice &s : harness.splices)
        choices.push_back({describeWireEnd(harness, SpliceRef{s.id}), SpliceRef{s.id}});
    for (const FreeEnd &f : harness.freeEnds)
        choices.push_back({describeWireEnd(harness, FreeEndRef{f.id}), FreeEndRef{f.id}});
    return choices;
}

std::optional<WireEnd> findWireEnd(const Harness &harness, const QString &text)
{
    auto key = [](QString s) {
        s.remove(QLatin1Char(' '));
        return s.toCaseFolded();
    };
    const QString wanted = key(text);
    if (wanted.isEmpty())
        return std::nullopt;
    for (const WireEndChoice &choice : wireEndChoices(harness)) {
        if (key(choice.text) == wanted)
            return choice.end;
    }
    return std::nullopt;
}

namespace {

struct PinWire
{
    QString wireId;
    bool farIsB = false;
    WireEnd farEnd;
};

std::vector<PinWire> wiresOnPin(const Harness &harness, const PinRef &pin)
{
    std::vector<PinWire> result;
    for (const Wire &w : harness.wires) {
        if (w.endA == WireEnd(pin))
            result.push_back({w.id, true, w.endB});
        else if (w.endB == WireEnd(pin))
            result.push_back({w.id, false, w.endA});
    }
    return result;
}

} // namespace

QString pinConnectionsText(const Harness &harness, const PinRef &pin)
{
    QStringList names;
    for (const PinWire &w : wiresOnPin(harness, pin))
        names << describeWireEnd(harness, w.farEnd);
    return names.join(QStringLiteral(", "));
}

PinConnectionPlan planPinConnections(const Harness &harness, const PinRef &pin, const QString &text)
{
    PinConnectionPlan plan;
    std::vector<WireEnd> wanted;
    for (const QString &name : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (name.trimmed().isEmpty())
            continue;
        const std::optional<WireEnd> end = findWireEnd(harness, name);
        if (!end || *end == WireEnd(pin))
            plan.unknown << name.trimmed();
        else if (std::find(wanted.begin(), wanted.end(), *end) == wanted.end())
            wanted.push_back(*end);
    }
    if (!plan.unknown.isEmpty())
        return plan;
    std::vector<PinWire> leftOver;
    for (const PinWire &w : wiresOnPin(harness, pin)) {
        const auto kept = std::find(wanted.begin(), wanted.end(), w.farEnd);
        if (kept != wanted.end())
            wanted.erase(kept);
        else
            leftOver.push_back(w);
    }
    size_t i = 0;
    for (; i < leftOver.size() && i < wanted.size(); ++i)
        plan.moves.push_back({leftOver[i].wireId, leftOver[i].farIsB, wanted[i]});
    for (size_t j = i; j < wanted.size(); ++j)
        plan.adds.push_back(wanted[j]);
    for (size_t j = i; j < leftOver.size(); ++j)
        plan.removals.push_back(leftOver[j].wireId);
    return plan;
}

std::vector<LayoutRef> layoutRefs(const Harness &harness)
{
    std::vector<LayoutRef> refs;
    for (const Connector &c : harness.connectors)
        refs.push_back({c.id, LayoutNodeType::Connector, c.label});
    for (const Splice &s : harness.splices)
        refs.push_back({s.id, LayoutNodeType::Splice, s.label});
    for (const FreeEnd &f : harness.freeEnds) {
        // Unlabeled ends are told apart by the first wire on them.
        QString label = describeFreeEnd(f);
        if (f.label.isEmpty()) {
            for (const Wire &w : harness.wires) {
                const auto *a = std::get_if<FreeEndRef>(&w.endA);
                const auto *b = std::get_if<FreeEndRef>(&w.endB);
                if ((a && a->freeEndId == f.id) || (b && b->freeEndId == f.id)) {
                    const QString wireName = w.name.isEmpty() ? describeWireEnd(harness, a ? w.endB : w.endA) : w.name;
                    label = QStringLiteral("%1 (%2)").arg(label, wireName);
                    break;
                }
            }
        }
        refs.push_back({f.id, layoutNodeTypeFor(f.kind), label});
    }
    return refs;
}

QString layoutRefLabel(const Harness &harness, const QString &refId)
{
    if (refId.isEmpty())
        return {};
    for (const LayoutRef &ref : layoutRefs(harness)) {
        if (ref.refId == refId)
            return ref.label;
    }
    return {};
}

QString sheetDisplayName(const Schematic &schematic, int index)
{
    const QString &name = schematic.sheets[size_t(index)].name;
    return name.isEmpty() ? Tr::tr("Sheet %1").arg(index + 1) : name;
}

WireSheets wireSheets(const Harness &harness, const Wire &wire)
{
    const Schematic &schematic = harness.schematic;
    const QString a = wireEndItemId(wire.endA);
    const QString b = wireEndItemId(wire.endB);
    std::vector<QString> own = schematic.sheetsOf(Schematic::wireSheetKey(wire.id));
    if (!schematic.placements.contains(Schematic::wireSheetKey(wire.id)))
        own.clear(); // sheetsOf() puts an item without a placement on the first sheet
    WireSheets result;
    for (const QString &sheet : own) {
        if (schematic.isOnSheet(a, sheet) && schematic.isOnSheet(b, sheet))
            result.sheets.push_back(sheet);
    }
    if (!result.sheets.empty())
        return result;
    for (const SchematicSheet &sheet : schematic.sheets) {
        if (schematic.isOnSheet(a, sheet.id) && schematic.isOnSheet(b, sheet.id)) {
            result.sheets.push_back(sheet.id);
            return result;
        }
    }
    auto tagSheet = [&](const QString &end) {
        for (const QString &sheet : own) {
            if (schematic.isOnSheet(end, sheet))
                return sheet;
        }
        return schematic.sheetOf(end);
    };
    result.tagSheetA = tagSheet(a);
    result.tagSheetB = tagSheet(b);
    return result;
}

QString wireEndItemId(const WireEnd &end)
{
    if (const auto *pin = std::get_if<PinRef>(&end))
        return pin->connectorId;
    if (const auto *splice = std::get_if<SpliceRef>(&end))
        return splice->spliceId;
    return std::get<FreeEndRef>(end).freeEndId;
}

} // namespace tbh
