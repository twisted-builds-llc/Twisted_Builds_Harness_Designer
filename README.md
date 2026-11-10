# Twisted Builds Harness Designer

Free, open-source (GPLv3-or-later) wiring harness design software for Linux, Windows and macOS.
Design connectors and circuits in a schematic, route them in a physical layout, and get wire
lengths, bundle diameters and a bill of materials.

**Privacy:** this application makes no network connections. No cloud, no accounts, no telemetry,
no update checks. Your designs never leave your computer.

> Status: early development. All editing features of v0.1 are in place (schematic, layout,
> wire lengths, highlighting, bundle diameters, sleeving, BOM, autosave, help). Installers
> come next.
> See [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md) for scope and roadmap.

## Using the schematic editor

| Action | How |
|---|---|
| Add a connector / splice | **C** / **S** (or the toolbar) |
| Change pin count | `−` / `+` in the connector header, or click the number and type (1-999) |
| Fill pin labels in bulk | Properties > **Pin labels...** (e.g. `A1-A8, B1-B8, C1-C12`) |
| Edit a pin label or circuit | Double-click it; **Enter** moves down, **Tab** moves across |
| Add a flying lead / eyelet | **F** / **E** (or the toolbar), then drag from its lead to a pin; set ring terminal, bare end, label and terminal P/N in Properties |
| Draw a wire | Drag from a pin row to another pin or a splice; drop on empty space for a flying lead |
| Move a wire end | Select the wire, then drag its end to another pin, splice, flying lead or eyelet (or empty space for a new flying lead) |
| Wire from a splice | Wire tool (**W**), then drag from the splice |
| Move items | Drag a connector by its header |
| Color, stripe, gauge | Properties panel; select several wires to change them together |
| Large connectors | Collapse with the header arrow, or set **Visible rows** to make the box scroll |
| Rename | **F2** or double-click a title |
| Find a pin or circuit | **Ctrl+F**, type, **Enter** for the next match |
| Pan / zoom | Middle-drag / mouse wheel (**Ctrl+wheel** over a scrolling connector), **Ctrl+0** fits |
| Copy, paste, duplicate, delete | **Ctrl+C**, **Ctrl+V**, **Ctrl+D**, **Delete** |
| Align / distribute | **Arrange** menu |

## Using the layout editor and wire lengths

| Action | How |
|---|---|
| Put an item in the layout | **Layout** tab > double-click it in "Not in the layout yet" |
| Add a branch point | **B** |
| Draw a branch (segment) | Segment tool (**W**), drag from one point to another, or to empty space for a new branch point, then type its length |
| Change a branch length | Double-click the segment, or edit it in Properties |
| Allowances, service loop, rounding | Select nothing; edit the harness settings in Properties |
| See a wire's cut length | Select the wire: Properties and the status bar show the length and how it was calculated |
| Fix unrouted wires | **Problems** panel lists them; click one to select it |

Branch lengths are what you type, not what you draw: the drawing does not need to be to scale.

## Highlighting, bundle diameters and sleeving

| Action | How |
|---|---|
| See everything connected to something | Select a connector, pin, wire, splice, branch point or segment: related items in both editors stay bright and the rest dims. **Esc** or clicking empty space clears it. Toggle with **View > Highlight Related Items** |
| Highlight a single pin | Click its row in the connector box |
| Bundle diameter | In each layout segment's tooltip and properties; **View > Show Bundle Diameters** also shows it on the segment (`~` means some wire sizes are estimated) |
| Pick sleeving | Select a segment: choose a sleeving type and size, or use the recommended size |
| Wire types and sleeving sizes | **Edit > Parts Library** |

## Bill of materials

The **BOM** tab is generated live from the design:

- **Wire**: grouped by color, stripe, gauge and wire type, with total length and the waste
  factor from the harness settings. **Cut List**: one row per wire with its cut length
  (double-click to find the wire).
- **Sleeving**: total length per type and size, with waste.
- **Parts**: connectors, terminals (one per wired pin, plus eyelets and ring terminals),
  seals, cavity plugs and splices, counted from the part numbers you enter. For a cavity
  plug, enter the plug part number in the pin's Seal P/N column.
- **Other Items**: anything else (tape, ties, labels), added by hand and saved in the file.

Lines missing a part number, unrouted wires and sleeving not in the library are marked with
a warning. **Export CSV** (or **File > Export BOM**) writes one combined file or one file per
section, in the harness units.

**The parts library shipped with the app is approximate.** Wire outer diameters and sleeving
sizes are typical values, marked "approx." in the app until you edit them. Check them
against your manufacturer's datasheets before trusting bundle diameters or sleeving sizes.

## Building from source

Requirements: Qt 6.8 or newer, CMake 3.21+, Ninja, and a C++20 compiler
(GCC 11+, Clang 14+, or MSVC 2022). The first configure downloads the
[miniz](https://github.com/richgel999/miniz) zip library; nothing is downloaded at runtime.

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/<compiler>
cmake --build --preset release
ctest --preset release
./build/release/src/app/twisted-harness-designer
```

- **Linux:** install Qt with the Qt online installer or your distribution
  (Ubuntu 24.04 ships Qt 6.4 in `qt6-base-dev`, which is too old; use the installer there).
- **Windows:** run from a "x64 Native Tools Command Prompt for VS 2022" so Ninja finds MSVC.
- **macOS:** install Xcode command line tools and Ninja (`brew install ninja`).

Qt Creator can open `CMakeLists.txt` directly and picks up the presets.

## Help, autosave and keyboard use

- **Help > Getting Started** (**F1**) walks through a first harness; **Help > Keyboard
  Shortcuts** lists every shortcut, generated from the app itself.
- **Autosave for recovery:** unsaved changes are written to a recovery file every 2 minutes
  (change it, or turn it off, in **Edit > Preferences**). If the app closes unexpectedly, the
  next start offers to recover the work; **File > Recover Unsaved Work** shows it again later.
  Recovery files are removed when you save, and when the app closes normally.
- **Keyboard:** in either editor, **]** / **[** move between items, arrow keys move the
  selection (**Shift** for bigger steps), **F2** renames, **Delete** deletes, **Esc** clears.
  Drawing wires and segments still needs a mouse.
- **Languages:** the app is ready for translation; see [translations/README.md](translations/README.md).

## File format

Harness files use the `.tbh` extension: a zip of human-readable JSON documents.
See [docs/file-format.md](docs/file-format.md).

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE).
