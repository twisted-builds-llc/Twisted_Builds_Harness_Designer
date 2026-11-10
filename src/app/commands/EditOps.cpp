// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/EditOps.h"

#include "calc/WireLength.h"
#include "commands/EntityCommands.h"
#include "commands/HarnessCommands.h"
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
                                      const QSet<QString> &spliceIds, const QSet<QString> &freeEndIds,
                                      const QSet<QString> &wireIds)
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
    for (const QString &id : freeEndIds) {
        for (const QString &w : wiresOnFreeEnd(harness, id))
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
                                          const QSet<QString> &freeEndIds,
                                          const QSet<QString> &wireIds,
                                          const QSet<QString> &noteIds)
{
    const Harness &h = model->harness();
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Delete"));
    // Children redo in order and undo in reverse: wires go first and come back last.
    // Within each list, remove from the back so the recorded indices restore the order.
    const std::vector<QString> wires = wiresRemovedWith(h, connectorIds, spliceIds, freeEndIds, wireIds);
    clearLayoutRefs(model, connectorIds + spliceIds + freeEndIds, macro.get());
    for (auto it = wires.rbegin(); it != wires.rend(); ++it)
        new RemoveEntityCommand<Wire>(model, *it, {}, macro.get());
    for (auto it = h.freeEnds.rbegin(); it != h.freeEnds.rend(); ++it) {
        if (freeEndIds.contains(it->id))
            new RemoveEntityCommand<FreeEnd>(model, it->id, {}, macro.get());
    }
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

namespace {

// The placement of a wire's sheet entry with `sheetId` added; a wire without an entry keeps
// the sheet it is drawn on now.
SchematicPlacement wireSheetsWith(const Harness &h, const Wire &wire, const QString &sheetId)
{
    const QString key = Schematic::wireSheetKey(wire.id);
    SchematicPlacement entry;
    if (const auto it = h.schematic.placements.find(key); it != h.schematic.placements.end()) {
        entry = it->second;
    } else {
        const WireSheets now = wireSheets(h, wire);
        entry.sheetId = now.sheets.empty() ? sheetId : now.sheets.front();
    }
    entry.setPositionOn(h.schematic.resolvedSheetId(entry.sheetId), sheetId, {});
    return entry;
}

} // namespace

std::unique_ptr<QUndoCommand> pasteFragment(HarnessModel *model, Harness fragment, QPointF offset,
                                            QSet<QString> *newItemIds, const QString &sheetId)
{
    const Harness &h = model->harness();
    const QString targetSheet = h.schematic.resolvedSheetId(sheetId);
    // Items that still exist under the same name and are not on the target sheet are shown
    // there instead of copied: the same connector or splice on another sheet.
    QSet<QString> reused;
    auto reusable = [&](const QString &id, const QString &label, const QString &existingLabel) {
        return label == existingLabel && !h.schematic.isOnSheet(id, targetSheet);
    };
    for (const Connector &c : fragment.connectors) {
        if (const Connector *existing = model->find<Connector>(c.id); existing && reusable(c.id, c.label, existing->label))
            reused.insert(c.id);
    }
    for (const Splice &sp : fragment.splices) {
        if (const Splice *existing = model->find<Splice>(sp.id); existing && reusable(sp.id, sp.label, existing->label))
            reused.insert(sp.id);
    }
    for (const FreeEnd &f : fragment.freeEnds) {
        if (const FreeEnd *existing = model->find<FreeEnd>(f.id); existing && reusable(f.id, f.label, existing->label))
            reused.insert(f.id);
    }

    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Paste"));
    std::map<QString, std::optional<SchematicPlacement>> shown;
    for (const QString &id : std::as_const(reused)) {
        const auto it = fragment.schematic.placements.find(id);
        const QPointF position = (it != fragment.schematic.placements.end() ? it->second.position : QPointF()) + offset;
        if (const SchematicPlacement *old = model->placement(id)) {
            SchematicPlacement placement = *old;
            placement.setPositionOn(h.schematic.resolvedSheetId(old->sheetId), targetSheet, position);
            shown[id] = placement;
        } else {
            SchematicPlacement placement;
            placement.position = position;
            placement.sheetId = targetSheet;
            shown[id] = placement;
        }
        if (newItemIds)
            newItemIds->insert(id);
    }
    // Wires between reused items that were selected when copied (marked by a wire sheet
    // entry in the fragment) are shown there too.
    Schematic after = h.schematic;
    for (const auto &[key, placement] : shown)
        after.placements[key] = *placement;
    for (const Wire &w : fragment.wires) {
        const Wire *existing = model->find<Wire>(w.id);
        if (!existing || !fragment.schematic.placements.contains(Schematic::wireSheetKey(w.id))
            || !reused.contains(wireEndItemId(w.endA)) || !reused.contains(wireEndItemId(w.endB))
            || !after.isOnSheet(wireEndItemId(existing->endA), targetSheet)
            || !after.isOnSheet(wireEndItemId(existing->endB), targetSheet) || wireSheets(h, *existing).drawnOn(targetSheet))
            continue;
        shown[Schematic::wireSheetKey(w.id)] = wireSheetsWith(h, *existing, targetSheet);
    }
    if (!shown.empty())
        new SetPlacementsCommand(model, shown, {}, macro.get());
    // The rest is a copy. Wires touching a reused item exist already.
    std::erase_if(fragment.connectors, [&](const Connector &c) { return reused.contains(c.id); });
    std::erase_if(fragment.splices, [&](const Splice &sp) { return reused.contains(sp.id); });
    std::erase_if(fragment.freeEnds, [&](const FreeEnd &f) { return reused.contains(f.id); });
    std::erase_if(fragment.wires, [&](const Wire &w) {
        return reused.contains(wireEndItemId(w.endA)) || reused.contains(wireEndItemId(w.endB));
    });

    assignNewIds(fragment);
    for (auto &[key, placement] : fragment.schematic.placements) {
        placement.position += offset;
        const bool wireRoute = std::any_of(fragment.wires.begin(), fragment.wires.end(),
                                           [&](const Wire &w) { return w.id == key; });
        placement.sheetId = wireRoute ? QString() : targetSheet;
        placement.alsoOn.clear();
    }
    for (const Wire &w : fragment.wires) {
        SchematicPlacement sheet;
        sheet.sheetId = targetSheet;
        fragment.schematic.placements[Schematic::wireSheetKey(w.id)] = sheet;
    }
    auto placementsOf = [&](EntityKind kind, const QString &id) {
        Placements result;
        for (const QString &key : HarnessModel::placementKeys(kind, id)) {
            const auto it = fragment.schematic.placements.find(key);
            if (it != fragment.schematic.placements.end())
                result[key] = it->second;
        }
        return result;
    };

    for (Connector &c : fragment.connectors) {
        if (newItemIds)
            newItemIds->insert(c.id);
        Placements p = placementsOf(EntityKind::Connector, c.id);
        new AddEntityCommand<Connector>(model, std::move(c), std::move(p), {}, macro.get());
    }
    for (Splice &sp : fragment.splices) {
        if (newItemIds)
            newItemIds->insert(sp.id);
        Placements p = placementsOf(EntityKind::Splice, sp.id);
        new AddEntityCommand<Splice>(model, std::move(sp), std::move(p), {}, macro.get());
    }
    for (FreeEnd &f : fragment.freeEnds) {
        if (newItemIds)
            newItemIds->insert(f.id);
        Placements p = placementsOf(EntityKind::FreeEnd, f.id);
        new AddEntityCommand<FreeEnd>(model, std::move(f), std::move(p), {}, macro.get());
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

QString stubOfSplice(const Harness &harness, const QString &spliceNodeId)
{
    const std::vector<const LayoutSegment *> legs = segmentsOn(harness, spliceNodeId);
    if (legs.size() != 1 || stubSpliceNode(harness, *legs.front()) != spliceNodeId)
        return {};
    const QString far = legs.front()->nodeA == spliceNodeId ? legs.front()->nodeB : legs.front()->nodeA;
    return segmentsOn(harness, far).size() > 1 ? legs.front()->id : QString();
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
    // The splice's own wires (those of every splice sitting at its point) decide which loom
    // to follow where several leave a point.
    const auto spliceNode = std::find_if(harness.layoutNodes.begin(), harness.layoutNodes.end(),
                                         [&](const LayoutNode &n) { return n.id == splice; });
    QSet<QString> spliceWires;
    std::vector<QString> spliceIds{spliceNode->refId};
    spliceIds.insert(spliceIds.end(), spliceNode->sharedSpliceIds.begin(), spliceNode->sharedSpliceIds.end());
    for (const QString &id : spliceIds) {
        for (const QString &wireId : wiresOnSplice(harness, id))
            spliceWires.insert(wireId);
    }
    std::optional<QHash<QString, std::vector<QString>>> usage; // built only at a fork
    auto spliceWiresIn = [&](const LayoutSegment *segment) {
        if (!usage)
            usage = calc::wiresBySegment(harness, calc::LayoutGraph(harness));
        const std::vector<QString> through = usage->value(segment->id);
        return std::count_if(through.begin(), through.end(),
                             [&](const QString &wireId) { return spliceWires.contains(wireId); });
    };

    QString at = stub->nodeA == splice ? stub->nodeB : stub->nodeA; // X
    const LayoutSegment *came = &*stub;
    double walked = 0;
    QSet<QString> visited{splice};
    while (!visited.contains(at)) {
        visited.insert(at);
        std::vector<const LayoutSegment *> onward;
        for (const LayoutSegment *leg : segmentsOn(harness, at)) {
            if (leg != came)
                onward.push_back(leg);
        }
        if (onward.empty())
            return std::nullopt; // the loom ends here
        const LayoutSegment *next = onward.front();
        if (onward.size() > 1) {
            // A fork: follow the loom most of the splice's wires run through; a tie (or none)
            // leaves the choice open, so nothing is placed.
            long best = 0;
            next = nullptr;
            bool tie = false;
            for (const LayoutSegment *leg : onward) {
                const long count = spliceWiresIn(leg);
                if (count > best) {
                    best = count;
                    next = leg;
                    tie = false;
                } else if (count == best) {
                    tie = true;
                }
            }
            if (!next || tie)
                return std::nullopt;
        }
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
        // Exactly on a point inside the loom: side by side with the splices there, or in
        // place of a bend or fork.
        if (std::abs(distanceMm - walked) < 1e-6) {
            if ((farNode->type == LayoutNodeType::Splice && !farNode->refId.isEmpty())
                || farNode->type == LayoutNodeType::Branch)
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
                                         const WireEnd &newEnd)
{
    const Wire *current = model->find<Wire>(wireId);
    if (!current)
        return nullptr;
    const WireEnd &oldEnd = endB ? current->endB : current->endA;
    const WireEnd &otherEnd = endB ? current->endA : current->endB;
    if (newEnd == oldEnd || newEnd == otherEnd)
        return nullptr;

    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Re-attach wire"));
    Harness after = model->harness();
    Wire &updated = *std::find_if(after.wires.begin(), after.wires.end(),
                                  [&](const Wire &w) { return w.id == wireId; });
    (endB ? updated.endB : updated.endA) = newEnd;
    updated.routeSegmentIds.clear();
    const CircuitTextFill fill = fillEmptyCircuitText(after, {wireId});
    new UpdateEntityCommand<Wire>(model, updated, {}, macro.get());
    addFillUpdates(model, after, fill, macro.get(), {}, wireId);
    return macro;
}

std::unique_ptr<QUndoCommand> updateFreeEnd(HarnessModel *model, const FreeEnd &updated, const QString &text)
{
    const FreeEnd *current = model->find<FreeEnd>(updated.id);
    if (!current || *current == updated)
        return nullptr;
    auto macro = std::make_unique<QUndoCommand>(text);
    new UpdateEntityCommand<FreeEnd>(model, updated, {}, macro.get());
    // Its layout node draws the new kind (an eyelet ring or a flying lead).
    const LayoutNodeType type = layoutNodeTypeFor(updated.kind);
    for (const LayoutNode &n : model->harness().layoutNodes) {
        if (n.refId == updated.id && n.type != type) {
            LayoutNode node = n;
            node.type = type;
            new UpdateEntityCommand<LayoutNode>(model, node, {}, macro.get());
        }
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

// ---- Schematic sheets ----------------------------------------------------------

namespace {

bool isSheetItem(const HarnessModel *model, const QString &key)
{
    return model->find<Connector>(key) || model->find<Splice>(key) || model->find<FreeEnd>(key);
}

bool isWireSheetEntry(const HarnessModel *model, const QString &key)
{
    static const QString suffix = Schematic::wireSheetKey({});
    return key.endsWith(suffix) && model->find<Wire>(key.chopped(suffix.size()));
}

// Child commands that put every sheet item (and wire's own sheet) on `sheetFor(sheet)` for
// each sheet it is on once the list is `newSheets`, then set the list. Sheet ids are only
// rewritten where the new list would resolve them differently.
template<typename SheetFor>
void addSheetChange(HarnessModel *model, std::vector<SchematicSheet> newSheets, SheetFor sheetFor,
                    QUndoCommand *parent)
{
    const Schematic &old = model->harness().schematic;
    Schematic after;
    after.sheets = newSheets;
    // `current` as stored, `wanted` as resolved: keep `current` when it still means `wanted`.
    auto stored = [&](const QString &current, const QString &wanted) {
        const bool known = current.isEmpty()
                           || std::any_of(newSheets.begin(), newSheets.end(),
                                          [&](const SchematicSheet &s) { return s.id == current; });
        return known && after.resolvedSheetId(current) == wanted ? current : wanted;
    };
    std::map<QString, std::optional<SchematicPlacement>> placements;
    for (const auto &[key, placement] : old.placements) {
        SchematicPlacement moved = placement;
        // Items, wire routes (kept per sheet) and the sheets a wire is drawn on.
        if (isSheetItem(model, key) || isWireSheetEntry(model, key) || model->find<Wire>(key)) {
            std::vector<std::pair<QString, QPointF>> positions;
            for (const auto &[sheet, pos] : placement.sheetPositions(old.resolvedSheetId(placement.sheetId))) {
                const QString target = sheetFor(sheet);
                if (std::none_of(positions.begin(), positions.end(), [&](const auto &p) { return p.first == target; }))
                    positions.emplace_back(target, pos);
            }
            moved.setSheetPositions(positions);
            moved.sheetId = stored(placement.sheetId, moved.sheetId);
        } else {
            continue;
        }
        if (!(moved == placement))
            placements[key] = moved;
    }
    if (!placements.empty())
        new SetPlacementsCommand(model, placements, {}, parent);
    for (const Note &note : model->harness().notes) {
        if (note.canvas != NoteCanvas::Schematic)
            continue;
        const QString id = stored(note.sheetId, sheetFor(old.resolvedSheetId(note.sheetId)));
        if (id != note.sheetId) {
            Note moved = note;
            moved.sheetId = id;
            new UpdateEntityCommand<Note>(model, std::move(moved), {}, parent);
        }
    }
    new SetSheetsCommand(model, std::move(newSheets), {}, parent);
}

QString unchanged(const QString &sheetId)
{
    return sheetId;
}

bool hasSheet(const Schematic &schematic, const QString &sheetId)
{
    return std::any_of(schematic.sheets.begin(), schematic.sheets.end(),
                       [&](const SchematicSheet &s) { return s.id == sheetId; });
}

} // namespace

std::unique_ptr<QUndoCommand> addSheet(HarnessModel *model, QString *newSheetId)
{
    std::vector<SchematicSheet> sheets = model->harness().schematic.sheets;
    sheets.push_back(SchematicSheet{newId(), {}, {}});
    if (newSheetId)
        *newSheetId = sheets.back().id;
    return std::make_unique<SetSheetsCommand>(model, std::move(sheets), Tr::tr("Add sheet"));
}

std::unique_ptr<QUndoCommand> renameSheet(HarnessModel *model, const QString &sheetId, const QString &name)
{
    std::vector<SchematicSheet> sheets = model->harness().schematic.sheets;
    const auto it = std::find_if(sheets.begin(), sheets.end(), [&](const SchematicSheet &s) { return s.id == sheetId; });
    if (it == sheets.end() || it->name == name.trimmed())
        return nullptr;
    it->name = name.trimmed();
    return std::make_unique<SetSheetsCommand>(model, std::move(sheets), Tr::tr("Rename sheet"));
}

std::unique_ptr<QUndoCommand> removeSheet(HarnessModel *model, const QString &sheetId)
{
    std::vector<SchematicSheet> sheets = model->harness().schematic.sheets;
    const auto it = std::find_if(sheets.begin(), sheets.end(), [&](const SchematicSheet &s) { return s.id == sheetId; });
    if (it == sheets.end() || sheets.size() < 2)
        return nullptr;
    const QString target = it == sheets.begin() ? std::next(it)->id : std::prev(it)->id;
    sheets.erase(it);
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Delete sheet"));
    addSheetChange(model, std::move(sheets),
                   [&](const QString &current) { return current == sheetId ? target : current; }, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> moveSheet(HarnessModel *model, const QString &sheetId, int toIndex)
{
    std::vector<SchematicSheet> sheets = model->harness().schematic.sheets;
    const auto it = std::find_if(sheets.begin(), sheets.end(), [&](const SchematicSheet &s) { return s.id == sheetId; });
    if (it == sheets.end())
        return nullptr;
    const int from = int(it - sheets.begin());
    const int to = std::clamp(toIndex, 0, int(sheets.size()) - 1);
    if (from == to)
        return nullptr;
    const SchematicSheet sheet = *it;
    sheets.erase(it);
    sheets.insert(sheets.begin() + to, sheet);
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Move sheet"));
    addSheetChange(model, std::move(sheets), unchanged, macro.get());
    return macro;
}

std::unique_ptr<QUndoCommand> moveToSheet(HarnessModel *model, const QSet<QString> &placementKeys,
                                          const QSet<QString> &noteIds, const QString &fromSheetId,
                                          const QString &toSheetId)
{
    const Schematic &schematic = model->harness().schematic;
    const QString from = schematic.resolvedSheetId(fromSheetId);
    if (!hasSheet(schematic, toSheetId) || from == toSheetId)
        return nullptr;
    auto macro = std::make_unique<QUndoCommand>(Tr::tr("Move to sheet"));
    Schematic after = schematic;
    std::map<QString, std::optional<SchematicPlacement>> placements;
    for (const QString &key : placementKeys) {
        if (!isSheetItem(model, key) || !schematic.isOnSheet(key, from))
            continue;
        const SchematicPlacement *old = model->placement(key);
        SchematicPlacement moved = old ? *old : SchematicPlacement{};
        std::vector<std::pair<QString, QPointF>> positions;
        const bool alreadyThere = schematic.isOnSheet(key, toSheetId);
        for (const auto &[sheet, pos] : moved.sheetPositions(schematic.resolvedSheetId(moved.sheetId))) {
            if (sheet != from)
                positions.emplace_back(sheet, pos);
            else if (!alreadyThere)
                positions.emplace_back(toSheetId, pos); // keeps its place in the list (home stays home)
        }
        moved.setSheetPositions(positions);
        placements[key] = moved;
        after.placements[key] = moved;
    }
    // Wires drawn on the old sheet move along when both ends are now on the new one.
    for (const Wire &w : model->harness().wires) {
        const QString key = Schematic::wireSheetKey(w.id);
        const SchematicPlacement *own = model->placement(key);
        if (!own || !schematic.isOnSheet(key, from) || !after.isOnSheet(wireEndItemId(w.endA), toSheetId)
            || !after.isOnSheet(wireEndItemId(w.endB), toSheetId))
            continue;
        std::vector<std::pair<QString, QPointF>> sheets;
        for (const auto &[sheet, pos] : own->sheetPositions(schematic.resolvedSheetId(own->sheetId))) {
            const QString target = sheet == from ? toSheetId : sheet;
            if (std::none_of(sheets.begin(), sheets.end(), [&](const auto &p) { return p.first == target; }))
                sheets.emplace_back(target, pos);
        }
        SchematicPlacement moved = *own;
        moved.setSheetPositions(sheets);
        placements[key] = moved;
    }
    if (!placements.empty())
        new SetPlacementsCommand(model, placements, {}, macro.get());
    for (const QString &id : noteIds) {
        const Note *note = model->find<Note>(id);
        if (!note || note->canvas != NoteCanvas::Schematic || schematic.resolvedSheetId(note->sheetId) != from)
            continue;
        Note moved = *note;
        moved.sheetId = toSheetId;
        new UpdateEntityCommand<Note>(model, std::move(moved), {}, macro.get());
    }
    if (macro->childCount() == 0)
        return nullptr;
    return macro;
}


std::unique_ptr<QUndoCommand> showOnSheet(HarnessModel *model, const std::map<QString, QPointF> &positions,
                                          const QSet<QString> &wireIds, const QString &sheetId)
{
    const Harness &h = model->harness();
    const Schematic &schematic = h.schematic;
    if (!hasSheet(schematic, sheetId))
        return nullptr;
    std::map<QString, std::optional<SchematicPlacement>> placements;
    Schematic after = schematic;
    for (const auto &[key, position] : positions) {
        if (!isSheetItem(model, key) || schematic.isOnSheet(key, sheetId))
            continue;
        SchematicPlacement placement;
        if (const SchematicPlacement *old = model->placement(key)) {
            placement = *old;
            placement.setPositionOn(schematic.resolvedSheetId(old->sheetId), sheetId, position);
        } else {
            placement.position = position;
            placement.sheetId = sheetId;
        }
        placements[key] = placement;
        after.placements[key] = placement;
    }
    // Wires too, where both their ends are shown there (now or by this command).
    for (const Wire &w : h.wires) {
        if (!wireIds.contains(w.id) || wireSheets(h, w).drawnOn(sheetId)
            || !after.isOnSheet(wireEndItemId(w.endA), sheetId) || !after.isOnSheet(wireEndItemId(w.endB), sheetId))
            continue;
        placements[Schematic::wireSheetKey(w.id)] = wireSheetsWith(h, w, sheetId);
    }
    if (placements.empty())
        return nullptr;
    return std::make_unique<SetPlacementsCommand>(model, placements, Tr::tr("Show on sheet"));
}

std::unique_ptr<QUndoCommand> removeFromSheet(HarnessModel *model, const QSet<QString> &placementKeys,
                                              const QSet<QString> &wireIds, const QString &sheetId)
{
    const Harness &h = model->harness();
    const Schematic &schematic = h.schematic;
    const QString sheet = schematic.resolvedSheetId(sheetId);
    std::map<QString, std::optional<SchematicPlacement>> placements;
    for (const QString &key : placementKeys) {
        const SchematicPlacement *old = model->placement(key);
        if (!old || !isSheetItem(model, key) || !schematic.isOnSheet(key, sheet) || schematic.sheetsOf(key).size() < 2)
            continue;
        std::vector<std::pair<QString, QPointF>> positions = old->sheetPositions(schematic.resolvedSheetId(old->sheetId));
        std::erase_if(positions, [&](const auto &p) { return p.first == sheet; });
        SchematicPlacement placement = *old;
        placement.setSheetPositions(positions);
        placements[key] = placement;
    }
    for (const Wire &w : h.wires) {
        const QString key = Schematic::wireSheetKey(w.id);
        const SchematicPlacement *old = model->placement(key);
        if (!wireIds.contains(w.id) || !old || wireSheets(h, w).sheets.size() < 2 || !schematic.isOnSheet(key, sheet))
            continue;
        std::vector<std::pair<QString, QPointF>> sheets = old->sheetPositions(schematic.resolvedSheetId(old->sheetId));
        std::erase_if(sheets, [&](const auto &p) { return p.first == sheet; });
        SchematicPlacement placement = *old;
        placement.setSheetPositions(sheets); // not empty: drawn on two or more of its sheets
        placements[key] = placement;
    }
    if (placements.empty())
        return nullptr;
    return std::make_unique<SetPlacementsCommand>(model, placements, Tr::tr("Remove from sheet"));
}

} // namespace tbh::app
