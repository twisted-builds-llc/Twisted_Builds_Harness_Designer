// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Queries.h"

#include <QCoreApplication>

namespace tbh {

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

QString tr(const char *text)
{
    return QCoreApplication::translate("tbh::Queries", text);
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
        return tr("(missing connector)");
    }
    if (const auto *spliceRef = std::get_if<SpliceRef>(&end)) {
        for (const Splice &s : harness.splices) {
            if (s.id == spliceRef->spliceId)
                return s.label;
        }
        return tr("(missing splice)");
    }
    const auto &free = std::get<FreeEnd>(end);
    QString kind;
    switch (free.kind) {
    case FreeEndKind::FlyingLead:
        kind = tr("Flying lead");
        break;
    case FreeEndKind::Eyelet:
        kind = tr("Eyelet");
        break;
    case FreeEndKind::RingTerminal:
        kind = tr("Ring terminal");
        break;
    case FreeEndKind::Bare:
        kind = tr("Bare end");
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

} // namespace tbh
