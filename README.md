# Twisted Builds Harness Designer

Free, open-source (GPLv3-or-later) wiring harness design software for Linux, Windows and macOS.
Design connectors and circuits in a schematic, route them in a physical layout, and get wire
lengths, bundle diameters and a bill of materials.

**Privacy:** this application makes no network connections. No cloud, no accounts, no telemetry,
no update checks. Your designs never leave your computer.

> Status: early development (milestone 2 of 6: schematic editor). The layout editor, wire
> lengths, bundle diameters and BOM come in later milestones.
> See [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md) for scope and roadmap.

## Using the schematic editor

| Action | How |
|---|---|
| Add a connector / splice | **C** / **S** (or the toolbar) |
| Change pin count | `−` / `+` in the connector header, or click the number and type (1-999) |
| Fill pin labels in bulk | Properties > **Pin labels...** (e.g. `A1-A8, B1-B8, C1-C12`) |
| Edit a pin label or circuit | Double-click it; **Enter** moves down, **Tab** moves across |
| Draw a wire | Drag from a pin row to another pin or a splice; drop on empty space for a flying lead |
| Wire from a splice | Wire tool (**W**), then drag from the splice |
| Move items | Drag a connector by its header |
| Color, stripe, gauge | Properties panel; select several wires to change them together |
| Large connectors | Collapse with the header arrow, or set **Visible rows** to make the box scroll |
| Rename | **F2** or double-click a title |
| Find a pin or circuit | **Ctrl+F**, type, **Enter** for the next match |
| Pan / zoom | Middle-drag / mouse wheel (**Ctrl+wheel** over a scrolling connector), **Ctrl+0** fits |
| Copy, paste, duplicate, delete | **Ctrl+C**, **Ctrl+V**, **Ctrl+D**, **Delete** |
| Align / distribute | **Arrange** menu |

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

## File format

Harness files use the `.tbh` extension: a zip of human-readable JSON documents.
See [docs/file-format.md](docs/file-format.md).

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE).
