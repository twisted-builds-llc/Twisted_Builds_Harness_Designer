// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Queries.h"

#include <QCoreApplication>

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
    const auto &free = std::get<FreeEnd>(end);
    QString kind;
    switch (free.kind) {
    case FreeEndKind::FlyingLead:
        kind = Tr::tr("Flying lead");
        break;
    case FreeEndKind::Eyelet:
        kind = Tr::tr("Eyelet");
        break;
    case FreeEndKind::RingTerminal:
        kind = Tr::tr("Ring terminal");
        break;
    case FreeEndKind::Bare:
        kind = Tr::tr("Bare end");
        break;
    }
    return free.label.isEmpty() ? kind : QStringLiteral("%1 %2").arg(kind, free.label);
}

QString describeWire(const Harness &harness, const Wire &wire)
{
    const QString ends = QStringLiteral("%1 - %2").arg(describeWireEnd(harness, wire.endA),
                                                       describeWireEnd(harness, wire.endB));
    return wire.name.isEmpty() ? ends : QStringLiteral("%1 (%2)").arg(wire.name, ends);
}

std::vector<LayoutRef> layoutRefs(const Harness &harness)
{
    std::vector<LayoutRef> refs;
    for (const Connector &c : harness.connectors)
        refs.push_back({c.id, LayoutNodeType::Connector, c.label});
    for (const Splice &s : harness.splices)
        refs.push_back({s.id, LayoutNodeType::Splice, s.label});
    for (const Wire &w : harness.wires) {
        for (bool endB : {false, true}) {
            const WireEnd &end = endB ? w.endB : w.endA;
            const auto *free = std::get_if<FreeEnd>(&end);
            if (!free)
                continue;
            const bool eyelet = free->kind == FreeEndKind::Eyelet || free->kind == FreeEndKind::RingTerminal;
            const QString wireName = w.name.isEmpty() ? describeWireEnd(harness, endB ? w.endA : w.endB) : w.name;
            refs.push_back({freeEndPlacementKey(w.id, endB),
                            eyelet ? LayoutNodeType::Eyelet : LayoutNodeType::FlyingLead,
                            QStringLiteral("%1 (%2)").arg(describeWireEnd(harness, end), wireName)});
        }
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

} // namespace tbh
