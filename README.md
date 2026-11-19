# Twisted Builds Harness Designer

<img width="2559" height="1397" alt="Schematic" src="https://github.com/user-attachments/assets/f354d544-e4c4-4515-aaaf-3e0b3e7c9ec8" />

<img width="2560" height="1397" alt="Layout" src="https://github.com/user-attachments/assets/648d03e0-40a8-490d-9cf6-9f4af6faed8a" />

Free, open-source (GPLv3-or-later) wiring harness design software for Linux, Windows and macOS.
Design connectors and circuits in a schematic, route them in a physical layout, and get wire
lengths, bundle diameters and a bill of materials.

**Privacy:** this application makes no network connections. No cloud, no accounts, no telemetry,
no update checks. Your designs never leave your computer.

Status: v0.1.1 released. See [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md) for
scope and roadmap.

## What's new in 0.1.1

- **Twisted and shielded cables:** group wires into a twisted pair (or any number of
  conductors), add a shield wire, and mark the cable as built or bought. The schematic draws
  the conductors twisting around each other; cut lengths include twist take-up, and bought
  cables get their own BOM line.
- **Connector, terminal, seal and plug library** with seed parts (Deutsch DT/DTM/DTP,
  TE AMPSEAL, Molex CMC), CSV import, part pickers in the connector panel and fit warnings.
- **Add-on harnesses:** mark factory wires and connectors (left out of the BOM), cut a
  factory wire where you splice in, and see the factory harness faded in the layout.
- **Layout labels, zip ties, strain relief and backshells**, counted in the BOM.
- **Circuit IDs**, wiring a pin by typing where it goes, pin table bulk edit and a
  cavity plug column.
- **Schematic outlines** around assemblies, **ground / chassis / power symbols**, easier
  wire end grabbing.
- **Pan and touch options:** right drag or Space + drag to pan, wheel panning, pinch zoom,
  touchscreens.

Full list and bug fixes: [v0.1.1 release notes](https://github.com/twisted-builds-llc/Twisted_Builds_Harness_Designer/releases/tag/v0.1.1).
Files saved by 0.1.1 cannot be opened by 0.1.0; older files open and upgrade automatically.

## Download and install

Get the latest version from the
[Releases page](https://github.com/twisted-builds-llc/Twisted_Builds_Harness_Designer/releases).
The installers are **not code-signed** yet, so Windows and macOS warn the first time you open
them. That is expected; the steps below get past it.

- **Windows 10/11 (64-bit):** run `TwistedHarnessDesigner-<version>-setup.exe`. If Windows
  shows "Windows protected your PC", click **More info**, then **Run anyway**. The installer
  asks whether to install for you only (no admin rights needed) or for all users, and can
  make `.tbh` files open in the app. The `-windows-x64.zip` is a portable copy: unzip it
  and run `bin\twisted-harness-designer.exe`.
- **macOS 12 or newer (Apple Silicon and Intel):** open the `.dmg` and drag
  **Twisted Harness Designer** to **Applications**. The first time, macOS says it cannot
  check the app for malicious software. On macOS 15 and newer, open **System Settings >
  Privacy & Security**, scroll down and click **Open Anyway**. On macOS 12-14, right-click
  the app, choose **Open**, then **Open** again.
- **Linux (64-bit, glibc 2.28 or newer: Ubuntu 20.04+, Debian 10+, Fedora 29+,
  RHEL/Rocky/Alma 8+, Mint 20+):** download the `.AppImage`, make it executable
  (`chmod +x TwistedHarnessDesigner-*.AppImage`, or Properties > Permissions in your file
  manager) and run it. If it complains about FUSE, run it with `--appimage-extract-and-run`,
  or install FUSE 2 (`sudo apt install libfuse2`, called `libfuse2t64` on Ubuntu 24.04).

## Using the schematic editor

| Action | How |
|---|---|
| Add a connector / splice | **C** / **S** (or the toolbar) |
| Change pin count | `−` / `+` in the connector header, or click the number and type (1-999) |
| Fill pin labels in bulk | Properties > **Pin labels...** (e.g. `A1-A8, B1-B8, C1-C12`) |
| Edit a pin label or circuit | Double-click it; **Enter** moves down, **Tab** moves across |
| Add a flying lead / eyelet | **F** / **E** (or the toolbar), then drag from its lead to a pin; set ring terminal, bare end, label and terminal P/N in Properties |
| Add a ground | **G** (**Place > Add Ground**) adds a GND eyelet; splices and free ends can show a ground, chassis ground or power symbol (Properties) |
| Outline an assembly | Select items, press **O** (**Place > Add Outline**) and type a title; drag its edge to move it with everything inside, drag the corner handle to resize |
| Draw a wire | Drag from a pin row to another pin or a splice; drop on empty space for a flying lead |
| Move a wire end | Select the wire, then drag its end to another pin, splice, flying lead or eyelet (or empty space for a new flying lead) |
| Wire from a splice | Wire tool (**W**), then drag from the splice |
| Move items | Drag a connector by its header |
| Wire a pin by name | Connector Properties > pin table **Wired to** column, or right-click a pin > **Connect Pin To...** (e.g. `ECU : 12`, a splice or flying lead; commas for several) |
| Edit many pins at once | Select several cells in the pin table, then type, **Delete**, or **Ctrl+C** / **Ctrl+V** |
| Circuit IDs | Every wire gets one (C001, C002, ...); edit it in Properties. Duplicates show in **Problems** |
| Color, stripe, gauge | Properties panel; select several wires to change them together |
| Twisted / shielded cable | Select 2+ wires, right-click > **Make Twisted / Shielded Cable**; set twist, **Add Shield Wire**, built or bought in Properties |
| Factory wiring (add-on harness) | Tick **Factory wire** / **Factory connector** in Properties: drawn dashed, left out of the BOM and build sheets |
| Cut a wire to splice in | Right-click the wire > **Cut Wire Here** (asks where along the route); right-click a cut end > **Replace Cut with Splice** |
| Large connectors | Collapse with the header arrow, or set **Visible rows** to make the box scroll |
| Rename | **F2** or double-click a title |
| Find a pin or circuit | **Ctrl+F**, type, **Enter** for the next match |
| Pan / zoom | Middle-drag (or right-drag / **Space**+drag, set in Preferences) / mouse wheel or pinch (**Ctrl+wheel** over a scrolling connector; Preferences can make the wheel pan and **Ctrl+wheel** zoom), **Ctrl+0** fits |
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
| Place a splice or cut in the loom | Drag it onto a segment or branch point, or type its stub length; **Position Along Bundle...** moves it along the run |
| Labels and zip ties | Right-click a segment > **Add Label Here** / **Add Zip Tie Here** (or **Add Zip Tie at Branch** on a branch point); drag along the segment; set text, part number and a cover sleeve in Properties |
| Factory harness | Factory-only parts are faded; **View > Show Factory Harness** hides them. Cut points show their distance from the nearest factory connector |

Branch lengths are what you type, not what you draw: the drawing does not need to be to scale.

## Highlighting, bundle diameters and sleeving

| Action | How |
|---|---|
| See everything connected to something | Select a connector, pin, wire, splice, branch point or segment: related items in both editors stay bright and the rest dims. **Esc** or clicking empty space clears it. Toggle with **View > Highlight Related Items** |
| Highlight a single pin | Click its row in the connector box |
| Bundle diameter | In each layout segment's tooltip and properties; **View > Show Bundle Diameters** also shows it on the segment (`~` means some wire sizes are estimated) |
| Pick sleeving | Select a segment: choose a sleeving type and size, or use the recommended size |
| Wire types and sleeving sizes | **Edit > Parts Library** |

## Connector parts library

**Edit > Parts Library** has **Connectors** and **Terminals, Seals & Plugs** tabs. Add, edit,
duplicate or remove parts, or **Import Parts CSV...** (**Save CSV Template...** gives the
columns; files are read locally). In a connector's Properties, the part number field lists
library housings: picking one fills in the maker, description, pin count and cavity labels.
The Terminal, Seal and Plug P/N cells list the parts made for that housing first. The
**Problems** panel warns when a terminal or seal does not fit the wire's gauge or insulation
diameter, or a part is not listed for the housing.

## Bill of materials

The **BOM** tab is generated live from the design:

- **Wire**: grouped by color, stripe, gauge and wire type, with total length and the waste
  factor from the harness settings. **Cut List**: one row per wire with its circuit ID, cable
  and cut length (double-click to find the wire).
- **Cables**: bought cables by part number and length, and shield braid for built shielded
  cables.
- **Sleeving**: total length per type and size, with waste (label cover sleeves included).
- **Parts**: connectors, backshells, terminals (one per wired pin, plus eyelets and ring
  terminals), seals, cavity plugs (the pin table's Plug P/N column), splices, labels and
  zip ties / strain relief, counted from the part numbers you enter.
- **Other Items**: anything else (tape, etc.), added by hand and saved in the file.

Factory wires and connectors are left out, and so is sleeving on segments that carry only
factory wires unless you tick **Added in this build (count in BOM)** on the segment.

Lines missing a part number, unrouted wires and sleeving not in the library are marked with
a warning. **Export CSV** (or **File > Export BOM**) writes one combined file or one file per
section, in the harness units.

**The parts library shipped with the app is approximate.** Wire outer diameters, sleeving
sizes and the seed connector, terminal, seal and plug parts are typical values, marked "approx." in the app until you edit them. Check them
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

How the installers are built is described in [packaging/README.md](packaging/README.md).

## Help, autosave and keyboard use

- **Help > Getting Started** (**F1**) walks through a first harness; **Help > Keyboard
  Shortcuts** lists every shortcut, generated from the app itself.
- **Autosave for recovery:** unsaved changes are written to a recovery file every 2 minutes
  (change it, or turn it off, in **Edit > Preferences**). If the app closes unexpectedly, the
  next start offers to recover the work; **File > Recover Unsaved Work** shows it again later.
  Recovery files are removed when you save, and when the app closes normally. The status bar
  shows the last autosave time, and says so if an autosave fails.
- **Keyboard:** in either editor, **]** / **[** move between items, arrow keys move the
  selection (**Shift** for bigger steps), **F2** renames, **Delete** deletes, **Esc** clears.
  Drawing wires and segments still needs a mouse.
- **Languages:** the app is ready for translation; see [translations/README.md](translations/README.md).

## File format

Harness files use the `.tbh` extension: a zip of human-readable JSON documents.
See [docs/file-format.md](docs/file-format.md).

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE).
