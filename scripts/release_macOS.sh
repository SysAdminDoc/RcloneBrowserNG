#!/bin/bash
set -euo pipefail

# Rclone Browser NG — local macOS release build script.
# Matches the CI release workflow; see .github/workflows/release.yml.
#
# Usage: ./scripts/release_macOS.sh
#
# Requirements:
#   - Xcode command line tools
#   - Homebrew
#   - cmake and qt@6 (brew install cmake qt@6)

ARCH="$(uname -m)"

for cmd in cmake brew; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "ERROR: '$cmd' not found."
    exit 1
  fi
done

QT_PREFIX="$(brew --prefix qt@6 2>/dev/null || true)"
if [ -z "$QT_PREFIX" ] || [ ! -d "$QT_PREFIX" ]; then
  echo "ERROR: Qt 6 not found. Install with: brew install qt@6"
  exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/..
VERSION="$(cat "$ROOT/VERSION")"
COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || true)"
if [ -n "$COMMIT" ]; then
  FULLVER="${VERSION}-${COMMIT}"
else
  FULLVER="$VERSION"
fi

JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
BUILD="$ROOT/build"
RELEASE="$ROOT/release"
NAME="RcloneBrowserNG-${FULLVER}-macos-${ARCH}"

# Clean previous build
rm -rf "$BUILD"
rm -rf "$RELEASE/$NAME"*
mkdir -p "$BUILD" "$RELEASE"

# Build
cd "$BUILD"
cmake "$ROOT" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build . --parallel "$JOBS"

APP="$BUILD/build/rclone-browser.app"
if [ ! -d "$APP" ]; then
  echo "ERROR: Build did not produce rclone-browser.app"
  exit 1
fi

# Deploy Qt frameworks and create DMG
"$QT_PREFIX/bin/macdeployqt" "$APP" -dmg -verbose=2

DMG="$BUILD/build/rclone-browser.dmg"
if [ -f "$DMG" ]; then
  mv "$DMG" "$RELEASE/${NAME}.dmg"
  echo "DMG: $RELEASE/${NAME}.dmg"
fi

# Create compressed app zip
ditto -c -k --keepParent "$APP" "$RELEASE/${NAME}.app.zip"
echo "ZIP: $RELEASE/${NAME}.app.zip"

echo
echo "Release artifacts in $RELEASE:"
ls -1 "$RELEASE/${NAME}"*
