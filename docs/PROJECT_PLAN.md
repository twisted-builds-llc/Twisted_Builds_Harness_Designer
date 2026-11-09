# Twisted Builds Harness Designer

Free, open-source (GPLv3), fully local wiring harness design software for Linux, Windows and macOS.
Built with Qt 6 + C++ (Widgets). No cloud, no accounts, no telemetry.

> **Handoff note for Claude CLI:** This file is the source of truth for scope and architecture.
> Read it fully before writing code. Work one milestone at a time (see "Milestones"), on its own
> branch, with tests. Ask before changing anything under "Decisions already made".

---

## 1. Purpose

A free alternative to the paid browser tool harness.design. The user designs a wiring harness in
three linked views that share one data model:

1. **Schematic** - connectors, pins, circuits, wire color/stripe/gauge.
2. **Layout** - physical routing: branch points, connectors, flying leads, eyelets/ring terminals,
   branch lengths. Drives wire-length and bundle-diameter calculations.
3. **BOM** - wire lengths, sleeving, connectors, terminals, seals, etc.

Businesses will use this for private designs, so everything stays on the local machine. There must
be **no network code at all** in the application (no update checks, no analytics, no crash upload).

## 2. Decisions already made

- Name: **Twisted Builds Harness Designer** (working repo name: `twisted-harness-designer`)
- License: **GPLv3-or-later** (forks stay open). Put `LICENSE` in the repo root and an
  `// SPDX-License-Identifier: GPL-3.0-or-later` header in every source file.
- Toolkit: **Qt 6, C++20, CMake, Qt Widgets** (QGraphicsView/QGraphicsScene for the editors).
  Modern look comes from a custom theme (dark + light), not from QML.
- Version control: GitHub. GitHub Actions CI on Linux, Windows, macOS is deferred to M6
  (added together with the installer/release workflows); until then, test locally.
- File extension: **`.tbh`** (default; easy to change in one constant - see section 6).
- Develop in Qt Creator (CMake project). Qt Designer `.ui` files are allowed for dialogs and
  panels; the canvas editors are code.

## 3. Non-goals (for v1)

- No cloud sync, accounts, sharing service or collaboration.
- No automatic wire routing or DRC beyond simple sanity warnings.
- No multi-conductor cables / twisted pairs / shielded cable (planned v2; keep the data model
  extensible, see section 5.6).
- No connector 3D models.

## 4. Architecture

```
twisted-harness-designer/
  CMakeLists.txt
  LICENSE                      (GPLv3)
  README.md
  CLAUDE.md                    (copy of section 12 of this file)
  docs/
    PROJECT_PLAN.md            (this file)
    file-format.md
  src/
    core/                      (static lib: tbh_core, links Qt6::Core ONLY - no Gui/Widgets)
      model/                   (Harness, Connector, Pin, Wire, Splice, LayoutNode, LayoutSegment...)
      calc/                    (wire length, bundle diameter, sleeving selection, highlight sets)
      bom/                     (BOM generation, CSV export)
      io/                      (.tbh read/write, schema versioning + migrations)
      library/                 (wire types, sleeving, connector/terminal libraries)
    app/                       (executable: links tbh_core + Qt6::Widgets)
      ui/                      (main window, docks, dialogs, theme)
      schematic/               (QGraphicsScene items + tools for the schematic editor)
      layout/                  (QGraphicsScene items + tools for the layout editor)
      bom/                     (BOM table view + export dialogs)
      commands/                (QUndoCommand subclasses)
  resources/
    themes/ icons/ default-library/   (JSON seed libraries)
  tests/
    core/                      (Qt Test, run through CTest)
    app/
  .github/workflows/           (ci.yml, release.yml)
```

Rules that follow from this:

- **`tbh_core` must not depend on Qt Gui/Widgets.** All math, model, BOM and file I/O live there
  and are unit-tested headless.
- **One model, many views.** Scenes never own data. They render the model and issue
  `QUndoCommand`s that mutate it. The model emits signals; views update from signals.
- **Every user edit is a QUndoCommand** from the first milestone. No exceptions.
- **Selection/highlight state** is a shared `SelectionModel` so a click in either editor drives
  both views.
- Pin a Qt version (Qt 6.8 LTS or newer) in CMake and CI. Use `find_package(Qt6 ...)`.

## 5. Data model

All entities have a stable `id` (UUID string). References between entities use ids, never pointers
or indexes, so files survive reordering.

### 5.1 Harness (root)
- `name`, `description`, `revision`, `author`, `units` (`in` | `mm` | `cm`; display only)
- `settings`: `packingFactor` (default 0.70), `serviceLoopPercent` (default 0),
  `stripAllowance` per end type, `wasteFactorWire` (default 5%), `wasteFactorSleeving` (default 5%),
  `lengthRoundingIncrement` (default 1 mm), `sleevingDiameterMargin` (default 10%)

### 5.2 Connector
- `label` (e.g. "ECU-A"), `partNumber`, `manufacturer`, `description`, `gender` (male/female/none),
  `notes`
- `pins[]` (ordered). **1 to 999 pins.**
- Pin: `id`, `label` (**free-form string**: `1`, `A1`, `B12`, `+`, `GND`, anything),
  `circuitText` (the text the user types: what the circuit is), `terminalPartNumber`,
  `sealPartNumber`, `isCavityPlug` (bool, for unused cavities fitted with a plug), `notes`

### 5.3 Wire (a circuit)
- `id`, `name`/`circuitText`, `endA`, `endB`
- Wire end: one of
  - `PinRef{connectorId, pinId}`
  - `SpliceRef{spliceId}`
  - `FreeEnd{kind: flyingLead | eyelet | ringTerminal | bare, terminalPartNumber?, label?}`
- `baseColor`, `stripeColor` (optional), `gauge` (value + system AWG|mm2), `wireTypeId`
- `lengthOverride` (optional, manual)
- A pin may have more than one wire (e.g. splices, shared grounds); warn, do not forbid.

### 5.4 Splice
- `label`, `partNumber` (optional). A node where several wire ends meet.

### 5.5 Layout graph
- `LayoutNode`: `id`, `type` (`connector` | `branch` | `splice` | `flyingLead` | `eyelet`),
  `refId` (connector/splice id when applicable), `position` (x, y)
- `LayoutSegment`: `id`, `nodeA`, `nodeB`, `length` (in harness units), `label`,
  `sleevingId` (optional), `sleevingCoverage` (full or start/end offsets - v1: full segment only)
- Positions are for drawing only. **Lengths are entered values, not derived from drawn distance**
  (user types the branch length; the drawing is schematic-to-scale-ish, not authoritative).
- A wire's physical route = path in the layout graph between the layout nodes of its two ends.
  Layouts are normally trees, so the path is unique; if a cycle exists use the shortest path and
  allow a manual route override per wire (`routeSegmentIds[]`).

### 5.6 Extensibility (do now, implement later)
Wires reference a `conductorKind` that is `single` in v1. Leave room for `twistedPair`,
`shielded`, `multiConductorCable` later without a schema break (schema is versioned, see section 6).

### 5.7 Libraries (user-editable JSON, stored per-user and optionally embedded in the file)
- **Wire types**: id, name (e.g. "GXL", "TXL"), per-gauge **outer diameter** table
- **Sleeving catalog**: id, name/type (braided, split loom, heat shrink...), size label,
  nominal inner diameter (and fully-recovered diameter for heat shrink)
- **Connectors / terminals / seals**: part numbers and descriptions for autofill
  (not built yet: M4 delivered wire types and sleeving; autofill is still open)
- Seed values shipped in `resources/default-library/` are **approximate defaults and must be
  labeled as such in the UI**; users are expected to verify against manufacturer datasheets.
  Do not present seeded numbers as authoritative.
- When a file is saved, embed the library entries it uses so it opens correctly on another machine.

## 6. File format

- Extension constant lives in one place (`core/io/FileFormat.h`).
- A `.tbh` file is a **zip container** holding JSON documents (use **miniz** via CMake
  FetchContent, or libzip - no network at runtime, only at build time):
  - `manifest.json` - `schemaVersion`, app version, timestamps
  - `harness.json` - metadata + settings
  - `connectors.json`, `wires.json`, `splices.json`
  - `schematic.json` - positions/sizes of schematic items
  - `layout.json` - nodes + segments
  - `bom.json` - BOM overrides/notes (BOM itself is regenerated from the model)
  - `library.json` - embedded library entries used by this harness
- Requirements: human-readable, stable key order (git-diff friendly), `schemaVersion` with a
  migration chain (`migrate_v1_to_v2`...), atomic save (write temp file, then rename), tolerate and
  preserve unknown keys where reasonable.
- Document the format in `docs/file-format.md` and keep it current.

## 7. Feature specs

### 7.1 Schematic editor
- **Connector box**
  - Starts with **1 pin row**. A header control: `[ - ]  N  [ + ]` (negative button left, count in
    the middle, positive button right). Count is also directly editable by typing. Range **1-999**.
  - Each row: pin label (editable, free text) + circuit text box (editable).
  - Adding pins appends rows with auto-incremented labels where the previous label ends in a
    number (`A8` -> `A9`), otherwise blank. A **"Pin label pattern..."** dialog fills labels in bulk
    (e.g. `A1-A8, B1-B8, C1-C12`; also plain ranges `1-120`, and reverse/skip options).
  - Removing pins that have wires attached must **confirm** and list the affected wires.
  - Connector-level fields: label, part number, manufacturer, gender, notes.
  - **Performance:** connectors with hundreds of pins must stay smooth. Do not create a heavy
    child graphics item per pin. Draw rows in the connector item's `paint()`, only for visible rows,
    keep a model-driven row geometry, add collapse/expand and a scroll or fixed-height mode for
    very large connectors, and use cached rendering where it helps. Target: 999-pin connector
    pans/zooms at interactive rates on mid-range hardware.
- **Wires**
  - Draw by dragging from a pin to another pin, a splice, or empty space (creates a flying lead).
  - Render wires as routed lines (orthogonal or smooth - pick one and keep it consistent) between
    pin anchors. Anchors move with rows when connectors are collapsed/scrolled.
  - **Color:** palette of standard wire color names plus custom hex. **Stripe color** is optional
    and independently adjustable. Render stripes visibly (base-color pen with a dashed stripe-color
    overlay pen) so striped wires are recognizable at normal zoom.
  - **Gauge:** AWG and mm2 supported; per-wire, with bulk edit for multi-selection.
  - Wire properties panel: name/circuit text, color, stripe, gauge, wire type, end types,
    terminals, computed length (read-only, with override field).
- **Selecting a wire** in the schematic shows its calculated length (from the layout) in the
  properties panel and status bar.
- Pan/zoom, grid snap, align/distribute, copy/paste, multi-select, delete, rename, search/filter by
  circuit text or pin label.

### 7.2 Layout editor
- Place **connectors, branch points, splices, flying-lead ends, eyelets/ring terminals** as nodes.
  Connect nodes with segments; each segment has an editable **length**.
- Layout editor shows which wires pass through a segment, and the count.
- Each wire's end-to-end path through the layout is resolved automatically from the schematic
  endpoints. Unrouted wires (an endpoint with no layout node, or no path) are flagged clearly in a
  "Problems" panel with click-to-select.
- **Highlight mode:** clicking a connector (or a wire/pin/segment) highlights everything related
  and dims everything else, in both the layout and schematic views. Toggle highlight/dim, and
  clear with Esc or clicking empty space. The related set is computed in `tbh_core`.
- **Bundle diameter** is shown per segment (see 8.2) and can be displayed as a label on the
  segment.
- Sleeving assignment per segment, with the recommendation from 8.3 offered but never forced.

### 7.3 BOM
- Generated live from the model; shown in a table view with tabs or sections:
  - **Wire:** grouped by color + stripe + gauge + wire type; total length with waste factor;
    count of cut pieces; optional **cut list** (one row per wire: name, ends, color/gauge, length)
  - **Sleeving:** grouped by size/type, total length with waste factor
  - **Connectors:** part number, description, quantity
  - **Terminals / seals / cavity plugs / splices:** part numbers and quantities, derived from pin and
    wire-end data
  - Items lacking a part number are listed as "(no part number)" and flagged.
- Export **CSV** in v1 (one file per section, or a combined file); PDF in a later milestone.
- BOM-only manual extras (e.g. tape, zip ties) can be added as free rows and are saved in `bom.json`.

## 8. Calculations (all live in `tbh_core/calc`, fully unit-tested)

### 8.1 Wire length
```
route   = path of layout segments between the wire's two end nodes
base    = sum(segment.length over route)
ends    = endAllowance(endA) + endAllowance(endB)          # strip/terminal/slack per end type
length  = (base + ends) * (1 + serviceLoopPercent/100)
cut     = roundUp(length, lengthRoundingIncrement)
```
`lengthOverride` wins if set. Show the breakdown (base, allowances, loop, rounding) in the UI so the
number is trustworthy at the bench. Flying leads add their own lead length (the lead segment).

### 8.2 Bundle diameter (per layout segment)
```
D = sqrt( sum(d_i^2) / k )
```
where `d_i` is each passing wire's insulated outer diameter (from the wire type + gauge table) and
`k` is the packing factor (default 0.70, user-adjustable, per harness). If a wire's OD is unknown,
use a clearly labeled fallback and flag it. Round for display; keep full precision internally.

### 8.3 Sleeving recommendation
Smallest sleeving catalog entry (of the selected type) whose usable inner diameter is
`>= D * (1 + sleevingDiameterMargin)`. For heat shrink use the as-supplied inner diameter for
fit-over and warn if the recovered diameter would be larger than D. Present as a recommendation.

### 8.4 Highlight set
Given a selected connector: all wires with an end on it, the other connectors/splices/free ends
those wires reach, and the layout nodes/segments those wires traverse. Same idea for a selected
wire, pin, or segment.

## 9. UI / UX

- Main window: central tabbed or split editors (Schematic | Layout | BOM), left project/parts
  dock, right properties dock, bottom Problems dock, status bar with live info.
- **Modern look:** custom dark and light themes (system-follow option), consistent spacing,
  vector icons (SVG), high-DPI correct, smooth zoom. Theme code isolated in `app/ui/theme`.
- Keyboard-first where possible: shortcuts for tools, delete, duplicate, undo/redo, zoom to fit,
  find. All shortcuts listed in a help dialog.
- Autosave-to-recovery-file plus a "recover unsaved work" prompt on crash.
- Units (in/mm/cm) are set per harness; store internally in millimeters. New harnesses take the
  default units from **user preferences** (Edit > Preferences, persisted per user; default
  inches), so users do not have to set them every time.
- Everything user-visible goes through `tr()` so translation can come later.

## 10. Testing

- `tbh_core`: Qt Test via CTest. Required coverage: length math (including allowances, loop,
  rounding, overrides), bundle diameter and sleeving selection, highlight set computation, BOM
  grouping/totals, file round-trip, schema migration, pin label pattern parser (`A1-A8, B1-B8`).
- Model/command tests: undo/redo round-trips for every command.
- Performance sanity test: build a 999-pin connector with hundreds of wires and assert scene
  construction time stays under a documented budget. Budget (debug build, 999-pin connector,
  500 striped wires): scene construction < 1500 ms, one full-view render < 1000 ms
  (`tests/app/tst_schematic.cpp`, `largeConnectorPerformance`).
- From M6: CI runs build + tests on Ubuntu, Windows and macOS on pull requests and on demand
  (not on every push).

## 11. Build, packaging, release

- CMake presets for debug/release on each OS.
- GitHub Actions: build + `ctest` on all three OSes. Install Qt in CI with a maintained action
  (e.g. aqtinstall-based) pinned to the project's Qt version.
- Release workflow (on tag): 
  - Linux: AppImage (linuxdeploy + Qt plugin)
  - Windows: `windeployqt` -> installer (Inno Setup or NSIS) plus a portable zip
  - macOS: `macdeployqt` -> `.dmg` (note code-signing/notarization status honestly in README)
- Register the `.tbh` file association in the installers where practical.
- README: screenshots, build-from-source steps per OS, the privacy statement ("this app makes no
  network connections"), and contribution guide. CONTRIBUTING.md and issue templates.

## 12. Milestones (build in this order)

Each milestone ends with: tests passing (locally until CI exists in M6), docs updated, a short
demo description.

**M1 - Skeleton, model, file I/O**
- CMake project, `tbh_core` + `app` split, GPLv3 files, Qt Test wired into CTest, themes stub.
- Core model (sections 5.1-5.5), JSON (de)serialization, `.tbh` zip save/load with schema version.
- Main window shell with docks, New/Open/Save/Save As, undo stack plumbing, recent files.
- *Done when:* a model built in code round-trips through a saved `.tbh` file, opens in the app, and
  tests pass locally.

**M2 - Connector and schematic editor**
- Connector box with `[-] N [+]` control (1-999), pin labels, circuit text, pin pattern dialog.
- Wires between pins/splices/free ends, color + stripe + gauge, properties panel, undo/redo for all.
- Large-connector performance work (section 7.1).
- *Done when:* a 300-pin ECU connector wired to several smaller connectors with striped wires can be
  built, saved, reopened, and edited smoothly.

**M3 - Layout editor and wire length**
- Layout nodes/segments with editable lengths; flying leads, eyelets, branch points.
- Harness settings editor: end/strip allowance per end type (default 0), service loop, rounding
  increment, waste factors. Undoable, saved in the file.
- Schematic wire re-attach: drag a selected wire's end onto another pin or splice to move it,
  or onto empty space to make it a flying lead; drop a free end on a pin/splice to attach it.
- Route resolution, length calculation (8.1), length shown on wire selection in the schematic,
  Problems panel for unrouted wires.
- *Done when:* clicking a wire in the schematic shows a correct, explained length for a test harness
  verified by hand-calculated unit tests.

**M4 - Highlighting and bundle diameter**
- Shared selection model, highlight/dim in both views (8.4).
- Per-segment bundle diameter (8.2), wire-type OD library editor, sleeving catalog and
  recommendation (8.3), sleeving assignment per segment.
- *Done when:* diameters match hand calculations in tests and sleeving recommendations update live
  when wires or gauges change.

**M5 - BOM and exports**
- BOM generation (section 7.3), table UI, CSV export, cut list, manual BOM rows.
- *Done when:* a complete harness exports a BOM whose wire/sleeving totals match unit-test fixtures.

**M6 - Polish and packaging**
- Themes finalized, shortcuts, autosave/recovery, help, accessibility pass, translations scaffolding.
- GitHub Actions CI (build + tests on Ubuntu, Windows, macOS; PRs and manual runs only).
- Release workflow producing AppImage, Windows installer + zip, macOS dmg. v0.1.0 tag.

**Backlog (v2 ideas, not in scope now):** 1:1 scale formboard PDF/SVG export, PDF BOM, connector
face-view drawings, twisted pair / shielded / multi-conductor cables, parts-library import from CSV,
multiple harness variants/options in one file, KiCad netlist import for connector pinouts.

## 13. CLAUDE.md rules (copy to repo root as `CLAUDE.md`)

- Read `docs/PROJECT_PLAN.md` first. Do not expand scope beyond the current milestone.
- Plan before coding on each milestone: list the files you will add/change and ask if anything
  is ambiguous. Prefer small, reviewable PRs.
- C++20, Qt 6, CMake. Follow Qt naming conventions. No raw `new`/`delete` outside Qt's parent-child
  ownership; prefer value types and smart pointers in `tbh_core`.
- `tbh_core` may link `Qt6::Core` only. Never include Widgets/Gui headers there.
- No network code, no telemetry, no analytics, no auto-update checks - ever. Build-time downloads
  (CMake FetchContent) are fine; runtime network access is not.
- Every user-visible edit is a `QUndoCommand`. Every calculation has unit tests with
  hand-computed expected values written in comments.
- Never present seeded library data (wire ODs, sleeving sizes) as authoritative.
- Every source file carries `// SPDX-License-Identifier: GPL-3.0-or-later`.
- All user-visible strings use `tr()`.
- Keep `docs/file-format.md` updated whenever the schema changes, and bump `schemaVersion` with a
  migration whenever a change is not backward compatible.
- Run the full test suite before declaring a milestone done. Report what was and was not verified.

## 14. Open questions (resolve during M1-M2, do not block on them)

- ~~Wire routing style on the schematic~~ - resolved in M2: **orthogonal** (horizontal stubs
  out of the pin row, one vertical run).
- ~~Default end allowances per end type~~ - resolved in M3: 0 for every end type, editable per
  harness in the harness properties (nothing selected).
- Sleeving coverage across multiple segments / partial segments (v1: full segment only).
- Final file extension (`.tbh` is the default).
