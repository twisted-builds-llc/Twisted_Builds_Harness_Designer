// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Queries.h"

#include <QCoreApplication>

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
