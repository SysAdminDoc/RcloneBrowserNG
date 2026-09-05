#!/bin/bash
set -euo pipefail

# Rclone Browser NG — local Linux AppImage build script.
# This is the local Linux release path; no CI release workflow is included.
#
# Usage: ./scripts/release_AppImage.sh
#
# Supported architectures: x86_64, aarch64
#
# Requirements:
#   - Ubuntu 22.04+ or equivalent (glibc 2.35+)
#   - cmake, g++, make, patchelf, sha256sum, zsyncmake, python3, curl or wget
#   - Qt 6 development packages (qt6-base-dev, qt6-base-dev-tools, qt6-qmake)
#   - desktop-file-utils, appstream (for validation)
#   - linuxdeploy, linuxdeploy-plugin-qt, linuxdeploy-plugin-appimage (auto-downloaded)

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64|aarch64) ;;
  *)
    echo "ERROR: Unsupported architecture '$ARCH'. Only x86_64 and aarch64 are supported."
    exit 1
    ;;
esac

for cmd in cmake g++ patchelf sha256sum zsyncmake python3; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "ERROR: '$cmd' not found. Install it first."
    exit 1
  fi
done
if ! command -v curl &>/dev/null && ! command -v wget &>/dev/null; then
  echo "ERROR: curl or wget is required to download packaging tools."
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Same branch policy the Windows lane enforces: refuse a Qt that is either
# unpatched for the documented CVEs or past its open-source support date.
QMAKE_TOOL="$(command -v qmake6 || command -v qmake || true)"
if [ -z "$QMAKE_TOOL" ]; then
  echo "ERROR: qmake6 not found. Install the Qt 6 development packages."
  exit 1
fi
python3 "$SCRIPT_DIR/validate_qt_version.py" --qmake "$QMAKE_TOOL"

ROOT="$SCRIPT_DIR"/..
VERSION="$(cat "$ROOT/VERSION")"
COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || true)"
if [ -n "$COMMIT" ]; then
  FULLVER="${VERSION}-${COMMIT}"
else
  FULLVER="$VERSION"
fi

# linuxdeploy reads $VERSION for the AppImage filename
export VERSION="$FULLVER"

BUILD="$ROOT/build"
RELEASE="$ROOT/release"

# Use RAM disk if available and not in CI
if [ -z "${CI:-}" ] && [ -d /dev/shm ]; then
  TEMP_BASE=/dev/shm
else
  TEMP_BASE="${TMPDIR:-/tmp}"
fi
APPDIR="$TEMP_BASE/RcloneBrowserNG-AppDir-$$"

# Clean previous build artifacts
rm -rf "$BUILD" "$APPDIR"
mkdir -p "$BUILD" "$RELEASE" "$APPDIR"

# Validate desktop and metainfo files
if command -v desktop-file-validate &>/dev/null; then
  desktop-file-validate "$ROOT/assets/io.github.sysadmindoc.rclonebrowserng.desktop"
fi
if command -v appstreamcli &>/dev/null; then
  appstreamcli validate --no-net "$ROOT/assets/io.github.sysadmindoc.rclonebrowserng.metainfo.xml" || true
fi

# Build
cd "$BUILD"
cmake "$ROOT" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make --jobs="$(nproc)"
DESTDIR="$APPDIR/AppDir" make install

# Copy info files
cp "$ROOT/README.md" "$APPDIR/AppDir/Readme.md"
cp "$ROOT/CHANGELOG.md" "$APPDIR/AppDir/Changelog.md"
cp "$ROOT/LICENSE" "$APPDIR/AppDir/License.txt"

# Download and verify pinned linuxdeploy tools (architecture-specific)
TOOLS_DIR="$APPDIR/tools"
"$ROOT/scripts/fetch_linuxdeploy_tools.sh" "$ARCH" "$TOOLS_DIR"

cd "$APPDIR"
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS_DIR:$PATH"
export LDAI_UPDATE_INFORMATION="gh-releases-zsync|SysAdminDoc|RcloneBrowserNG|latest|RcloneBrowserNG-*linux-${ARCH}.AppImage.zsync"

# Keep the offscreen platform available for the packaged --version smoke. It
# never opens a window, so release verification remains safe on headless hosts.
QMAKE_TOOL="$(command -v qmake6 || command -v qmake || true)"
if [ -n "$QMAKE_TOOL" ]; then
  QT_PLUGINS="$($QMAKE_TOOL -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
  OFFSCREEN_PLUGIN="$QT_PLUGINS/platforms/libqoffscreen.so"
  PLATFORM_DIR="$(find "$APPDIR/AppDir" -type d -name platforms -print -quit)"
  if [ -f "$OFFSCREEN_PLUGIN" ] && [ -n "$PLATFORM_DIR" ]; then
    cp "$OFFSCREEN_PLUGIN" "$PLATFORM_DIR/"
  fi
fi

"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
  --appdir AppDir \
  --desktop-file "AppDir/usr/share/applications/io.github.sysadmindoc.rclonebrowserng.desktop" \
  --plugin qt \
  --output appimage

# Move the resulting AppImage to the release directory
APPIMAGE="$(find . -maxdepth 1 -name '*.AppImage' -type f | head -n 1)"
if [ -z "$APPIMAGE" ]; then
  echo "ERROR: No AppImage produced."
  exit 1
fi

ARTIFACT_NAME="RcloneBrowserNG-${FULLVER}-linux-${ARCH}.AppImage"
mv "$APPIMAGE" "$RELEASE/$ARTIFACT_NAME"

zsyncmake "$RELEASE/$ARTIFACT_NAME" \
  -o "$RELEASE/$ARTIFACT_NAME.zsync"
python3 "$ROOT/scripts/verify_appimage_update.py" \
  --artifact "$RELEASE/$ARTIFACT_NAME" \
  --expected "$LDAI_UPDATE_INFORMATION"

if command -v python3 >/dev/null 2>&1; then
  python3 "$ROOT/scripts/smoke_package.py" \
    --artifact "$RELEASE/$ARTIFACT_NAME" --version "$VERSION"
else
  echo "NOTE: python3 not found — skipping packaged AppImage smoke."
fi

# Clean up temp directory
cd "$ROOT"
rm -rf "$APPDIR"

echo
echo "Release artifact: $RELEASE/$ARTIFACT_NAME"
