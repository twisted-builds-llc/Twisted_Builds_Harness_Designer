// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Builders for compound edits that must keep the model consistent. Each returns one
// undo command (a macro with children where needed), ready to push.

#include "commands/EntityCommands.h"
#include "model/HarnessModel.h"

#include <QPointF>
#include <QSet>
#include <QUndoCommand>

#include <memory>
#include <optional>

namespace tbh::app {

// Wires that removeItems() would delete: the given wires plus all wires attached to the
// given connectors, splices and free ends.
std::vector<QString> wiresRemovedWith(const Harness &harness, const QSet<QString> &connectorIds,
                                      const QSet<QString> &spliceIds, const QSet<QString> &freeEndIds,
                                      const QSet<QString> &wireIds);

// Also removes the notes in `noteIds`. Layout nodes of removed items stay, unassigned.
std::unique_ptr<QUndoCommand> removeItems(HarnessModel *model, const QSet<QString> &connectorIds,
                                          const QSet<QString> &spliceIds,
                                          const QSet<QString> &freeEndIds,
                                          const QSet<QString> &wireIds,
                                          const QSet<QString> &noteIds = {});

// Wires attached to pins of the connector that are absent from `newPins`.
std::vector<QString> wiresLostByPinChange(const Harness &harness, const Connector &connector,
                                          const std::vector<Pin> &newPins);

// Replaces the connector's pins, deleting wires on pins that disappear.
std::unique_ptr<QUndoCommand> setConnectorPins(HarnessModel *model, const QString &connectorId,
                                               std::vector<Pin> newPins, const QString &text);

// Inserts a fragment (see model/Fragment.h) with fresh ids, shifted by `offset`.
// Returns the new connector and splice ids through the out-parameter.
std::unique_ptr<QUndoCommand> pasteFragment(HarnessModel *model, Harness fragment, QPointF offset,
                                            QSet<QString> *newItemIds);

// Adds a wire. Empty circuit text is filled across it (fillEmptyCircuitText): a pin
// without text takes the text of the pin at the other end (through splices), and an
// unnamed wire takes it as its name. One undo step.
std::unique_ptr<QUndoCommand> addWire(HarnessModel *model, Wire wire, Placements placements,
                                      const QString &text);

// Replaces a connector with `updated`. For pins whose circuit text was changed to a
// non-empty value, empty circuit text at the other end of their wires, and through
// splices, is filled (fillEmptyCircuitText). Returns null if nothing changes.
std::unique_ptr<QUndoCommand> updateConnector(HarnessModel *model, const Connector &updated,
                                              const QString &text);

// Applies several wire updates as one undo step.
std::unique_ptr<QUndoCommand> updateWires(HarnessModel *model, const std::vector<Wire> &wires,
                                          const QString &text);

// Removes layout nodes, segments and notes. Segments on removed nodes go too; wires whose
// manual route used a removed segment fall back to automatic routing.
std::unique_ptr<QUndoCommand> removeLayoutItems(HarnessModel *model, const QSet<QString> &nodeIds,
                                                const QSet<QString> &segmentIds,
                                                const QSet<QString> &noteIds = {});

// Sets a note's text (trimmed). Empty text deletes the note. Returns null if the note is
// missing or nothing changes.
std::unique_ptr<QUndoCommand> setNoteText(HarnessModel *model, const QString &noteId, const QString &text);

// Joins branch point `branchId` into node `intoId`: its segments are re-attached to
// `intoId`, segments between the two are removed (manual routes through them fall back to
// automatic), and the branch point is deleted. Returns null unless `branchId` is a branch
// point and `intoId` another existing node.
std::unique_ptr<QUndoCommand> mergeBranchInto(HarnessModel *model, const QString &branchId,
                                              const QString &intoId);

// Puts node `nodeId`, which has no segments yet, inside segment `segmentId` (e.g. a splice
// in the middle of a sleeved run): the segment keeps its id and now runs nodeA-node, and a
// new segment runs node-nodeB. Both keep the label and sleeving. The entered length is
// split at `fraction` (0..1 from nodeA), and each half gets its drawing bend. Manual routes
// through the segment fall back to automatic. Returns null unless the segment exists and
// the node exists with no segments; the new segment's id goes to `newSegmentId`.
std::unique_ptr<QUndoCommand> splitSegmentAt(HarnessModel *model, const QString &segmentId,
                                             const QString &nodeId, double fraction,
                                             std::optional<QPointF> bendA,
                                             std::optional<QPointF> bendB,
                                             QString *newSegmentId = nullptr);

// Where along the loom a splice drawn as a stub lands. `stubId` runs from a splice node
// that has no other segment to a node X. The splice falls `distanceMm` from X, walking the
// loom out of X: where several segments leave a point (X itself, or a fork further on), the
// walk follows the one most of the splice's wires are routed through (none or a tie: no
// position). The walk passes bends, forks and splices already inside, not connectors.
// It lands in `segmentId`, `offsetMm` from its end nearer X (`fromNodeA` says which), or exactly on splice point `atNodeId`, where it sits
// side by side with the splices there (segmentId then empty). Empty when the stub does not
// qualify or the distance is not inside the walkable loom.
struct LoomPosition
{
    QString segmentId;
    bool fromNodeA = true;
    double offsetMm = 0;
    QString atNodeId;
};
std::optional<LoomPosition> loomPositionForStub(const Harness &harness, const QString &stubId,
                                                double distanceMm);
// The splice node whose only segment is `stubId`, or empty (whether or not the loom fits a
// distance).
QString spliceOfStub(const Harness &harness, const QString &stubId);

// Moves node `nodeId` along the run through it without changing the run's total length:
// the node must have exactly two segments; `fromSegmentId` (one of them) is set to
// `distanceMm` and the other to the rest. When the run has no length yet only
// `fromSegmentId` is set. Returns null if nothing changes, the node is not on exactly two
// segments, or `distanceMm` is not inside the run.
std::unique_ptr<QUndoCommand> setDistanceAlongRun(HarnessModel *model, const QString &nodeId,
                                                  const QString &fromSegmentId, double distanceMm);

// Moves one end of a wire to `newEnd` (re-attach); any manual route is dropped. The old
// end's item (e.g. a free end) stays. Empty circuit text is filled across the wire as in
// addWire(). Returns null if nothing changes or both ends would be the same item.
std::unique_ptr<QUndoCommand> setWireEnd(HarnessModel *model, const QString &wireId, bool endB,
                                         const WireEnd &newEnd);

// Replaces a free end with `updated`. A kind change also changes the type of its layout
// node (eyelet or flying lead). Returns null if nothing changes.
std::unique_ptr<QUndoCommand> updateFreeEnd(HarnessModel *model, const FreeEnd &updated, const QString &text);

// Pins with `count` entries: existing pins are kept, new pins get nextPinLabel() labels.
std::vector<Pin> resizedPins(const std::vector<Pin> &pins, int count);

} // namespace tbh::app
