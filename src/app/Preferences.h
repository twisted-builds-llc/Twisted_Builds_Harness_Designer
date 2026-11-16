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

} // namespace tbh::app::preferences
