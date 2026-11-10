// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/EditOps.h"

#include "commands/EntityCommands.h"
#include "model/Fragment.h"
#include "model/PinLabels.h"
#include "model/Queries.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace tbh::app {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::app::EditOps)
};
} // namespace

namespace {

// Child commands that update the connectors and wires changed by a fill in `after`.
// `skipConnectorId` / `skipWireId` are updated by the caller already.
void addFillUpdates(HarnessModel *model, const Harness &after, const CircuitTextFill &fill,
                    QUndoCommand *parent, const QString &skipConnectorId = {},
                    const QString &skipWireId = {})
{
    for (const Connector &c : after.connectors) {
        if (fill.connectorIds.contains(c.id) && c.id != skipConnectorId)
            new UpdateEntityCommand<Connector>(model, c, {}, parent);
    }
    for (const Wire &w : after.wires) {
        if (fill.wireIds.contains(w.id) && w.id != skipWireId)
            new UpdateEntityCommand<Wire>(model, w, {}, parent);
    }
}

template<typename T>
const T &byId(const std::vector<T> &items, const QString &id)
{
    return *std::find_if(items.begin(), items.end(), [&](const T &item) { return item.id == id; });
}

// Free-end keys of the given wires (layout nodes may reference them).
QSet<QString> freeEndKeys(const HarnessModel *model, const std::vector<QString> &wireIds)
{
    QSet<QString> keys;
    for (const QString &id : wireIds) {
        if (const Wire *w = model->find<Wire>(id)) {
            if (std::holds_alternative<FreeEnd>(w->endA))
                keys.insert(freeEndPlacementKey(id, false));
            if (std::holds_alternative<FreeEnd>(w->endB))
                keys.insert(freeEndPlacementKey(id, true));
        }
    }
    return keys;
}

// Layout nodes pointing at items that are going away keep their place but lose the reference.
void clearLayoutRefs(HarnessModel *model, const QSet<QString> &refs, QUndoCommand *parent)
{
    if (refs.isEmpty())
        return;
    for (const LayoutNode &n : model->harness().layoutNodes) {
        LayoutNode updated = n;
        std::erase_if(updated.sharedSpliceIds, [&](const QString &id) { return refs.contains(id); });
        if (!updated.refId.isEmpty() && refs.contains(updated.refId)) {
            updated.refId.clear();
            // A splice sharing the point takes it over.
            if (!updated.sharedSpliceIds.empty()) {
                updated.refId = updated.sharedSpliceIds.front();
                updated.sharedSpliceIds.erase(updated.sharedSpliceIds.begin());
            }
        }
        if (updated != n)
            new UpdateEntityCommand<LayoutNode>(model, updated, {}, parent);
    }
}

} // namespace

std::vector<QString> wiresRemovedWith(const Harness &harness, const QSet<QString> &connectorIds,
                                      const QSet<QString> &spliceIds, const QSet<QString> &wireIds)
{
    QSet<QString> all = wireIds;
    for (const QString &id : connectorIds) {
        for (const QString &w : wiresOnConnector(harness, id))
            all.insert(w);
    }
    for (const QString &id : spliceIds) {
        for (const QString &w : wiresOnSplice(harness, id))
            all.insert(w);
    }
    // Keep model order so undo restores deterministically.
    std::vector<QString> ordered;
    for (const Wire &w : harness.wires) {
        if (all.contains(w.id))
            ordered.push_back(w.id);
    }
    return ordered;
}

namespace {

void removeNotes(HarnessModel *model, const QSet<QString> &noteIds, QUndoCommand *macro)
{
    const std::vector<Note> &notes = model->harness().notes;
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
        if (noteIds.contains(it->id))
            new RemoveEntityCommand<Note>(model, it->id, {}, macro);
    }
}

} // namespace

std::unique_ptr<QUndoCommand> setNoteText(HarnessModel *model, const QString &noteId, const QString &text)
{
    const Note *current = model->find<Note>(noteId);
    const QString trimmed = text.trimmed();
    if (!current || trimmed == current->text)
        return nullptr;
    if (trimmed.isEmpty())
        return std::make_unique<RemoveEntityCommand<Note>>(model, noteId, Tr::tr("Delete note"));
    Note updated = *current;
    updated.text = trimmed;
    return std::make_unique<UpdateEntityCommand<Note>>(model, updated, Tr::tr("Edit note"));
}

std::unique_ptr<QUndoCommand> removeItems(HarnessModel *model, const QSet<QString> &connectorIds,
                                          const QSet<QString> &spliceIds,
                                          const QSet<QString> &wireIds,
                                          const QSet<QString> &noteIds)
{
    const Harness &h = model->harness();
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Delete"));
    // Children redo in order and undo in reverse: wires go first and come back last.
    // Within each list, remove from the back so the recorded indices restore the order.
    const std::vector<QString> wires = wiresRemovedWith(h, connectorIds, spliceIds, wireIds);
    clearLayoutRefs(model, connectorIds + spliceIds + freeEndKeys(model, wires), macro.get());
    for (auto it = wires.rbegin(); it != wires.rend(); ++it)
        new RemoveEntityCommand<Wire>(model, *it, {}, macro.get());
    for (auto it = h.splices.rbegin(); it != h.splices.rend(); ++it) {
        if (spliceIds.contains(it->id))
            new RemoveEntityCommand<Splice>(model, it->id, {}, macro.get());
    }
    for (auto it = h.connectors.rbegin(); it != h.connectors.rend(); ++it) {
        if (connectorIds.contains(it->id))
            new RemoveEntityCommand<Connector>(model, it->id, {}, macro.get());
    }
    removeNotes(model, noteIds, macro.get());
    return macro;
}

std::vector<QString> wiresLostByPinChange(const Harness &harness, const Connector &connector,
                                          const std::vector<Pin> &newPins)
{
    QSet<QString> kept;
    for (const Pin &p : newPins)
        kept.insert(p.id);
    QSet<QString> lost;
    for (const Pin &p : connector.pins) {
        if (!kept.contains(p.id))
            lost.insert(p.id);
    }
    return lost.isEmpty() ? std::vector<QString>{} : wiresOnPins(harness, connector.id, lost);
}

std::unique_ptr<QUndoCommand> setConnectorPins(HarnessModel *model, const QString &connectorId,
                                               std::vector<Pin> newPins, const QString &text)
{
    Connector connector = *model->find<Connector>(connectorId);
    const std::vector<QString> lost = wiresLostByPinChange(model->harness(), connector, newPins);
    connector.pins = std::move(newPins);
    auto macro = std::make_unique<QUndoCommand>(text);
    clearLayoutRefs(model, freeEndKeys(model, lost), macro.get());
    // Remove from the back so indices recorded for undo stay valid.
    std::vector<QString> ordered = lost;
    std::sort(ordered.begin(), ordered.end(), [&](const QString &a, const QString &b) {
        return model->indexOf<Wire>(a) > model->indexOf<Wire>(b);
    });
    for (const QString &wireId : ordered)
        new RemoveEntityCommand<Wire>(model, wireId, {}, macro.get());
    new UpdateEntityCommand<Connector>(model, std::move(connector), {}, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> pasteFragment(HarnessModel *model, Harness fragment, QPointF offset,
                                            QSet<QString> *newItemIds)
{
    assignNewIds(fragment);
    for (auto &[key, placement] : fragment.schematic.placements)
        placement.position += offset;
    auto placementsOf = [&](EntityKind kind, const QString &id) {
        Placements result;
        for (const QString &key : HarnessModel::placementKeys(kind, id)) {
            const auto it = fragment.schematic.placements.find(key);
            if (it != fragment.schematic.placements.end())
                result[key] = it->second;
        }
        return result;
    };

    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Paste"));
    for (Connector &c : fragment.connectors) {
        if (newItemIds)
            newItemIds->insert(c.id);
        Placements p = placementsOf(EntityKind::Connector, c.id);
        new AddEntityCommand<Connector>(model, std::move(c), std::move(p), {}, macro.get());
    }
    for (Splice &s : fragment.splices) {
        if (newItemIds)
            newItemIds->insert(s.id);
        Placements p = placementsOf(EntityKind::Splice, s.id);
        new AddEntityCommand<Splice>(model, std::move(s), std::move(p), {}, macro.get());
    }
    for (Wire &w : fragment.wires) {
        Placements p = placementsOf(EntityKind::Wire, w.id);
        new AddEntityCommand<Wire>(model, std::move(w), std::move(p), {}, macro.get());
    }
    return macro;
}

std::unique_ptr<QUndoCommand> addWire(HarnessModel *model, Wire wire, Placements placements,
                                      const QString &text)
{
    Harness after = model->harness();
    after.wires.push_back(std::move(wire));
    const CircuitTextFill fill = fillEmptyCircuitText(after, {after.wires.back().id});
    auto macro = std::make_unique<QUndoCommand>(text);
    new AddEntityCommand<Wire>(model, after.wires.back(), std::move(placements), {}, macro.get());
    addFillUpdates(model, after, fill, macro.get(), {}, after.wires.back().id);
    return macro;
}

std::unique_ptr<QUndoCommand> updateConnector(HarnessModel *model, const Connector &updated,
                                              const QString &text)
{
    const Connector *current = model->find<Connector>(updated.id);
    if (!current || *current == updated)
        return nullptr;
    QSet<QString> retexted;
    for (const Pin &pin : updated.pins) {
        if (pin.circuitText.isEmpty())
            continue;
        const auto old = std::find_if(current->pins.begin(), current->pins.end(),
                                      [&](const Pin &p) { return p.id == pin.id; });
        if (old == current->pins.end() || old->circuitText != pin.circuitText)
            retexted.insert(pin.id);
    }
    Harness after = model->harness();
    auto it = std::find_if(after.connectors.begin(), after.connectors.end(),
                           [&](const Connector &c) { return c.id == updated.id; });
    *it = updated;
    const CircuitTextFill fill =
        fillEmptyCircuitText(after, wiresOnPins(after, updated.id, retexted));
    auto macro = std::make_unique<QUndoCommand>(text);
    new UpdateEntityCommand<Connector>(model, byId(after.connectors, updated.id), {}, macro.get());
    addFillUpdates(model, after, fill, macro.get(), updated.id);
    return macro;
}

std::unique_ptr<QUndoCommand> updateWires(HarnessModel *model, const std::vector<Wire> &wires,
                                          const QString &text)
{
    auto macro = std::make_unique<QUndoCommand>(text);
    for (const Wire &w : wires) {
        if (*model->find<Wire>(w.id) != w)
            new UpdateEntityCommand<Wire>(model, w, {}, macro.get());
    }
    return macro;
}

std::unique_ptr<QUndoCommand> removeLayoutItems(HarnessModel *model, const QSet<QString> &nodeIds,
                                                const QSet<QString> &segmentIds,
                                                const QSet<QString> &noteIds)
{
    const Harness &h = model->harness();
    QSet<QString> segments = segmentIds;
    for (const LayoutSegment &s : h.layoutSegments) {
        if (nodeIds.contains(s.nodeA) || nodeIds.contains(s.nodeB))
            segments.insert(s.id);
    }
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Delete"));
    for (const Wire &w : h.wires) {
        const bool usesRemoved = std::any_of(w.routeSegmentIds.begin(), w.routeSegmentIds.end(),
                                             [&](const QString &id) { return segments.contains(id); });
        if (usesRemoved) {
            Wire updated = w;
            updated.routeSegmentIds.clear();
            new UpdateEntityCommand<Wire>(model, updated, {}, macro.get());
        }
    }
    // Remove from the back so recorded indices restore the original order on undo.
    for (auto it = h.layoutSegments.rbegin(); it != h.layoutSegments.rend(); ++it) {
        if (segments.contains(it->id))
            new RemoveEntityCommand<LayoutSegment>(model, it->id, {}, macro.get());
    }
    for (auto it = h.layoutNodes.rbegin(); it != h.layoutNodes.rend(); ++it) {
        if (nodeIds.contains(it->id))
            new RemoveEntityCommand<LayoutNode>(model, it->id, {}, macro.get());
    }
    removeNotes(model, noteIds, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> splitSegmentAt(HarnessModel *model, const QString &segmentId,
                                             const QString &nodeId, double fraction,
                                             std::optional<QPointF> bendA,
                                             std::optional<QPointF> bendB, QString *newSegmentId)
{
    const LayoutSegment *segment = model->find<LayoutSegment>(segmentId);
    if (!segment || !model->find<LayoutNode>(nodeId))
        return nullptr;
    const Harness &h = model->harness();
    if (std::any_of(h.layoutSegments.begin(), h.layoutSegments.end(),
                    [&](const LayoutSegment &s) { return s.nodeA == nodeId || s.nodeB == nodeId; }))
        return nullptr;
    fraction = std::clamp(fraction, 0.0, 1.0);
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Place in segment"));
    for (const Wire &w : h.wires) {
        if (std::find(w.routeSegmentIds.begin(), w.routeSegmentIds.end(), segmentId) != w.routeSegmentIds.end()) {
            Wire updated = w;
            updated.routeSegmentIds.clear();
            new UpdateEntityCommand<Wire>(model, updated, {}, macro.get());
        }
    }
    LayoutSegment first = *segment;
    LayoutSegment second = *segment;
    first.nodeB = nodeId;
    first.lengthMm = segment->lengthMm * fraction;
    first.bend = bendA;
    second.id = newId();
    second.nodeA = nodeId;
    second.lengthMm = segment->lengthMm - first.lengthMm;
    second.bend = bendB;
    second.extra = {};
    if (newSegmentId)
        *newSegmentId = second.id;
    new UpdateEntityCommand<LayoutSegment>(model, std::move(first), {}, macro.get());
    new AddEntityCommand<LayoutSegment>(model, std::move(second), {}, {}, macro.get());
    return macro;
}

namespace {

std::vector<const LayoutSegment *> segmentsOn(const Harness &h, const QString &nodeId)
{
    std::vector<const LayoutSegment *> legs;
    for (const LayoutSegment &s : h.layoutSegments) {
        if (s.nodeA == nodeId || s.nodeB == nodeId)
            legs.push_back(&s);
    }
    return legs;
}

// The splice node of a splice stub, or empty.
QString stubSpliceNode(const Harness &h, const LayoutSegment &stub)
{
    for (const QString &id : {stub.nodeA, stub.nodeB}) {
        const auto node = std::find_if(h.layoutNodes.begin(), h.layoutNodes.end(),
                                       [&](const LayoutNode &n) { return n.id == id; });
        if (node != h.layoutNodes.end() && node->type == LayoutNodeType::Splice && segmentsOn(h, id).size() == 1)
            return id;
    }
    return {};
}

} // namespace

QString spliceOfStub(const Harness &harness, const QString &stubId)
{
    const auto stub = std::find_if(harness.layoutSegments.begin(), harness.layoutSegments.end(),
                                   [&](const LayoutSegment &s) { return s.id == stubId; });
    return stub != harness.layoutSegments.end() ? stubSpliceNode(harness, *stub) : QString();
}

std::optional<LoomPosition> loomPositionForStub(const Harness &harness, const QString &stubId,
                                                double distanceMm)
{
    const auto stub = std::find_if(harness.layoutSegments.begin(), harness.layoutSegments.end(),
                                   [&](const LayoutSegment &s) { return s.id == stubId; });
    if (stub == harness.layoutSegments.end() || distanceMm <= 0)
        return std::nullopt;
    const QString splice = stubSpliceNode(harness, *stub);
    if (splice.isEmpty())
        return std::nullopt;
    QString at = stub->nodeA == splice ? stub->nodeB : stub->nodeA; // X
    const LayoutSegment *came = &*stub;
    double walked = 0;
    QSet<QString> visited{splice};
    while (!visited.contains(at)) {
        visited.insert(at);
        const auto legs = segmentsOn(harness, at);
        if (legs.size() != 2)
            return std::nullopt; // X needs exactly one loom; the walk stops at ends and forks
        const LayoutSegment *next = legs[0] == came ? legs[1] : legs[0];
        if (next->lengthMm <= 0)
            return std::nullopt;
        if (distanceMm < walked + next->lengthMm) {
            if (distanceMm <= walked)
                return std::nullopt; // on a point that is already there
            return LoomPosition{next->id, next->nodeA == at, distanceMm - walked};
        }
        walked += next->lengthMm;
        const QString far = next->nodeA == at ? next->nodeB : next->nodeA;
        const auto farNode = std::find_if(harness.layoutNodes.begin(), harness.layoutNodes.end(),
                                          [&](const LayoutNode &n) { return n.id == far; });
        if (farNode == harness.layoutNodes.end())
            return std::nullopt;
        // Exactly on a splice already in the loom: side by side with it.
        if (std::abs(distanceMm - walked) < 1e-6) {
            if (farNode->type == LayoutNodeType::Splice && !farNode->refId.isEmpty())
                return LoomPosition{{}, true, 0, far};
            return std::nullopt;
        }
        // Only bends and splices inside the loom are walked through, not connectors.
        if (farNode->type == LayoutNodeType::Connector)
            return std::nullopt;
        came = next;
        at = far;
    }
    return std::nullopt;
}

std::unique_ptr<QUndoCommand> setDistanceAlongRun(HarnessModel *model, const QString &nodeId,
                                                  const QString &fromSegmentId, double distanceMm)
{
    std::vector<const LayoutSegment *> run;
    for (const LayoutSegment &s : model->harness().layoutSegments) {
        if (s.nodeA == nodeId || s.nodeB == nodeId)
            run.push_back(&s);
    }
    if (run.size() != 2 || distanceMm <= 0)
        return nullptr;
    if (run[1]->id == fromSegmentId)
        std::swap(run[0], run[1]);
    if (run[0]->id != fromSegmentId)
        return nullptr;
    const double total = std::max(0.0, run[0]->lengthMm) + std::max(0.0, run[1]->lengthMm);
    if (total > 0 && distanceMm >= total)
        return nullptr;
    LayoutSegment from = *run[0];
    LayoutSegment other = *run[1];
    from.lengthMm = distanceMm;
    if (total > 0)
        other.lengthMm = total - distanceMm;
    if (from == *run[0] && other == *run[1])
        return nullptr;
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Set position along bundle"));
    new UpdateEntityCommand<LayoutSegment>(model, std::move(from), {}, macro.get());
    if (other != *run[1])
        new UpdateEntityCommand<LayoutSegment>(model, std::move(other), {}, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> mergeBranchInto(HarnessModel *model, const QString &branchId,
                                              const QString &intoId)
{
    const LayoutNode *branch = model->find<LayoutNode>(branchId);
    if (!branch || branch->type != LayoutNodeType::Branch || branchId == intoId
        || !model->find<LayoutNode>(intoId))
        return nullptr;
    const Harness &h = model->harness();
    QSet<QString> between;
    std::vector<LayoutSegment> reattached;
    for (const LayoutSegment &s : h.layoutSegments) {
        const bool onBranch = s.nodeA == branchId || s.nodeB == branchId;
        if (!onBranch)
            continue;
        if (s.nodeA == intoId || s.nodeB == intoId) {
            between.insert(s.id);
            continue;
        }
        LayoutSegment updated = s;
        (updated.nodeA == branchId ? updated.nodeA : updated.nodeB) = intoId;
        reattached.push_back(updated);
    }
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Join points"));
    for (const Wire &w : h.wires) {
        if (std::any_of(w.routeSegmentIds.begin(), w.routeSegmentIds.end(),
                        [&](const QString &id) { return between.contains(id); })) {
            Wire updated = w;
            updated.routeSegmentIds.clear();
            new UpdateEntityCommand<Wire>(model, updated, {}, macro.get());
        }
    }
    // Remove from the back so recorded indices restore the original order on undo.
    for (auto it = h.layoutSegments.rbegin(); it != h.layoutSegments.rend(); ++it) {
        if (between.contains(it->id))
            new RemoveEntityCommand<LayoutSegment>(model, it->id, {}, macro.get());
    }
    for (LayoutSegment &s : reattached)
        new UpdateEntityCommand<LayoutSegment>(model, std::move(s), {}, macro.get());
    new RemoveEntityCommand<LayoutNode>(model, branchId, {}, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> setWireEnd(HarnessModel *model, const QString &wireId, bool endB,
                                         const WireEnd &newEnd, QPointF freeEndPosition)
{
    const Wire *current = model->find<Wire>(wireId);
    if (!current)
        return nullptr;
    const WireEnd &oldEnd = endB ? current->endB : current->endA;
    const WireEnd &otherEnd = endB ? current->endA : current->endB;
    const bool newIsFree = std::holds_alternative<FreeEnd>(newEnd);
    if (!newIsFree && (newEnd == oldEnd || newEnd == otherEnd))
        return nullptr;

    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Re-attach wire"));
    const QString key = freeEndPlacementKey(wireId, endB);
    const bool oldIsFree = std::holds_alternative<FreeEnd>(oldEnd);
    if (oldIsFree && !newIsFree) {
        clearLayoutRefs(model, {key}, macro.get());
        new SetPlacementsCommand(model, {{key, std::nullopt}}, {}, macro.get());
    }
    Harness after = model->harness();
    Wire &updated = *std::find_if(after.wires.begin(), after.wires.end(),
                                  [&](const Wire &w) { return w.id == wireId; });
    (endB ? updated.endB : updated.endA) = newEnd;
    updated.routeSegmentIds.clear();
    const CircuitTextFill fill = fillEmptyCircuitText(after, {wireId});
    new UpdateEntityCommand<Wire>(model, updated, {}, macro.get());
    addFillUpdates(model, after, fill, macro.get(), {}, wireId);
    if (newIsFree) {
        SchematicPlacement placement;
        if (const SchematicPlacement *old = model->placement(key))
            placement = *old;
        placement.position = freeEndPosition;
        new SetPlacementsCommand(model, {{key, placement}}, {}, macro.get());
    }
    return macro;
}

std::vector<Pin> resizedPins(const std::vector<Pin> &pins, int count)
{
    count = std::clamp(count, kMinPins, kMaxPins);
    std::vector<Pin> result(pins.begin(), pins.begin() + std::min<qsizetype>(count, qsizetype(pins.size())));
    while (qsizetype(result.size()) < count) {
        Pin pin;
        pin.id = newId();
        pin.label = result.empty() ? QStringLiteral("1") : nextPinLabel(result.back().label);
        result.push_back(std::move(pin));
    }
    return result;
}

} // namespace tbh::app
