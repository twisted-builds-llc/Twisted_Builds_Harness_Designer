// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Single home for file-format constants. Change the extension here only.

#include <QLatin1StringView>

namespace tbh::io {

inline constexpr QLatin1StringView kFileExtension{"tbh"}; // without the dot
inline constexpr int kSchemaVersion = 2;

// Container entry names.
inline constexpr QLatin1StringView kManifestEntry{"manifest.json"};
inline constexpr QLatin1StringView kHarnessEntry{"harness.json"};
inline constexpr QLatin1StringView kConnectorsEntry{"connectors.json"};
inline constexpr QLatin1StringView kWiresEntry{"wires.json"};
inline constexpr QLatin1StringView kSplicesEntry{"splices.json"};
inline constexpr QLatin1StringView kFreeEndsEntry{"freeEnds.json"};
inline constexpr QLatin1StringView kLayoutEntry{"layout.json"};
inline constexpr QLatin1StringView kSchematicEntry{"schematic.json"};
inline constexpr QLatin1StringView kLibraryEntry{"library.json"};
inline constexpr QLatin1StringView kBomEntry{"bom.json"};

} // namespace tbh::io
