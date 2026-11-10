# CLAUDE.md

Rules for working in this repository (copied from section 13 of `docs/PROJECT_PLAN.md`).

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

## Build and test

```sh
cmake --preset debug -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build --preset debug
ctest --preset debug
```

Release build: same commands with the `release` preset. Tests need `QT_QPA_PLATFORM=offscreen`
(the test presets set it). Optional screenshots for visual checks: set `TBH_SCREENSHOT_DIR` and
run the `screenshots`/`screenshot` test functions in `tests/app`.

## Status (keep this current)

- M1-M5 done and on `main`. M6 polish done (autosave/recovery, help, accessibility,
  translations scaffolding). Hands-on testing fixes on `main`: splices inside the loom
  (type a splice stub's length to place it that far from the connector, or drag it onto the
  loom), several splices side by side at one loom point (`LayoutNode::sharedSpliceIds`),
  Show Bundle Diameters on by default. Split view (View > Split Schematic and Layout, Ctrl+\,
  side by side or stacked; off by default), Place All for unplaced items, a splice partway
  along a run labeled with the run's total length on two lines ("N inches total" /
  "branch length", `calc::spliceRunLengthMm`), and splice stubs placed by distance that walk
  through forks along the path most of their wires take, all merged to `main`. **Next: more user hands-on testing, then M6 packaging** (GitHub Actions
  CI on PRs/manual only, AppImage, Windows installer + zip, macOS dmg, v0.1.0 tag). CI was
  deliberately deferred until packaging; do not add workflows that run on every push.
- Flying leads, eyelets, ring terminals and bare ends are `FreeEnd` entities (schema 2,
  `freeEnds.json`; v1 files migrate on open), placed on the schematic like splices
  (Place > Add Flying Lead F / Add Eyelet E) and wired from their leads. Merged to `main`.
- Still open (not in any finished milestone): connector/terminal/seal autofill library (5.7),
  UI for manual wire route overrides (`routeSegmentIds`), keyboard-only wire/segment drawing,
  translations for languages other than English.
- User preferences: inches by default (mm/cm optional, set in Edit > Preferences); work on a
  branch per milestone and merge/push to `main` only when the user says so.

## Conventions in the code

- Model entities (Connector, Wire, Splice, LayoutNode, LayoutSegment, BomExtra) are edited only
  through `HarnessModel::insert/update/remove<T>` from undo commands
  (`app/commands/EntityCommands.h`). Compound edits that must keep references valid live in
  `app/commands/EditOps.*` (e.g. removing a connector clears layout node refs and deletes its
  wires). `io::deserializeHarness` rejects dangling references, so every edit must leave a
  loadable harness.
- Lengths are stored in mm; display/parse through `ui/Format.h` (`formatLength`, `parseLength`).
- Calculations live in `tbh_core/calc` and `tbh_core/bom` with hand-calculated tests; the shared
  fixture is `tests/core/SampleHarness.h`.
- Translations: QObject classes use `tr()`. Non-QObject files declare a local
  `class Tr { Q_DECLARE_TR_FUNCTIONS(<context>) };` and call `Tr::tr()` so `lupdate` sees the
  context. After adding plural (`%n`) strings run the `update_translations` target and fill the
  English forms in `translations/twisted-harness-designer_en.ts`.
- Tests that touch QSettings or app data call `QStandardPaths::setTestModeEnabled(true)` and use a
  `tbh-tests` organization name. Tests that create a `MainWindow` must clear
  `Autosave::recoveryDirectory()` first, or a leftover recovery file opens a modal dialog.
- Selection is shared across views via `SelectionModel`; highlight/dim is driven by
  `Highlighter` from it. Scenes never own data.
