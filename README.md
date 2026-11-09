# Twisted Builds Harness Designer

Free, open-source (GPLv3-or-later) wiring harness design software for Linux, Windows and macOS.
Design connectors and circuits in a schematic, route them in a physical layout, and get wire
lengths, bundle diameters and a bill of materials.

**Privacy:** this application makes no network connections. No cloud, no accounts, no telemetry,
no update checks. Your designs never leave your computer.

> Status: early development (milestone 1 of 6: project skeleton, data model, file I/O).
> See [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md) for scope and roadmap.

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
