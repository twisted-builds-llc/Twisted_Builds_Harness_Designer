// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QCoreApplication;

namespace tbh::app {

// Installs translators for the system language: Qt's own strings (standard buttons and
// dialogs) when available, and the app's strings from :/i18n. English is the source
// language; its catalog only supplies plural forms ("1 wire" / "2 wires") and is used
// whenever no catalog exists for the system language.
void installTranslators(QCoreApplication &app);

} // namespace tbh::app
