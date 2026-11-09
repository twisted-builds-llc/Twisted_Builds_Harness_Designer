// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Edits shared by the schematic view and the properties panels. Each asks for
// confirmation when wires would be deleted, then pushes one undo command.

#include "SelectionModel.h"
#include "model/Harness.h"

class QWidget;

namespace tbh::app {

class Document;

// Shows the wires that an action deletes and asks to continue. True if there are no
// such wires or the user agrees.
bool confirmWireRemoval(QWidget *parent, const Harness &harness, const std::vector<QString> &wireIds,
                        const QString &action);

bool changePinCount(QWidget *parent, Document *document, const QString &connectorId, int count);
bool setPins(QWidget *parent, Document *document, const QString &connectorId, std::vector<Pin> pins,
             const QString &undoText);
bool deleteItems(QWidget *parent, Document *document, const Selection &selection);

} // namespace tbh::app
