# Packaging and releases

The `Release` GitHub Actions workflow (`.github/workflows/release.yml`) builds every package.
`CI` (`.github/workflows/ci.yml`) builds and tests on pull requests and manual runs only.

| Platform | Package | Built by |
|---|---|---|
| Linux x86_64 | `TwistedHarnessDesigner-<v>-x86_64.AppImage` | `linux/build-appimage.sh` in an AlmaLinux 8 container (glibc 2.28) with linuxdeploy and its Qt plugin |
| Windows x64 | `TwistedHarnessDesigner-<v>-setup.exe` and `-windows-x64.zip` | `cmake --install` (runs windeployqt, copies the MSVC runtime), then Inno Setup with `windows/installer.iss` |
| macOS 12+ universal | `TwistedHarnessDesigner-<v>-macos.dmg` | `cmake --install` (runs macdeployqt), ad-hoc `codesign`, `hdiutil` |

All packages use Qt 6.8.3 (LTS). The version comes from `project(VERSION ...)` in the top-level
`CMakeLists.txt`.

## Making a release

1. Set `project(VERSION x.y.z)` in `CMakeLists.txt` and merge to `main`.
2. Tag and push: `git tag vx.y.z && git push origin vx.y.z`.
3. The workflow builds, runs the tests on each OS, and creates a **draft** release with the
   four files. It fails if the tag does not match the project version.
4. Download and try each package, then publish the draft on GitHub.

To build the packages without releasing, run the `Release` workflow by hand (Actions tab >
Release > Run workflow); the files appear as workflow artifacts.

## Building the AppImage locally

Needs Docker. From the repository root:

```sh
docker run --rm -v "$PWD":/src -w /src -e HOST_UID="$(id -u)" \
    almalinux:8 packaging/linux/build-appimage.sh
```

The AppImage lands in `dist/`. The build directory is `build/appimage`.

## Icons

`resources/icons/twisted-harness-designer.{png,ico,icns}` are drawn by `icons/make_icons.py`
(needs Pillow). To change the icon, edit the script or replace the three files.

## Signing

Nothing is signed with a publisher certificate yet. Windows SmartScreen and macOS Gatekeeper
warn on first open (README explains the steps). The macOS app has an ad-hoc signature only,
which Apple Silicon needs to run it at all. Signing needs an Apple Developer ID (plus
notarization) and a Windows code-signing certificate.
