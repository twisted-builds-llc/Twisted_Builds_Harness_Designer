// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

// Per-user application preferences, persisted in QSettings.
namespace tbh::app::preferences {

// Units given to newly created harnesses. Default: inches.
Units defaultUnits();
void setDefaultUnits(Units units);

// Minutes between autosaves of unsaved changes; 0 turns autosave off. Default: 2.
int autosaveMinutes();
void setAutosaveMinutes(int minutes);

// Extra input that pans the schematic and layout canvases. A middle-button drag always pans.
// Default: middle button only.
enum class PanInput { MiddleDrag, RightDrag, SpaceDrag };
PanInput panInput();
void setPanInput(PanInput input);

// True: the scroll wheel (two-finger scroll on a touchpad) pans the canvas and Ctrl+wheel
// zooms. False: the wheel zooms. Default: false.
bool wheelPans();
void setWheelPans(bool pans);

// True: while circuit text is typed (schematic pin cells, pin table), a list suggests
// circuitSuggestionDefaults() plus the circuit texts and wire names already in the harness.
// Default: false.
bool circuitSuggestions();
void setCircuitSuggestions(bool on);
// The quick choices offered first. Default: 12V+, 5V+, GND, Sensor GND, Unused.
QStringList circuitSuggestionDefaults();
void setCircuitSuggestionDefaults(const QStringList &texts);
// What to suggest for circuit text in `harness`: empty when suggestions are off, else the
// defaults, then the texts used in the harness, each text once (ignoring case).
QStringList circuitSuggestionsFor(const Harness &harness);

} // namespace tbh::app::preferences
