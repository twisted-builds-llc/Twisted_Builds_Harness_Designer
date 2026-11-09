// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Library.h"

#include <QSet>

namespace tbh {

std::optional<double> WireType::outerDiameterMm(const Gauge &gauge) const
{
    for (const WireOuterDiameter &od : outerDiameters) {
        if (od.gauge == gauge)
            return od.odMm;
    }
    return std::nullopt;
}

const WireType *Library::wireType(const QString &id) const
{
    for (const WireType &t : wireTypes) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

const Sleeving *Library::sleeving(const QString &id) const
{
    for (const Sleeving &s : sleevings) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

QStringList Library::sleevingFamilies() const
{
    QStringList families;
    for (const Sleeving &s : sleevings) {
        if (!families.contains(s.family))
            families.append(s.family);
    }
    return families;
}

Library mergeLibraries(const Library &user, const Library &embedded)
{
    Library merged = user;
    for (const WireType &t : embedded.wireTypes) {
        if (!user.wireType(t.id))
            merged.wireTypes.push_back(t);
    }
    for (const Sleeving &s : embedded.sleevings) {
        if (!user.sleeving(s.id))
            merged.sleevings.push_back(s);
    }
    return merged;
}

Library usedLibraryEntries(const Harness &harness, const Library &library)
{
    QSet<QString> wireTypes;
    for (const Wire &w : harness.wires)
        wireTypes.insert(w.wireTypeId);
    QSet<QString> sleevings;
    for (const LayoutSegment &s : harness.layoutSegments)
        sleevings.insert(s.sleevingId);
    Library used;
    for (const WireType &t : library.wireTypes) {
        if (wireTypes.contains(t.id))
            used.wireTypes.push_back(t);
    }
    for (const Sleeving &s : library.sleevings) {
        if (sleevings.contains(s.id))
            used.sleevings.push_back(s);
    }
    return used;
}

} // namespace tbh
