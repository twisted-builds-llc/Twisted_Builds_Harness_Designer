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
    return LayoutSegment{id, a, b, mm, {}, {}, SleevingCoverage::Full, std::nullopt, {}};
}

/* Test layout (lengths in mm):

     ecu(n1) --seg1 300-- branch(n2) --seg2 450-- sensor(n3)
                              |
                            seg3 120
                              |
                          splice(n4) --seg4 80-- flying lead "fl", end B of w3 (n5)

   Settings: allowance pin 10, splice 5, flyingLead 0, ringTerminal 15;
   service loop 5 %; rounding increment 5 mm.                                   */
inline Harness sample()
{
    Harness h;
    h.connectors = {connector(u"ecu"_s, 4), connector(u"sensor"_s, 2), connector(u"lamp"_s, 1)};
    h.splices = {Splice{u"sp"_s, u"S1"_s, {}, {}}};
    h.freeEnds = {FreeEnd{u"fl"_s, FreeEndKind::FlyingLead, {}, {}, {}}};
    h.wires = {
        wire(u"w1"_s, PinRef{u"ecu"_s, u"ecu1"_s}, PinRef{u"sensor"_s, u"sensor1"_s}),
        wire(u"w2"_s, PinRef{u"ecu"_s, u"ecu2"_s}, SpliceRef{u"sp"_s}),
        wire(u"w3"_s, SpliceRef{u"sp"_s}, FreeEndRef{u"fl"_s}),
        wire(u"w4"_s, PinRef{u"ecu"_s, u"ecu3"_s}, PinRef{u"sensor"_s, u"sensor2"_s}),
        wire(u"w5"_s, PinRef{u"ecu"_s, u"ecu4"_s}, PinRef{u"lamp"_s, u"lamp1"_s}),
    };
    h.wires[3].lengthOverrideMm = 1000.0;
    h.layoutNodes = {node(u"n1"_s, LayoutNodeType::Connector, u"ecu"_s),
                     node(u"n2"_s, LayoutNodeType::Branch, {}),
                     node(u"n3"_s, LayoutNodeType::Connector, u"sensor"_s),
                     node(u"n4"_s, LayoutNodeType::Splice, u"sp"_s),
                     node(u"n5"_s, LayoutNodeType::FlyingLead, u"fl"_s)};
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



/* sample() cut lengths (hand-calculated in tst_wirelength): w1 810, w2 460,
   w4 1000 (override), w5 not routed. w3 becomes a ring terminal here (allowance 15):
   base 80 + ends 5 + 15 = 100; * 1.05 = 105 mm. Waste factors 5 %.
   Groups: RED/WHT 18 AWG = w1 + w2; BLK 20 AWG = w3 + w4 + w5.
   Sleeving: seg1 300 + seg2 450 PET 1/4; seg3 120 shrink; seg4 80 unknown id.          */
inline Harness bomSample()
{
    Harness h = sample();
    for (Wire &w : h.wires) {
        const bool red = w.id == u"w1"_s || w.id == u"w2"_s;
        w.baseColor = red ? (w.id == u"w2"_s ? u"red"_s : u"RED"_s) : u"BLK"_s; // grouping ignores case
        w.stripeColor = red ? u"WHT"_s : QString();
        w.gauge = Gauge{red ? 18.0 : 20.0, GaugeSystem::Awg};
    }
    h.freeEnds[0].kind = FreeEndKind::RingTerminal;
    h.freeEnds[0].terminalPartNumber = u"RING-6"_s;
    h.wires[0].name = u"5V, \"REF\""_s; // needs CSV quoting

    Connector &ecu = h.connectors[0];
    ecu.partNumber = u"ECU-PN"_s;
    ecu.description = u"ECU header"_s;
    for (int i = 0; i < 3; ++i)
        ecu.pins[size_t(i)].terminalPartNumber = u"T-1"_s; // ecu4 has none
    ecu.pins[0].sealPartNumber = u"S-1"_s;
    ecu.pins[1].sealPartNumber = u"S-1"_s;
    ecu.pins.push_back(Pin{u"ecu5"_s, u"5"_s, {}, {}, u"PLUG-1"_s, true, {}, {}});
    for (Pin &p : h.connectors[1].pins)
        p.terminalPartNumber = u"T-2"_s;
    h.splices[0].partNumber = u"SPL-1"_s;

    h.layoutSegments[0].sleevingId = u"pet-6"_s;
    h.layoutSegments[1].sleevingId = u"pet-6"_s;
    h.layoutSegments[2].sleevingId = u"hs"_s;
    h.layoutSegments[3].sleevingId = u"missing"_s;
    h.bomExtras = {BomExtra{u"x"_s, u"TAPE"_s, u"Cloth tape, 19 mm"_s, 2, u"roll"_s, {}, {}}};
    return h;
}

inline Library bomLibrary()
{
    Library lib;
    lib.sleevings = {Sleeving{u"pet-6"_s, u"PET"_s, SleevingKind::Braided, u"1/4 in"_s, 6.4, std::nullopt, false, {}},
                     Sleeving{u"hs"_s, u"Shrink"_s, SleevingKind::HeatShrink, u"3/8 in"_s, 9.5, 4.8, false, {}}};
    return lib;
}

} // namespace tbh::testing
