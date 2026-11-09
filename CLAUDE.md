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
