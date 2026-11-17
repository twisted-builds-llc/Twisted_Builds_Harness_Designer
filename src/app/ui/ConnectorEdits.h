// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Edits shared by the schematic view and the properties panels. Each asks for
// confirmation when wires would be deleted, then pushes one undo command.

#include "SelectionModel.h"
#include "model/Harness.h"

#include <QStringList>

#include <utility>
#include <vector>

class QWidget;

namespace tbh::app {

class Document;
class UndoBatch;

// Shows the wires that an action deletes and asks to continue. True if there are no
// such wires or the user agrees.
bool confirmWireRemoval(QWidget *parent, const Harness &harness, const std::vector<QString> &wireIds,
                        const QString &action);

bool changePinCount(QWidget *parent, Document *document, const QString &connectorId, int count);
bool setPins(QWidget *parent, Document *document, const QString &connectorId, std::vector<Pin> pins,
             const QString &undoText);
bool deleteItems(QWidget *parent, Document *document, const Selection &selection);

// Wiring pins by naming their far ends (planPinConnections()): each pin is wired to exactly
// the ends its text names.
using PinConnections = std::vector<std::pair<PinRef, QString>>;
// Checks every name and asks before wires would be removed. False if a name matches nothing
// (those names go to `unknown`) or the user cancels.
bool checkPinConnections(QWidget *parent, Document *document, const PinConnections &connections,
                         QStringList *unknown = nullptr);
// Moves, adds (in the document's wire style, drawn on the connector's sheet) and removes
// wires into `batch`. Call checkPinConnections() first.
void applyPinConnections(Document *document, const PinConnections &connections, UndoBatch &batch);

} // namespace tbh::app
