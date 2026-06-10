# Rclone Browser NG

[![Version](https://img.shields.io/badge/version-2.0.0-blue.svg)](https://github.com/SysAdminDoc/RcloneBrowserNG/releases)
[![License](https://img.shields.io/github/license/SysAdminDoc/RcloneBrowserNG.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux%20%7C%20BSD-lightgrey.svg)](#build-instructions)
[![Build](https://github.com/SysAdminDoc/RcloneBrowserNG/actions/workflows/build.yml/badge.svg)](https://github.com/SysAdminDoc/RcloneBrowserNG/actions/workflows/build.yml)
[![Qt](https://img.shields.io/badge/Qt-5.15%20%2F%206.x-green.svg)](https://www.qt.io/)

Simple cross-platform GUI for the [rclone](https://rclone.org/) command line tool.

Community continuation of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser) (itself a fork of [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser)). Both upstream repositories are abandoned; this fork restores compatibility with modern rclone and Qt 6 and continues fixing bugs and adding features.

Supports Windows, macOS, GNU/Linux and the BSD family.

## What NG fixes over the abandoned upstream

*   Works with modern rclone (v1.56+) — transfer progress, size, bandwidth and ETA parse correctly again
*   Builds with Qt 6 (and still Qt 5.15) — required for Wayland and current Linux distributions
*   `rclone config` works with modern terminal emulators (gnome-terminal 3.38+, kitty, alacritty, wezterm, foot, tilix, …)
*   Delete runs as a background job instead of freezing the GUI
*   Multiple simultaneous mounts work on Windows (RC port collision fixed)
*   Cancelled jobs actually terminate the underlying rclone process
*   Many crash, reliability and dark-mode fixes — see [CHANGELOG.md](CHANGELOG.md)

## Features

*   Browse and modify any rclone remote, including encrypted ones
*   Uses the same configuration file as rclone — no extra configuration required
*   Custom location and encryption support for `rclone.conf`
*   Navigate multiple remotes simultaneously in separate tabs
*   Hierarchical file listing with name, size and modify date
*   All rclone commands run asynchronously — no GUI freezing
*   Lazily cached file hierarchy for fast folder traversal
*   Upload, download, create folders, rename, move and delete files and folders
*   Calculate folder sizes, export file lists, copy rclone commands to clipboard
*   Run multiple jobs in the background, with per-file progress
*   Drag & drop from your file explorer to upload
*   Stream media files to a player such as [mpv](https://mpv.io/) or [VLC](https://www.videolan.org)
*   Mount and unmount remotes (Windows needs [WinFsp](https://winfsp.dev/), macOS needs [macFUSE](https://macfuse.github.io/))
*   Optional tray icon with finished-transfer notifications
*   Portable mode — keep the app, rclone and its config on a memory stick
*   Google Drive "shared with me" support
*   Public link generation for remotes that support sharing
*   Saved tasks — create a job once, then run or edit it later (with dry-run support)
*   Configurable dark mode on all systems

## How to get it

Download binaries for Windows, macOS and Linux from the [releases](https://github.com/SysAdminDoc/RcloneBrowserNG/releases) page, or build from source (below).

## Build instructions

### Linux

1.  Install dependencies:
    *   **Debian/Ubuntu**: `sudo apt update && sudo apt -y install git g++ cmake make qt6-base-dev libgl1-mesa-dev` (Qt 5: `qtbase5-dev`)
    *   **Fedora**: `sudo dnf -y install git g++ cmake make qt6-qtbase-devel`
    *   **Arch/Manjaro**: `sudo pacman -Sy --noconfirm --needed git gcc cmake make qt6-base`
2.  `git clone https://github.com/SysAdminDoc/RcloneBrowserNG.git && cd RcloneBrowserNG`
3.  `mkdir build && cd build`
4.  `cmake ..`
5.  `make`
6.  `sudo make install`

### FreeBSD

1.  `sudo pkg install git cmake qt6-base` (or the qt5 packages)
2.  Then follow the Linux steps from step 2.

*Note: for rclone mounts to work you may need `sudo sysctl vfs.usermount=1` — see this rclone forum [thread](https://forum.rclone.org/t/failed-to-mount-fuse-fs-freebsd/7723/9).*

*Note: rclone does not support `mount` on OpenBSD/NetBSD, so the feature is disabled there.*

### macOS

1.  Install [Homebrew](https://brew.sh/) if you don't have it
2.  `brew install git cmake rclone qt@6`
3.  `git clone https://github.com/SysAdminDoc/RcloneBrowserNG.git && cd RcloneBrowserNG`
4.  `mkdir build && cd build`
5.  `cmake .. -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)`
6.  `make`
7.  Package with Qt libraries to create a self-contained app: `$(brew --prefix qt@6)/bin/macdeployqt build/rclone-browser.app`

### Windows

1.  Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (the "Desktop development with C++" workload is enough — Build Tools also work)
2.  Install [CMake](https://cmake.org/)
3.  Install Qt 6 (64-bit, MSVC) from the [Qt website](https://www.qt.io/download-open-source/) — these steps assume `C:\Qt`
4.  `git clone https://github.com/SysAdminDoc/RcloneBrowserNG.git && cd RcloneBrowserNG`
5.  `mkdir build && cd build`
6.  `cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release" -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64 .. && cmake --build . --config Release`
7.  `C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe --no-translations ".\build\Release\RcloneBrowser.exe"`
8.  `build\Release` now contains `RcloneBrowser.exe` and everything required to run it

## Portable vs standard mode

In standard mode configuration is stored in the usual per-user locations:

*   macOS:
    *   preferences: `~/Library/Preferences/com.rclone-browser.rclone-browser.plist`
    *   tasks file: `~/Library/Application Support/rclone-browser/rclone-browser/tasks.bin`
*   Linux/BSD:
    *   preferences: `~/.config/rclone-browser/rclone-browser.conf`
    *   tasks file: `~/.local/share/rclone-browser/rclone-browser/tasks.bin`
*   Windows:
    *   preferences: registry `HKEY_CURRENT_USER\Software\rclone-browser\rclone-browser`
    *   tasks file: `%LOCALAPPDATA%\rclone-browser\rclone-browser\tasks.bin`

To enable portable mode, create an `.ini` file next to the executable with the same name — e.g. for `RcloneBrowser.exe` create `RcloneBrowser.ini` (for a macOS bundle, next to the `.app`). For Linux AppImages create a directory named `<appimage>.config` next to the AppImage file, per the [AppImage portable-mode spec](https://docs.appimage.org/user-guide/portable-mode.html).

In portable mode all configuration lives next to the application, and the rclone binary and `rclone.conf` paths may be relative to the executable — so everything can live on a memory stick.

## History

This project keeps a long-running community tool alive. Martins Mozeiko wrote the original Rclone Browser; kapitainsky maintained a widely-used fork through 2020 with many fixes and features; both repositories then went dormant while rclone kept evolving and eventually broke them. RcloneBrowserNG picks up from the kapitainsky line with modern rclone and Qt 6 compatibility.
