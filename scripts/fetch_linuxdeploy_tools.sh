#!/bin/bash
set -euo pipefail

usage() {
  echo "Usage: $0 <x86_64|aarch64> <output-dir>" >&2
}

if [ "$#" -ne 2 ]; then
  usage
  exit 2
fi

ARCH="$1"
OUT_DIR="$2"

case "$ARCH" in
  x86_64|aarch64) ;;
  *)
    echo "ERROR: Unsupported linuxdeploy architecture '$ARCH'." >&2
    usage
    exit 2
    ;;
esac

if ! command -v sha256sum >/dev/null 2>&1; then
  echo "ERROR: sha256sum is required to verify downloaded packaging tools." >&2
  exit 1
fi

download_file() {
  local url="$1"
  local dest="$2"

  if command -v curl >/dev/null 2>&1; then
    curl --fail --location --silent --show-error --output "$dest" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$dest" "$url"
  else
    echo "ERROR: curl or wget is required to download packaging tools." >&2
    exit 1
  fi
}

sha_for() {
  local key="$1"
  case "$key" in
    linuxdeploy:x86_64) echo "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d" ;;
    linuxdeploy:aarch64) echo "620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff" ;;
    qt:x86_64) echo "15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724" ;;
    qt:aarch64) echo "bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1" ;;
    appimage:x86_64) echo "992d502a248e14ab185448ddf6f6e7d25558cb84d4623c354c3af350c25fccb3" ;;
    appimage:aarch64) echo "83c292149274965a865dcd44c135cfca8ba28c6b7de3eb628d4b8b5f248af17c" ;;
    *)
      echo "ERROR: No SHA256 pinned for '$key'." >&2
      exit 1
      ;;
  esac
}

fetch_tool() {
  local repo="$1"
  local tag="$2"
  local asset="$3"
  local key="$4"
  local expected actual tmp dest

  expected="$(sha_for "$key")"
  tmp="$(mktemp)"
  dest="$OUT_DIR/$asset"

  if ! download_file "https://github.com/linuxdeploy/$repo/releases/download/$tag/$asset" "$tmp"; then
    rm -f "$tmp"
    echo "ERROR: Download failed for $asset" >&2
    exit 1
  fi
  actual="$(sha256sum "$tmp" | awk '{print $1}')"
  if [ "$actual" != "$expected" ]; then
    echo "ERROR: SHA256 mismatch for $asset" >&2
    echo "  expected: $expected" >&2
    echo "  actual:   $actual" >&2
    rm -f "$tmp"
    exit 1
  fi
  mv "$tmp" "$dest"
  chmod +x "$dest"
  echo "Verified $asset"
}

mkdir -p "$OUT_DIR"

fetch_tool "linuxdeploy" "1-alpha-20251107-1" \
  "linuxdeploy-${ARCH}.AppImage" "linuxdeploy:${ARCH}"
fetch_tool "linuxdeploy-plugin-qt" "1-alpha-20250213-1" \
  "linuxdeploy-plugin-qt-${ARCH}.AppImage" "qt:${ARCH}"
fetch_tool "linuxdeploy-plugin-appimage" "1-alpha-20250213-1" \
  "linuxdeploy-plugin-appimage-${ARCH}.AppImage" "appimage:${ARCH}"
