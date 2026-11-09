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

namespace tbh::app {

// Wires that removeItems() would delete: the given wires plus all wires attached to the
// given connectors and splices.
std::vector<QString> wiresRemovedWith(const Harness &harness, const QSet<QString> &connectorIds,
                                      const QSet<QString> &spliceIds, const QSet<QString> &wireIds);

std::unique_ptr<QUndoCommand> removeItems(HarnessModel *model, const QSet<QString> &connectorIds,
                                          const QSet<QString> &spliceIds,
                                          const QSet<QString> &wireIds);

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
// without text takes the text of the pin at the other end, and an unnamed wire takes it
// as its name. One undo step.
std::unique_ptr<QUndoCommand> addWire(HarnessModel *model, Wire wire, Placements placements,
                                      const QString &text);

// Replaces a connector with `updated`. For pins whose circuit text was changed to a
// non-empty value, empty circuit text at the other end of their wires is filled
// (fillEmptyCircuitText). Returns null if nothing changes.
std::unique_ptr<QUndoCommand> updateConnector(HarnessModel *model, const Connector &updated,
                                              const QString &text);

// Applies several wire updates as one undo step.
std::unique_ptr<QUndoCommand> updateWires(HarnessModel *model, const std::vector<Wire> &wires,
                                          const QString &text);

// Removes layout nodes and segments. Segments on removed nodes go too; wires whose
// manual route used a removed segment fall back to automatic routing.
std::unique_ptr<QUndoCommand> removeLayoutItems(HarnessModel *model, const QSet<QString> &nodeIds,
                                                const QSet<QString> &segmentIds);

// Moves one end of a wire to `newEnd` (re-attach). A new free end is placed at
// `freeEndPosition`. Layout references to an old free end are cleared and any manual
// route is dropped. Empty circuit text is filled across the wire as in addWire().
// Returns null if nothing changes or both ends would be the same pin.
std::unique_ptr<QUndoCommand> setWireEnd(HarnessModel *model, const QString &wireId, bool endB,
                                         const WireEnd &newEnd, QPointF freeEndPosition);

// Pins with `count` entries: existing pins are kept, new pins get nextPinLabel() labels.
std::vector<Pin> resizedPins(const std::vector<Pin> &pins, int count);

} // namespace tbh::app
