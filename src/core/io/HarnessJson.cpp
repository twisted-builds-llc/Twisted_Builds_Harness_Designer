// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/HarnessJson.h"

#include "io/FileFormat.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <utility>

namespace tbh::io {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::io::HarnessJson)
};
} // namespace

using namespace Qt::StringLiterals;

namespace {

// Thrown inside this file only; converted to an error string at the API boundary.
struct FormatError
{
    QString message;
};

[[noreturn]] void fail(const QString &message)
{
    throw FormatError{message};
}

// ---- enum <-> string ------------------------------------------------------

template<typename E>
struct EnumName
{
    E value;
    const char *name;
};

constexpr EnumName<Gender> kGenders[] = {
    {Gender::None, "none"}, {Gender::Male, "male"}, {Gender::Female, "female"}};
constexpr EnumName<GaugeSystem> kGaugeSystems[] = {
    {GaugeSystem::Awg, "awg"}, {GaugeSystem::Mm2, "mm2"}};
constexpr EnumName<FreeEndKind> kFreeEndKinds[] = {{FreeEndKind::FlyingLead, "flyingLead"},
                                                   {FreeEndKind::Eyelet, "eyelet"},
                                                   {FreeEndKind::RingTerminal, "ringTerminal"},
                                                   {FreeEndKind::Bare, "bare"}};
constexpr EnumName<EndType> kEndTypes[] = {{EndType::Pin, "pin"},
                                           {EndType::Splice, "splice"},
                                           {EndType::FlyingLead, "flyingLead"},
                                           {EndType::Eyelet, "eyelet"},
                                           {EndType::RingTerminal, "ringTerminal"},
                                           {EndType::Bare, "bare"}};
constexpr EnumName<ConductorKind> kConductorKinds[] = {{ConductorKind::Single, "single"}};
constexpr EnumName<LayoutNodeType> kLayoutNodeTypes[] = {{LayoutNodeType::Connector, "connector"},
                                                         {LayoutNodeType::Branch, "branch"},
                                                         {LayoutNodeType::Splice, "splice"},
                                                         {LayoutNodeType::FlyingLead, "flyingLead"},
                                                         {LayoutNodeType::Eyelet, "eyelet"}};
constexpr EnumName<SleevingCoverage> kSleevingCoverages[] = {{SleevingCoverage::Full, "full"}};
constexpr EnumName<SleevingKind> kSleevingKinds[] = {{SleevingKind::Braided, "braided"},
                                                     {SleevingKind::SplitLoom, "splitLoom"},
                                                     {SleevingKind::HeatShrink, "heatShrink"},
                                                     {SleevingKind::Other, "other"}};

template<typename E, size_t N>
QString enumToString(const EnumName<E> (&table)[N], E value)
{
    for (const auto &entry : table) {
        if (entry.value == value)
            return QString::fromLatin1(entry.name);
    }
    Q_UNREACHABLE_RETURN(QString());
}

template<typename E, size_t N>
std::optional<E> enumFromString(const EnumName<E> (&table)[N], const QString &text)
{
    for (const auto &entry : table) {
        if (text == QLatin1StringView(entry.name))
            return entry.value;
    }
    return std::nullopt;
}

// ---- reading --------------------------------------------------------------

// Reads typed fields from one JSON object, tracking which keys were consumed so the
// rest can be preserved in `extra`. Absent keys yield defaults; wrong types fail.
class ObjectReader
{
public:
    ObjectReader(QJsonObject object, QString context)
        : m_object(std::move(object))
        , m_context(std::move(context))
    {
    }

    const QString &context() const { return m_context; }

    QString string(const char *key, bool required = false)
    {
        const QJsonValue value = take(key, required);
        if (value.isUndefined())
            return {};
        if (!value.isString())
            failType(key, "string");
        return value.toString();
    }

    QString id(const char *key = "id")
    {
        const QString value = string(key, true);
        if (value.isEmpty())
            fail(Tr::tr("%1: \"%2\" must not be empty.").arg(m_context, QLatin1StringView(key)));
        return value;
    }

    double number(const char *key, double defaultValue, bool required = false)
    {
        const QJsonValue value = take(key, required);
        if (value.isUndefined())
            return defaultValue;
        if (!value.isDouble())
            failType(key, "number");
        return value.toDouble();
    }

    std::optional<double> optionalNumber(const char *key)
    {
        const QJsonValue value = take(key, false);
        if (value.isUndefined() || value.isNull())
            return std::nullopt;
        if (!value.isDouble())
            failType(key, "number");
        return value.toDouble();
    }

    bool boolean(const char *key, bool defaultValue)
    {
        const QJsonValue value = take(key, false);
        if (value.isUndefined())
            return defaultValue;
        if (!value.isBool())
            failType(key, "boolean");
        return value.toBool();
    }

    QJsonArray array(const char *key)
    {
        const QJsonValue value = take(key, false);
        if (value.isUndefined())
            return {};
        if (!value.isArray())
            failType(key, "array");
        return value.toArray();
    }

    QJsonObject object(const char *key, bool required = false)
    {
        const QJsonValue value = take(key, required);
        if (value.isUndefined())
            return {};
        if (!value.isObject())
            failType(key, "object");
        return value.toObject();
    }

    template<typename E, size_t N>
    E enumValue(const char *key, const EnumName<E> (&table)[N], E defaultValue)
    {
        const QJsonValue value = take(key, false);
        if (value.isUndefined())
            return defaultValue;
        if (!value.isString())
            failType(key, "string");
        const auto parsed = enumFromString(table, value.toString());
        if (!parsed)
            fail(Tr::tr("%1: unknown value \"%2\" for \"%3\".")
                     .arg(m_context, value.toString(), QLatin1StringView(key)));
        return *parsed;
    }

    QJsonObject extra() const
    {
        QJsonObject rest = m_object;
        for (const QString &key : m_used)
            rest.remove(key);
        return rest;
    }

private:
    QJsonValue take(const char *key, bool required)
    {
        const QString name = QString::fromLatin1(key);
        m_used.insert(name);
        const QJsonValue value = m_object.value(name);
        if (required && value.isUndefined())
            fail(Tr::tr("%1: missing required field \"%2\".").arg(m_context, name));
        return value;
    }

    [[noreturn]] void failType(const char *key, const char *type) const
    {
        fail(Tr::tr("%1: field \"%2\" must be a %3.")
                 .arg(m_context, QLatin1StringView(key), QLatin1StringView(type)));
    }

    QJsonObject m_object;
    QString m_context;
    QSet<QString> m_used;
};

QJsonObject requireObject(const QJsonValue &value, const QString &context)
{
    if (!value.isObject())
        fail(Tr::tr("%1 must be an object.").arg(context));
    return value.toObject();
}

QString indexed(const QString &base, qsizetype index)
{
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

QJsonDocument parseEntry(const ZipEntries &entries, QLatin1StringView name, bool required)
{
    const auto it = entries.find(QString(name));
    if (it == entries.end()) {
        if (required)
            fail(Tr::tr("The container is missing \"%1\".").arg(name));
        return {};
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(it->second, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        fail(Tr::tr("\"%1\" is not valid JSON: %2 (offset %3).")
                 .arg(name, parseError.errorString())
                 .arg(parseError.offset));
    return doc;
}

QJsonArray entryArray(const ZipEntries &entries, QLatin1StringView name)
{
    const QJsonDocument doc = parseEntry(entries, name, false);
    if (doc.isNull())
        return {};
    if (!doc.isArray())
        fail(Tr::tr("\"%1\" must contain a JSON array.").arg(name));
    return doc.array();
}

Pin readPin(ObjectReader r)
{
    Pin pin;
    pin.id = r.id();
    pin.label = r.string("label");
    pin.circuitText = r.string("circuitText");
    pin.terminalPartNumber = r.string("terminalPartNumber");
    pin.sealPartNumber = r.string("sealPartNumber");
    pin.isCavityPlug = r.boolean("isCavityPlug", false);
    pin.notes = r.string("notes");
    pin.extra = r.extra();
    return pin;
}

Connector readConnector(ObjectReader r)
{
    Connector c;
    c.id = r.id();
    c.label = r.string("label");
    c.partNumber = r.string("partNumber");
    c.manufacturer = r.string("manufacturer");
    c.description = r.string("description");
    c.gender = r.enumValue("gender", kGenders, Gender::None);
    c.notes = r.string("notes");
    const QJsonArray pins = r.array("pins");
    const QString pinsContext = r.context() + QStringLiteral(".pins");
    for (qsizetype i = 0; i < pins.size(); ++i) {
        const QString context = indexed(pinsContext, i);
        c.pins.push_back(readPin(ObjectReader(requireObject(pins[i], context), context)));
    }
    c.extra = r.extra();
    return c;
}

WireEnd readWireEnd(ObjectReader r)
{
    const QString type = r.string("type", true);
    if (type == QLatin1StringView("pin"))
        return PinRef{r.id("connectorId"), r.id("pinId")};
    if (type == QLatin1StringView("splice"))
        return SpliceRef{r.id("spliceId")};
    if (type == QLatin1StringView("free")) {
        FreeEnd end;
        end.kind = r.enumValue("kind", kFreeEndKinds, FreeEndKind::FlyingLead);
        end.terminalPartNumber = r.string("terminalPartNumber");
        end.label = r.string("label");
        return end;
    }
    fail(Tr::tr("%1: unknown wire end type \"%2\".").arg(r.context(), type));
}

Wire readWire(ObjectReader r)
{
    Wire w;
    w.id = r.id();
    w.name = r.string("name");
    const QString base = r.context();
    w.endA = readWireEnd(ObjectReader(r.object("endA", true), base + QStringLiteral(".endA")));
    w.endB = readWireEnd(ObjectReader(r.object("endB", true), base + QStringLiteral(".endB")));
    w.baseColor = r.string("baseColor");
    w.stripeColor = r.string("stripeColor");
    ObjectReader gauge(r.object("gauge"), base + QStringLiteral(".gauge"));
    w.gauge.value = gauge.number("value", 0.0);
    w.gauge.system = gauge.enumValue("system", kGaugeSystems, GaugeSystem::Awg);
    w.wireTypeId = r.string("wireTypeId");
    w.conductorKind = r.enumValue("conductorKind", kConductorKinds, ConductorKind::Single);
    w.lengthOverrideMm = r.optionalNumber("lengthOverrideMm");
    const QJsonArray route = r.array("routeSegmentIds");
    for (qsizetype i = 0; i < route.size(); ++i) {
        if (!route[i].isString())
            fail(Tr::tr("%1 must be a string.").arg(indexed(base + QStringLiteral(".routeSegmentIds"), i)));
        w.routeSegmentIds.push_back(route[i].toString());
    }
    w.extra = r.extra();
    return w;
}

Splice readSplice(ObjectReader r)
{
    Splice s;
    s.id = r.id();
    s.label = r.string("label");
    s.partNumber = r.string("partNumber");
    s.extra = r.extra();
    return s;
}

LayoutNode readLayoutNode(ObjectReader r)
{
    LayoutNode n;
    n.id = r.id();
    n.type = r.enumValue("type", kLayoutNodeTypes, LayoutNodeType::Branch);
    n.refId = r.string("refId");
    ObjectReader pos(r.object("position"), r.context() + QStringLiteral(".position"));
    n.position = QPointF(pos.number("x", 0.0), pos.number("y", 0.0));
    n.extra = r.extra();
    return n;
}

LayoutSegment readLayoutSegment(ObjectReader r)
{
    LayoutSegment s;
    s.id = r.id();
    s.nodeA = r.id("nodeA");
    s.nodeB = r.id("nodeB");
    s.lengthMm = r.number("lengthMm", 0.0);
    s.label = r.string("label");
    s.sleevingId = r.string("sleevingId");
    s.sleevingCoverage = r.enumValue("sleevingCoverage", kSleevingCoverages, SleevingCoverage::Full);
    if (const QJsonObject bend = r.object("bend"); !bend.isEmpty()) {
        ObjectReader b(bend, r.context() + QStringLiteral(".bend"));
        s.bend = QPointF(b.number("x", 0.0, true), b.number("y", 0.0, true));
    }
    s.extra = r.extra();
    return s;
}

Note readNote(ObjectReader r, NoteCanvas canvas)
{
    Note n;
    n.id = r.id();
    n.canvas = canvas;
    n.text = r.string("text");
    ObjectReader pos(r.object("position"), r.context() + QStringLiteral(".position"));
    n.position = QPointF(pos.number("x", 0.0), pos.number("y", 0.0));
    n.extra = r.extra();
    return n;
}

Settings readSettings(ObjectReader r)
{
    Settings s;
    s.packingFactor = r.number("packingFactor", s.packingFactor);
    s.serviceLoopPercent = r.number("serviceLoopPercent", s.serviceLoopPercent);
    const QJsonObject strip = r.object("stripAllowanceMm");
    for (auto it = strip.begin(); it != strip.end(); ++it) {
        const auto type = enumFromString(kEndTypes, it.key());
        if (!type)
            fail(Tr::tr("settings.stripAllowanceMm: unknown end type \"%1\".").arg(it.key()));
        if (!it.value().isDouble())
            fail(Tr::tr("settings.stripAllowanceMm.%1 must be a number.").arg(it.key()));
        s.stripAllowanceMm[*type] = it.value().toDouble();
    }
    s.wasteFactorWirePercent = r.number("wasteFactorWirePercent", s.wasteFactorWirePercent);
    s.wasteFactorSleevingPercent = r.number("wasteFactorSleevingPercent", s.wasteFactorSleevingPercent);
    s.lengthRoundingIncrementMm = r.number("lengthRoundingIncrementMm", s.lengthRoundingIncrementMm);
    s.sleevingDiameterMarginPercent =
        r.number("sleevingDiameterMarginPercent", s.sleevingDiameterMarginPercent);
    s.extra = r.extra();
    return s;
}

template<typename T, typename ReadFn>
std::vector<T> readArray(const QJsonArray &array, const QString &base, ReadFn read)
{
    std::vector<T> items;
    items.reserve(size_t(array.size()));
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString context = indexed(base, i);
        items.push_back(read(ObjectReader(requireObject(array[i], context), context)));
    }
    return items;
}

// ---- integrity ------------------------------------------------------------

template<typename T>
QSet<QString> uniqueIds(const std::vector<T> &items, const QString &what)
{
    QSet<QString> ids;
    for (const T &item : items) {
        if (ids.contains(item.id))
            fail(Tr::tr("Duplicate %1 id \"%2\".").arg(what, item.id));
        ids.insert(item.id);
    }
    return ids;
}

void checkIntegrity(const Harness &h)
{
    std::map<QString, QSet<QString>> pinsByConnector;
    for (const Connector &c : h.connectors) {
        const qsizetype count = qsizetype(c.pins.size());
        if (count < kMinPins || count > kMaxPins)
            fail(Tr::tr("Connector \"%1\" has %2 pins; allowed range is %3-%4.")
                     .arg(c.label)
                     .arg(count)
                     .arg(kMinPins)
                     .arg(kMaxPins));
        if (pinsByConnector.count(c.id))
            fail(Tr::tr("Duplicate connector id \"%1\".").arg(c.id));
        pinsByConnector[c.id] = uniqueIds(c.pins, Tr::tr("pin"));
    }
    const QSet<QString> splices = uniqueIds(h.splices, Tr::tr("splice"));
    const QSet<QString> nodes = uniqueIds(h.layoutNodes, Tr::tr("layout node"));
    const QSet<QString> segments = uniqueIds(h.layoutSegments, Tr::tr("layout segment"));
    uniqueIds(h.notes, Tr::tr("note"));
    uniqueIds(h.wires, Tr::tr("wire"));
    uniqueIds(h.bomExtras, Tr::tr("BOM row"));

    auto checkEnd = [&](const Wire &w, const WireEnd &end) {
        if (const auto *pin = std::get_if<PinRef>(&end)) {
            const auto it = pinsByConnector.find(pin->connectorId);
            if (it == pinsByConnector.end())
                fail(Tr::tr("Wire \"%1\" references missing connector \"%2\".").arg(w.id, pin->connectorId));
            if (!it->second.contains(pin->pinId))
                fail(Tr::tr("Wire \"%1\" references missing pin \"%2\".").arg(w.id, pin->pinId));
        } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
            if (!splices.contains(splice->spliceId))
                fail(Tr::tr("Wire \"%1\" references missing splice \"%2\".").arg(w.id, splice->spliceId));
        }
    };
    for (const Wire &w : h.wires) {
        checkEnd(w, w.endA);
        checkEnd(w, w.endB);
        for (const QString &segmentId : w.routeSegmentIds) {
            if (!segments.contains(segmentId))
                fail(Tr::tr("Wire \"%1\" routes through missing segment \"%2\".").arg(w.id, segmentId));
        }
    }
    QSet<QString> freeEnds;
    for (const Wire &w : h.wires) {
        if (std::holds_alternative<FreeEnd>(w.endA))
            freeEnds.insert(freeEndPlacementKey(w.id, false));
        if (std::holds_alternative<FreeEnd>(w.endB))
            freeEnds.insert(freeEndPlacementKey(w.id, true));
    }
    QSet<QString> referenced;
    for (const LayoutNode &n : h.layoutNodes) {
        if (n.refId.isEmpty())
            continue;
        bool ok = false;
        switch (n.type) {
        case LayoutNodeType::Connector:
            ok = pinsByConnector.count(n.refId) > 0;
            break;
        case LayoutNodeType::Splice:
            ok = splices.contains(n.refId);
            break;
        case LayoutNodeType::FlyingLead:
        case LayoutNodeType::Eyelet:
            ok = freeEnds.contains(n.refId);
            break;
        case LayoutNodeType::Branch:
            break;
        }
        if (!ok)
            fail(Tr::tr("Layout node \"%1\" references missing item \"%2\".").arg(n.id, n.refId));
        if (referenced.contains(n.refId))
            fail(Tr::tr("More than one layout node references \"%1\".").arg(n.refId));
        referenced.insert(n.refId);
    }
    for (const LayoutSegment &s : h.layoutSegments) {
        if (!nodes.contains(s.nodeA) || !nodes.contains(s.nodeB))
            fail(Tr::tr("Layout segment \"%1\" references a missing node.").arg(s.id));
        if (s.nodeA == s.nodeB)
            fail(Tr::tr("Layout segment \"%1\" starts and ends at the same node.").arg(s.id));
        if (s.lengthMm < 0)
            fail(Tr::tr("Layout segment \"%1\" has a negative length.").arg(s.id));
    }
}

// ---- writing --------------------------------------------------------------

QJsonObject writePin(const Pin &p)
{
    QJsonObject o = p.extra;
    o[u"id"] = p.id;
    o[u"label"] = p.label;
    o[u"circuitText"] = p.circuitText;
    o[u"terminalPartNumber"] = p.terminalPartNumber;
    o[u"sealPartNumber"] = p.sealPartNumber;
    o[u"isCavityPlug"] = p.isCavityPlug;
    o[u"notes"] = p.notes;
    return o;
}

QJsonObject writeConnector(const Connector &c)
{
    QJsonObject o = c.extra;
    o[u"id"] = c.id;
    o[u"label"] = c.label;
    o[u"partNumber"] = c.partNumber;
    o[u"manufacturer"] = c.manufacturer;
    o[u"description"] = c.description;
    o[u"gender"] = enumToString(kGenders, c.gender);
    o[u"notes"] = c.notes;
    QJsonArray pins;
    for (const Pin &p : c.pins)
        pins.append(writePin(p));
    o[u"pins"] = pins;
    return o;
}

QJsonObject writeWireEnd(const WireEnd &end)
{
    QJsonObject o;
    if (const auto *pin = std::get_if<PinRef>(&end)) {
        o[u"type"] = QStringLiteral("pin");
        o[u"connectorId"] = pin->connectorId;
        o[u"pinId"] = pin->pinId;
    } else if (const auto *splice = std::get_if<SpliceRef>(&end)) {
        o[u"type"] = QStringLiteral("splice");
        o[u"spliceId"] = splice->spliceId;
    } else {
        const auto &free = std::get<FreeEnd>(end);
        o[u"type"] = QStringLiteral("free");
        o[u"kind"] = enumToString(kFreeEndKinds, free.kind);
        o[u"terminalPartNumber"] = free.terminalPartNumber;
        o[u"label"] = free.label;
    }
    return o;
}

QJsonObject writeWire(const Wire &w)
{
    QJsonObject o = w.extra;
    o[u"id"] = w.id;
    o[u"name"] = w.name;
    o[u"endA"] = writeWireEnd(w.endA);
    o[u"endB"] = writeWireEnd(w.endB);
    o[u"baseColor"] = w.baseColor;
    o[u"stripeColor"] = w.stripeColor;
    o[u"gauge"] = QJsonObject{{u"value"_s, w.gauge.value},
                              {u"system"_s, enumToString(kGaugeSystems, w.gauge.system)}};
    o[u"wireTypeId"] = w.wireTypeId;
    o[u"conductorKind"] = enumToString(kConductorKinds, w.conductorKind);
    if (w.lengthOverrideMm)
        o[u"lengthOverrideMm"] = *w.lengthOverrideMm;
    else
        o.remove(u"lengthOverrideMm");
    QJsonArray route;
    for (const QString &id : w.routeSegmentIds)
        route.append(id);
    o[u"routeSegmentIds"] = route;
    return o;
}

QJsonObject writeSplice(const Splice &s)
{
    QJsonObject o = s.extra;
    o[u"id"] = s.id;
    o[u"label"] = s.label;
    o[u"partNumber"] = s.partNumber;
    return o;
}

QJsonObject writeLayoutNode(const LayoutNode &n)
{
    QJsonObject o = n.extra;
    o[u"id"] = n.id;
    o[u"type"] = enumToString(kLayoutNodeTypes, n.type);
    o[u"refId"] = n.refId;
    o[u"position"] = QJsonObject{{u"x"_s, n.position.x()}, {u"y"_s, n.position.y()}};
    return o;
}

QJsonObject writeLayoutSegment(const LayoutSegment &s)
{
    QJsonObject o = s.extra;
    o[u"id"] = s.id;
    o[u"nodeA"] = s.nodeA;
    o[u"nodeB"] = s.nodeB;
    o[u"lengthMm"] = s.lengthMm;
    o[u"label"] = s.label;
    o[u"sleevingId"] = s.sleevingId;
    o[u"sleevingCoverage"] = enumToString(kSleevingCoverages, s.sleevingCoverage);
    if (s.bend)
        o[u"bend"] = QJsonObject{{u"x"_s, s.bend->x()}, {u"y"_s, s.bend->y()}};
    return o;
}

QJsonObject writeNote(const Note &n)
{
    QJsonObject o = n.extra;
    o[u"id"] = n.id;
    o[u"text"] = n.text;
    o[u"position"] = QJsonObject{{u"x"_s, n.position.x()}, {u"y"_s, n.position.y()}};
    return o;
}

QJsonObject writeSettings(const Settings &s)
{
    QJsonObject o = s.extra;
    o[u"packingFactor"] = s.packingFactor;
    o[u"serviceLoopPercent"] = s.serviceLoopPercent;
    QJsonObject strip;
    for (const auto &[type, mm] : s.stripAllowanceMm)
        strip[enumToString(kEndTypes, type)] = mm;
    o[u"stripAllowanceMm"] = strip;
    o[u"wasteFactorWirePercent"] = s.wasteFactorWirePercent;
    o[u"wasteFactorSleevingPercent"] = s.wasteFactorSleevingPercent;
    o[u"lengthRoundingIncrementMm"] = s.lengthRoundingIncrementMm;
    o[u"sleevingDiameterMarginPercent"] = s.sleevingDiameterMarginPercent;
    return o;
}

QJsonObject writeSchematic(const Schematic &schematic, const QJsonArray &notes)
{
    QJsonObject o = schematic.extra;
    QJsonObject placements;
    for (const auto &[key, p] : schematic.placements) {
        QJsonObject po = p.extra;
        po[u"x"] = p.position.x();
        po[u"y"] = p.position.y();
        po[u"collapsed"] = p.collapsed;
        po[u"maxVisibleRows"] = p.maxVisibleRows;
        placements[key] = po;
    }
    o[u"placements"] = placements;
    o[u"notes"] = notes;
    return o;
}

Schematic readSchematic(ObjectReader r)
{
    Schematic schematic;
    const QJsonObject placements = r.object("placements");
    for (auto it = placements.begin(); it != placements.end(); ++it) {
        const QString context = QStringLiteral("schematic.placements.%1").arg(it.key());
        ObjectReader pr(requireObject(it.value(), context), context);
        SchematicPlacement p;
        p.position = QPointF(pr.number("x", 0.0), pr.number("y", 0.0));
        p.collapsed = pr.boolean("collapsed", false);
        p.maxVisibleRows = int(pr.number("maxVisibleRows", 0));
        if (p.maxVisibleRows < 0)
            fail(Tr::tr("%1: \"maxVisibleRows\" must not be negative.").arg(context));
        p.extra = pr.extra();
        schematic.placements[it.key()] = p;
    }
    schematic.extra = r.extra();
    return schematic;
}

Gauge readGauge(ObjectReader r)
{
    Gauge g;
    g.value = r.number("value", 0.0);
    g.system = r.enumValue("system", kGaugeSystems, GaugeSystem::Awg);
    return g;
}

QJsonObject writeGauge(const Gauge &g)
{
    return QJsonObject{{u"value"_s, g.value}, {u"system"_s, enumToString(kGaugeSystems, g.system)}};
}

Library readLibrary(ObjectReader r)
{
    Library library;
    const QJsonArray types = r.array("wireTypes");
    for (qsizetype i = 0; i < types.size(); ++i) {
        const QString context = indexed(QStringLiteral("library.wireTypes"), i);
        ObjectReader typeReader(requireObject(types[i], context), context);
        WireType t;
        t.id = typeReader.id();
        t.name = typeReader.string("name");
        t.approximate = typeReader.boolean("approximate", false);
        const QJsonArray ods = typeReader.array("outerDiameters");
        for (qsizetype j = 0; j < ods.size(); ++j) {
            const QString odContext = indexed(context + QStringLiteral(".outerDiameters"), j);
            ObjectReader odr(requireObject(ods[j], odContext), odContext);
            WireOuterDiameter od;
            od.gauge = readGauge(ObjectReader(odr.object("gauge", true), odContext + QStringLiteral(".gauge")));
            od.odMm = odr.number("odMm", 0.0, true);
            if (od.odMm <= 0)
                fail(Tr::tr("%1: \"odMm\" must be positive.")
                         .arg(odContext));
            t.outerDiameters.push_back(od);
        }
        t.extra = typeReader.extra();
        if (library.wireType(t.id))
            fail(Tr::tr("Duplicate wire type id \"%1\".").arg(t.id));
        library.wireTypes.push_back(t);
    }
    const QJsonArray sleevings = r.array("sleevings");
    for (qsizetype i = 0; i < sleevings.size(); ++i) {
        const QString context = indexed(QStringLiteral("library.sleevings"), i);
        ObjectReader sr(requireObject(sleevings[i], context), context);
        Sleeving s;
        s.id = sr.id();
        s.family = sr.string("family");
        s.kind = sr.enumValue("kind", kSleevingKinds, SleevingKind::Other);
        s.sizeLabel = sr.string("sizeLabel");
        s.innerDiameterMm = sr.number("innerDiameterMm", 0.0, true);
        s.recoveredDiameterMm = sr.optionalNumber("recoveredDiameterMm");
        s.approximate = sr.boolean("approximate", false);
        s.extra = sr.extra();
        if (library.sleeving(s.id))
            fail(Tr::tr("Duplicate sleeving id \"%1\".").arg(s.id));
        library.sleevings.push_back(s);
    }
    return library;
}

QJsonObject writeLibrary(const Library &library)
{
    QJsonArray types;
    for (const WireType &t : library.wireTypes) {
        QJsonObject o = t.extra;
        o[u"id"] = t.id;
        o[u"name"] = t.name;
        o[u"approximate"] = t.approximate;
        QJsonArray ods;
        for (const WireOuterDiameter &od : t.outerDiameters)
            ods.append(QJsonObject{{u"gauge"_s, writeGauge(od.gauge)}, {u"odMm"_s, od.odMm}});
        o[u"outerDiameters"] = ods;
        types.append(o);
    }
    QJsonArray sleevings;
    for (const Sleeving &s : library.sleevings) {
        QJsonObject o = s.extra;
        o[u"id"] = s.id;
        o[u"family"] = s.family;
        o[u"kind"] = enumToString(kSleevingKinds, s.kind);
        o[u"sizeLabel"] = s.sizeLabel;
        o[u"innerDiameterMm"] = s.innerDiameterMm;
        o[u"approximate"] = s.approximate;
        if (s.recoveredDiameterMm)
            o[u"recoveredDiameterMm"] = *s.recoveredDiameterMm;
        else
            o.remove(u"recoveredDiameterMm");
        sleevings.append(o);
    }
    return QJsonObject{{u"wireTypes"_s, types}, {u"sleevings"_s, sleevings}};
}

// Placements whose item no longer exists carry no information; drop them.
void dropOrphanPlacements(Harness &h)
{
    QSet<QString> keys;
    for (const Connector &c : h.connectors)
        keys.insert(c.id);
    for (const Splice &s : h.splices)
        keys.insert(s.id);
    for (const Wire &w : h.wires) {
        keys.insert(w.id); // the wire's route (x of its vertical run)
        if (std::holds_alternative<FreeEnd>(w.endA))
            keys.insert(freeEndPlacementKey(w.id, false));
        if (std::holds_alternative<FreeEnd>(w.endB))
            keys.insert(freeEndPlacementKey(w.id, true));
    }
    std::erase_if(h.schematic.placements, [&](const auto &entry) { return !keys.contains(entry.first); });
}

template<typename T, typename WriteFn>
QByteArray toJsonArrayBytes(const std::vector<T> &items, WriteFn write)
{
    QJsonArray array;
    for (const T &item : items)
        array.append(write(item));
    return QJsonDocument(array).toJson(QJsonDocument::Indented);
}

bool isKnownEntry(const QString &name)
{
    return name == kManifestEntry || name == kHarnessEntry || name == kConnectorsEntry
           || name == kWiresEntry || name == kSplicesEntry || name == kLayoutEntry
           || name == kSchematicEntry || name == kLibraryEntry || name == kBomEntry;
}

} // namespace

ZipEntries serializeHarness(const Harness &h)
{
    ZipEntries entries;
    for (const auto &[name, data] : h.otherEntries) {
        if (!isKnownEntry(name))
            entries[name] = data;
    }

    QJsonObject harness = h.extra;
    harness[u"name"] = h.info.name;
    harness[u"description"] = h.info.description;
    harness[u"revision"] = h.info.revision;
    harness[u"author"] = h.info.author;
    harness[u"units"] = unitsSymbol(h.info.units);
    harness[u"settings"] = writeSettings(h.settings);
    entries[QString(kHarnessEntry)] = QJsonDocument(harness).toJson(QJsonDocument::Indented);

    entries[QString(kConnectorsEntry)] = toJsonArrayBytes(h.connectors, writeConnector);
    entries[QString(kWiresEntry)] = toJsonArrayBytes(h.wires, writeWire);
    entries[QString(kSplicesEntry)] = toJsonArrayBytes(h.splices, writeSplice);

    QJsonArray nodes;
    for (const LayoutNode &n : h.layoutNodes)
        nodes.append(writeLayoutNode(n));
    QJsonArray segments;
    for (const LayoutSegment &s : h.layoutSegments)
        segments.append(writeLayoutSegment(s));
    // Each canvas file holds its own notes.
    QJsonArray layoutNotes;
    QJsonArray schematicNotes;
    for (const Note &n : h.notes)
        (n.canvas == NoteCanvas::Layout ? layoutNotes : schematicNotes).append(writeNote(n));
    entries[QString(kLayoutEntry)] =
        QJsonDocument(QJsonObject{{u"nodes"_s, nodes}, {u"segments"_s, segments}, {u"notes"_s, layoutNotes}})
            .toJson(QJsonDocument::Indented);
    entries[QString(kSchematicEntry)] =
        QJsonDocument(writeSchematic(h.schematic, schematicNotes)).toJson(QJsonDocument::Indented);
    entries[QString(kLibraryEntry)] = serializeLibrary(h.library);
    QJsonArray extras;
    for (const BomExtra &e : h.bomExtras) {
        QJsonObject o = e.extra;
        o[u"id"] = e.id;
        o[u"partNumber"] = e.partNumber;
        o[u"description"] = e.description;
        o[u"quantity"] = e.quantity;
        o[u"unit"] = e.unit;
        o[u"notes"] = e.notes;
        extras.append(o);
    }
    entries[QString(kBomEntry)] =
        QJsonDocument(QJsonObject{{u"extras"_s, extras}}).toJson(QJsonDocument::Indented);
    return entries;
}

std::optional<Harness> deserializeHarness(const ZipEntries &entries, QString *error)
{
    try {
        Harness h;

        const QJsonDocument harnessDoc = parseEntry(entries, kHarnessEntry, true);
        if (!harnessDoc.isObject())
            fail(Tr::tr("\"%1\" must contain a JSON object.").arg(kHarnessEntry));
        ObjectReader r(harnessDoc.object(), QStringLiteral("harness"));
        h.info.name = r.string("name");
        h.info.description = r.string("description");
        h.info.revision = r.string("revision");
        h.info.author = r.string("author");
        const QString units = r.string("units");
        if (!units.isEmpty()) {
            const auto parsed = unitsFromSymbol(units);
            if (!parsed)
                fail(Tr::tr("harness: unknown value \"%1\" for \"units\".").arg(units));
            h.info.units = *parsed;
        }
        h.settings = readSettings(ObjectReader(r.object("settings"), QStringLiteral("settings")));
        h.extra = r.extra();

        h.connectors = readArray<Connector>(entryArray(entries, kConnectorsEntry),
                                            QStringLiteral("connectors"), readConnector);
        h.wires = readArray<Wire>(entryArray(entries, kWiresEntry), QStringLiteral("wires"), readWire);
        h.splices = readArray<Splice>(entryArray(entries, kSplicesEntry), QStringLiteral("splices"),
                                      readSplice);

        const QJsonDocument layoutDoc = parseEntry(entries, kLayoutEntry, false);
        if (!layoutDoc.isNull()) {
            if (!layoutDoc.isObject())
                fail(Tr::tr("\"%1\" must contain a JSON object.").arg(kLayoutEntry));
            ObjectReader layout(layoutDoc.object(), QStringLiteral("layout"));
            h.layoutNodes = readArray<LayoutNode>(layout.array("nodes"), QStringLiteral("layout.nodes"),
                                                  readLayoutNode);
            h.layoutSegments = readArray<LayoutSegment>(layout.array("segments"),
                                                        QStringLiteral("layout.segments"),
                                                        readLayoutSegment);
            h.notes = readArray<Note>(layout.array("notes"), QStringLiteral("layout.notes"),
                                      [](ObjectReader r) { return readNote(std::move(r), NoteCanvas::Layout); });
            // Unknown top-level keys in layout.json are not preserved in v1.
        }

        const QJsonDocument schematicDoc = parseEntry(entries, kSchematicEntry, false);
        if (!schematicDoc.isNull()) {
            if (!schematicDoc.isObject())
                fail(Tr::tr("\"%1\" must contain a JSON object.").arg(kSchematicEntry));
            ObjectReader schematic(schematicDoc.object(), QStringLiteral("schematic"));
            const std::vector<Note> notes =
                readArray<Note>(schematic.array("notes"), QStringLiteral("schematic.notes"),
                                [](ObjectReader r) { return readNote(std::move(r), NoteCanvas::Schematic); });
            h.notes.insert(h.notes.end(), notes.begin(), notes.end());
            h.schematic = readSchematic(std::move(schematic));
        }

        const QJsonDocument libraryDoc = parseEntry(entries, kLibraryEntry, false);
        if (!libraryDoc.isNull()) {
            if (!libraryDoc.isObject())
                fail(Tr::tr("\"%1\" must contain a JSON object.").arg(kLibraryEntry));
            h.library = readLibrary(ObjectReader(libraryDoc.object(), QStringLiteral("library")));
        }

        const QJsonDocument bomDoc = parseEntry(entries, kBomEntry, false);
        if (!bomDoc.isNull()) {
            if (!bomDoc.isObject())
                fail(Tr::tr("\"%1\" must contain a JSON object.").arg(kBomEntry));
            ObjectReader bom(bomDoc.object(), QStringLiteral("bom"));
            h.bomExtras = readArray<BomExtra>(bom.array("extras"), QStringLiteral("bom.extras"), [](ObjectReader r) {
                BomExtra e;
                e.id = r.id();
                e.partNumber = r.string("partNumber");
                e.description = r.string("description");
                e.quantity = r.number("quantity", 1.0);
                e.unit = r.string("unit");
                e.notes = r.string("notes");
                e.extra = r.extra();
                return e;
            });
        }

        for (const auto &[name, data] : entries) {
            if (!isKnownEntry(name))
                h.otherEntries[name] = data;
        }

        checkIntegrity(h);
        dropOrphanPlacements(h);
        return h;
    } catch (const FormatError &e) {
        if (error)
            *error = e.message;
        return std::nullopt;
    }
}

QByteArray serializeLibrary(const Library &library)
{
    return QJsonDocument(writeLibrary(library)).toJson(QJsonDocument::Indented);
}

std::optional<Library> deserializeLibrary(const QByteArray &json, QString *error)
{
    try {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            fail(Tr::tr("The library is not valid JSON: %1").arg(parseError.errorString()));
        return readLibrary(ObjectReader(doc.object(), QStringLiteral("library")));
    } catch (const FormatError &e) {
        if (error)
            *error = e.message;
        return std::nullopt;
    }
}

} // namespace tbh::io
