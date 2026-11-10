#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds, tests and packages the Linux AppImage. Runs inside an AlmaLinux 8
# container so the AppImage needs only glibc 2.28 (Ubuntu 20.04+, Debian 10+,
# RHEL/Rocky/Alma 8+, Fedora 29+). From the repository root:
#
#   docker run --rm -v "$PWD":/src -w /src -e HOST_UID="$(id -u)" \
#       almalinux:8 packaging/linux/build-appimage.sh
#
# Output: dist/TwistedHarnessDesigner-<version>-x86_64.AppImage
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.8.3}"
QT_DIR="/opt/qt/${QT_VERSION}/gcc_64"
BUILD_DIR="build/appimage"
APPDIR="${BUILD_DIR}/AppDir"
TOOLS_DIR="${BUILD_DIR}/tools"

# Build tools and the system libraries Qt links against. xcb-util-cursor comes
# from EPEL; it is bundled into the AppImage because many distros lack it.
dnf -y -q install dnf-plugins-core epel-release
dnf config-manager --set-enabled powertools
dnf -y -q install gcc-toolset-13 python3.11 python3.11-pip file findutils \
    mesa-libGL-devel mesa-libEGL-devel libxkbcommon-devel libxkbcommon-x11 \
    fontconfig-devel freetype-devel libX11-devel libxcb xcb-util-wm xcb-util-image \
    xcb-util-keysyms xcb-util-renderutil xcb-util-cursor dbus-libs
# shellcheck disable=SC1091
source /opt/rh/gcc-toolset-13/enable
python3.11 -m pip install -q aqtinstall cmake ninja
export PATH="/usr/local/bin:${PATH}"

if [ ! -x "${QT_DIR}/bin/qmake" ]; then
    aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O /opt/qt
fi

cmake -S . -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "${BUILD_DIR}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "${BUILD_DIR}" --output-on-failure

rm -rf "${APPDIR}"
DESTDIR="$(pwd)/${APPDIR}" cmake --install "${BUILD_DIR}"

mkdir -p "${TOOLS_DIR}"
for tool in linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
            linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage; do
    file_name="${TOOLS_DIR}/$(basename "${tool}")"
    [ -f "${file_name}" ] || curl -fsSL -o "${file_name}" "https://github.com/${tool}"
    chmod +x "${file_name}"
done

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "${BUILD_DIR}/CMakeCache.txt")"
export APPIMAGE_EXTRACT_AND_RUN=1  # no FUSE inside containers
export QMAKE="${QT_DIR}/bin/qmake"
export LD_LIBRARY_PATH="${QT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION}"
export PATH="$(pwd)/${TOOLS_DIR}:${PATH}"

rm -f ./*.AppImage
linuxdeploy-x86_64.AppImage --appdir "${APPDIR}" --plugin qt --output appimage

mkdir -p dist
mv ./*-x86_64.AppImage "dist/TwistedHarnessDesigner-${VERSION}-x86_64.AppImage"
if [ -n "${HOST_UID:-}" ]; then
    chown -R "${HOST_UID}" dist "${BUILD_DIR}"
fi
echo "Built dist/TwistedHarnessDesigner-${VERSION}-x86_64.AppImage"
