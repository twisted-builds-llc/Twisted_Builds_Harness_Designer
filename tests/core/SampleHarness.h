// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared hand-calculated test harness for the calc tests.

#include "model/Harness.h"

#include <QString>

namespace tbh::testing {

using namespace Qt::StringLiterals;


inline Connector connector(const QString &id, int pins)
{
    Connector c;
    c.id = id;
    c.label = id.toUpper();
    for (int i = 1; i <= pins; ++i)
        c.pins.push_back(Pin{id + QString::number(i), QString::number(i), {}, {}, {}, false, {}, {}});
    return c;
}

inline Wire wire(const QString &id, WireEnd a, WireEnd b)
{
    Wire w;
    w.id = id;
    w.endA = std::move(a);
    w.endB = std::move(b);
    return w;
}

inline LayoutNode node(const QString &id, LayoutNodeType type, const QString &ref)
{
    return LayoutNode{id, type, ref, QPointF(), {}};
}

inline LayoutSegment segment(const QString &id, const QString &a, const QString &b, double mm)
{
    return LayoutSegment{id, a, b, mm, {}, {}, SleevingCoverage::Full, {}};
}

/* Test layout (lengths in mm):

     ecu(n1) --seg1 300-- branch(n2) --seg2 450-- sensor(n3)
                              |
                            seg3 120
                              |
                          splice(n4) --seg4 80-- flying lead of w3 (n5)

   Settings: allowance pin 10, splice 5, flyingLead 0, ringTerminal 15;
   service loop 5 %; rounding increment 5 mm.                                   */
inline Harness sample()
{
    Harness h;
    h.connectors = {connector(u"ecu"_s, 4), connector(u"sensor"_s, 2), connector(u"lamp"_s, 1)};
    h.splices = {Splice{u"sp"_s, u"S1"_s, {}, {}}};
    h.wires = {
        wire(u"w1"_s, PinRef{u"ecu"_s, u"ecu1"_s}, PinRef{u"sensor"_s, u"sensor1"_s}),
        wire(u"w2"_s, PinRef{u"ecu"_s, u"ecu2"_s}, SpliceRef{u"sp"_s}),
        wire(u"w3"_s, SpliceRef{u"sp"_s}, FreeEnd{FreeEndKind::FlyingLead, {}, {}}),
        wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, PinRef{u"sensor"_s, u"sensor2"_s}),
        wire(u"w5"_s, PinRef{u"ecu"_s, u"ecu4"_s}, PinRef{u"lamp"_s, u"lamp1"_s}),
    };
    h.wires[3].lengthOverrideMm = 1000.0;
    h.layoutNodes = {node(u"n1"_s, LayoutNodeType::Connector, u"ecu"_s),
                     node(u"n2"_s, LayoutNodeType::Branch, {}),
                     node(u"n3"_s, LayoutNodeType::Connector, u"sensor"_s),
                     node(u"n4"_s, LayoutNodeType::Splice, u"sp"_s),
                     node(u"n5"_s, LayoutNodeType::FlyingLead, freeEndPlacementKey(u"w3"_s, true))};
    h.layoutSegments = {segment(u"seg1"_s, u"n1"_s, u"n2"_s, 300), segment(u"seg2"_s, u"n2"_s, u"n3"_s, 450),
                        segment(u"seg3"_s, u"n2"_s, u"n4"_s, 120), segment(u"seg4"_s, u"n4"_s, u"n5"_s, 80)};
    h.settings.stripAllowanceMm = {{EndType::Pin, 10}, {EndType::Splice, 5}, {EndType::FlyingLead, 0},
                                   {EndType::RingTerminal, 15}};
    h.settings.serviceLoopPercent = 5;
    h.settings.lengthRoundingIncrementMm = 5;
    return h;
}

inline const Wire &wireById(const Harness &h, const QString &id)
{
    for (const Wire &w : h.wires) {
        if (w.id == id)
            return w;
    }
    Q_UNREACHABLE();
}


} // namespace tbh::testing
