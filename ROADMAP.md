# ROADMAP — RcloneBrowserNG

> Community continuation of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser) (itself a fork of [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser)).  
> Both upstream repos are abandoned (last activity: Dec 2020 / 2018 respectively).  
> This roadmap consolidates **every** open issue, unmerged PR, and community request from the entire upstream/fork ecosystem.

Status legend: `[ ]` = open, `[x]` = done, `[-]` = won't fix / deferred

---

## P0 — Critical (app broken with modern rclone)

These are blocking issues that make the app non-functional with any rclone released after July 2021.

- [ ] **Fix rclone v1.56+ output parsing** — Size, Bandwidth, Transferred, ETA, and Remaining Time fields are all permanently blank. The progress regex in `job_widget.cpp` doesn't match rclone's new output format. This is the single most impactful bug (~15 duplicate reports). Three unmerged PRs exist: kapitainsky#225, #212, #246. The downer08tutti/RcloneBrowser2 fork has a comprehensive backward-compatible fix.  
  _Sources: kapitainsky #174, #182, #198, #227, #231, #142, #141; mmozeiko #128, #121, #117, #150, #130, #46, #45_

- [ ] **Fix Qt 5.15 / Qt 6 deprecated API compile failures** — `QProcess::start`, `QString::split`, and other deprecated calls cause build errors on modern systems. Unmerged PR: kapitainsky#126.  
  _Sources: kapitainsky #248, #223, #245; mmozeiko #96_

- [ ] **Port to Qt 6** — Qt 5 is EOL. A working Qt6 port was demonstrated in kapitainsky#244 (user shared patches). Required for Wayland support on Linux.  
  _Sources: kapitainsky #244_

---

## P1 — High Priority Bugs

Bugs that affect core functionality for existing users.

- [ ] **Config button does nothing / opens empty terminal** — Reported on Windows 11, macOS, Ubuntu 18/20.04, and BSD. The terminal detection logic and rclone config invocation are both broken on modern systems.  
  _Sources: kapitainsky #239, #209, #163, #172, #145, #104, #189, #175; mmozeiko #136, #57_

- [ ] **DELETE command blocks GUI (not async)** — Deleting files freezes the entire interface until rclone finishes.  
  _Sources: mmozeiko #104_

- [ ] **Exclude option never applied to transfers** — The exclude filter set in the transfer dialog is silently ignored.  
  _Sources: kapitainsky #179_

- [ ] **rclone keeps running after job cancellation** — Canceling a job in the UI doesn't kill the rclone subprocess.  
  _Sources: kapitainsky #117_

- [ ] **Duplicates on Google Drive silently both deleted** — When duplicate filenames exist, delete operations remove all copies without warning.  
  _Sources: kapitainsky #238_

- [ ] **Move operation moves files, not the folder (Dropbox)** — Move on Dropbox extracts files from the folder instead of moving the folder itself.  
  _Sources: kapitainsky #171_

- [ ] **Global lockfile blocks multi-user environments** — Single system-wide lock file prevents multiple users from running the app simultaneously. Unmerged fix: kapitainsky PR#173 (per-user lockfile).  
  _Sources: kapitainsky #162_

- [ ] **Mount multiple remotes fails** — Remote control server port conflict when mounting more than one remote simultaneously.  
  _Sources: kapitainsky #180_

- [ ] **Mount drops during large transfers** — Mounts randomly disconnect mid-transfer.  
  _Sources: kapitainsky #158_

- [ ] **Special characters in paths break navigation** — `[ ] ( )`, forward slashes, trailing spaces, and colons in folder/file names cause crashes or broken browsing.  
  _Sources: kapitainsky #130, #107, #160; mmozeiko #42, #82, #77, #83; PR#123_

- [ ] **Clipboard "copy command" broken with spaces in path** — Copied rclone command doesn't properly quote paths containing spaces.  
  _Sources: mmozeiko #63; kapitainsky #226_

- [ ] **Crashes after heavy use** — Memory leak or resource exhaustion causing crashes during long sessions.  
  _Sources: kapitainsky #120_

- [ ] **AppImage immediate exit on Linux Mint** — Binary starts and immediately closes without error.  
  _Sources: kapitainsky #168_

- [ ] **Win10/11 auto-close on startup** — Application closes immediately after launch on some Windows configurations.  
  _Sources: kapitainsky #159_

- [ ] **UI freezes on failed fusermount unmount** — If unmount fails, the entire GUI hangs.  
  _Sources: mmozeiko #89_

- [ ] **Drag & drop broken** — Multiple files on Windows fail; Dropbox drag & drop broken while Google Drive works.  
  _Sources: mmozeiko #9, #143_

- [ ] **Google Photos lists only empty folders** — Google Photos remote shows folder structure but no files.  
  _Sources: kapitainsky #167_

- [ ] **Task state not persisted** — Saved tasks sometimes lost between sessions.  
  _Sources: kapitainsky #150_

- [ ] **Crash on New Folder creation** — macOS crash when creating new folders.  
  _Sources: mmozeiko #55_

- [ ] **Crash after closing stream then stream job** — Segfault when closing a streaming session.  
  _Sources: mmozeiko #35_

---

## P2 — UI / Display Bugs

- [ ] **Window too large / can't resize below 1232px** — Forced minimum width makes the app unusable on small screens, laptops, and tiling WMs. Unmerged fix: kapitainsky PR#166 (second button row for 1024x768).  
  _Sources: kapitainsky #228, #156_

- [ ] **Status bar overlaps breadcrumb** — Layout collision in the main window.  
  _Sources: kapitainsky #83_

- [ ] **Job widget UI glitches with many small files** — Progress display breaks when sending large batches of small files.  
  _Sources: kapitainsky #169_

- [ ] **Notification title shows "rclone-browser" instead of "Rclone Browser"** — Inconsistent branding in system notifications.  
  _Sources: kapitainsky #84_

- [ ] **Glitched tooltips** — Tooltip rendering artifacts.  
  _Sources: kapitainsky #115_

- [ ] **Font rendering on Mac 4K displays** — Wonky text rendering on Retina/HiDPI.  
  _Sources: mmozeiko #74_

- [ ] **Export output not UTF-8 on non-English Windows** — File list export uses system codepage instead of UTF-8.  
  _Sources: kapitainsky #77_

---

## P3 — Most-Requested Features

Features requested across multiple repos, forks, and forum threads. Sorted by community demand.

### Scheduling & Automation
- [ ] **Scheduled tasks / cron** — Built-in scheduler for recurring sync/copy/mount jobs. The single most requested feature across the ecosystem.  
  _Sources: kapitainsky #200, #177, #73; docker #34; rclone forum_

- [ ] **Auto-mount remotes on launch** — Option to automatically mount configured remotes when the app starts.  
  _Sources: kapitainsky #146, #250, #60_

- [ ] **Start minimized to system tray** — Launch silently to tray with auto-mount, no visible window.  
  _Sources: kapitainsky #208, #60_

- [ ] **Auto-update capability** — Check for and apply updates to both RcloneBrowserNG and rclone.  
  _Sources: kapitainsky #249, #195_

### Task Management
- [ ] **Job queue manager** — Sequential job execution instead of all-concurrent. Configurable concurrency limit.  
  _Sources: kapitainsky #204, #109; mmozeiko #6, #14_

- [ ] **Restart / retry failed jobs** — Manual or automatic retry of failed transfers.  
  _Sources: kapitainsky #118; mmozeiko #44_

- [ ] **Pause running jobs** — Pause and resume active transfers.  
  _Sources: mmozeiko #71_

- [ ] **Editable saved tasks** — Allow editing source, destination, and options on saved tasks.  
  _Sources: kapitainsky #87, #143_

- [ ] **Multi-select tasks and batch run** — Select and run multiple saved tasks at once.  
  _Sources: kapitainsky #98, #80_

- [ ] **Task list sorting and search** — Filter and sort the saved tasks list.  
  _Sources: kapitainsky #21_

- [ ] **"Copy command" button in Tasks view** — Copy the full rclone command for a saved task.  
  _Sources: kapitainsky #176, #123_

- [ ] **Confirmation dialog before running task** — "Are you sure?" prompt for destructive tasks.  
  _Sources: kapitainsky #144_

- [ ] **Remember extra options per task** — Task-specific rclone flags aren't persisted.  
  _Sources: kapitainsky #136_

- [ ] **Convert tasks.bin to human-readable format** — Replace binary task storage with JSON/XML.  
  _Sources: kapitainsky #143_

- [ ] **CLI interface to run saved tasks** — Run saved tasks from the command line without opening the GUI.  
  _Sources: kapitainsky #181_

### File Browser / Navigation
- [ ] **Manual path entry / editable address bar** — Type or paste a path directly instead of clicking through folders.  
  _Sources: kapitainsky #16, #148; mmozeiko #110_

- [ ] **Dual-pane interface** — Side-by-side local/remote or remote/remote browsing.  
  _Sources: kapitainsky #71; mmozeiko #80, #98_

- [ ] **Multi-file/folder selection** — Select multiple items for batch operations.  
  _Sources: kapitainsky #42, #121; mmozeiko #84_

- [ ] **Search / filter files** — Search within the current remote/directory.  
  _Sources: mmozeiko #64_

- [ ] **Drag & drop to upload** — Drag files from file explorer directly into a remote folder.  
  _Sources: mmozeiko #108; kapitainsky #28_

- [ ] **Sort by recent / custom sort in Google Drive** — Sort file list by last modified.  
  _Sources: kapitainsky #122_

- [ ] **File preview / double-click to open** — Preview or open files directly from the browser.  
  _Sources: mmozeiko #101_

- [ ] **Show hidden files toggle** — Toggle visibility of dotfiles.  
  _Sources: mmozeiko #86_

- [ ] **Browse Google Drive trash** — Access the trash folder for recovery.  
  _Sources: mmozeiko #37_

- [ ] **Tiles/icon view for remotes list** — Alternative to list view when many remotes are configured.  
  _Sources: kapitainsky #76_

### Advanced Operations
- [ ] **Bisync / bidirectional sync** — GUI support for rclone's bisync command.  
  _Sources: kapitainsky #243; mmozeiko #106_

- [ ] **Remote-to-remote transfers** — Copy/sync directly between two remotes without local intermediary.  
  _Sources: mmozeiko #27_

- [ ] **Exclude files UI for sync** — Visual exclude/include filter builder in the transfer dialog.  
  _Sources: kapitainsky #252_

- [ ] **Multi-file selection auto-generates --include-from** — Selecting multiple files builds a filter file automatically.  
  _Sources: kapitainsky #188_

- [ ] **Support for rclone "check" command** — Verify remote contents match local.  
  _Sources: mmozeiko #39; kapitainsky #62_

- [ ] **Support for rclone "serve"** — GUI for rclone's serve HTTP/FTP/WebDAV.  
  _Sources: kapitainsky #129_

- [ ] **Dedupe command** — GUI interface for rclone dedupe.  
  _Sources: kapitainsky #196_

- [ ] **Remote URL upload (copyurl)** — Download a URL directly to a remote.  
  _Sources: kapitainsky #199_

- [ ] **Public link generation (rclone link)** — Generate shareable public links.  
  _Sources: mmozeiko #111_

- [ ] **Use trash on delete (Google Drive)** — Move to trash instead of permanent delete.  
  _Sources: mmozeiko #73_

### Mount Enhancements
- [ ] **Remember last used mountpoint** — Persist mount directory between sessions.  
  _Sources: kapitainsky #215_

- [ ] **Copy/export mount command** — Show the full mount command for external use.  
  _Sources: kapitainsky #194_

- [ ] **Map as network drive on Windows** — Mount as a Windows network drive letter.  
  _Sources: kapitainsky #210_

- [ ] **Extended mount UI** — Directory navigation, cache settings, read-only toggle in mount dialog.  
  _Sources: kapitainsky #52_

### Monitoring & Logging
- [ ] **Real-time error log** — Live error log panel during transfers.  
  _Sources: kapitainsky #233, #134_

- [ ] **Global upload/download statistics** — Cumulative transfer counter across all jobs.  
  _Sources: kapitainsky #111_

- [ ] **ETA field in transfer list** — Show estimated time remaining per transfer.  
  _Sources: kapitainsky #112_

### Miscellaneous
- [ ] **i18n / multi-language support** — Internationalization framework with translations.  
  _Sources: kapitainsky #138, #47; rclone forum (Portuguese, others)_

- [ ] **Hide/group encrypted remotes** — Hide the underlying unencrypted remote when a crypt remote exists.  
  _Sources: kapitainsky #206, #178_

- [ ] **Multiple config file support** — Switch between different rclone.conf files.  
  _Sources: kapitainsky #19_

- [ ] **Portable version** — Self-contained build with no registry/install requirements.  
  _Sources: kapitainsky #222_

- [ ] **Reopen existing instance instead of error** — Clicking the exe when already running should focus the existing window.  
  _Sources: kapitainsky #214_

- [ ] **Proxy support in GUI** — HTTP/SOCKS proxy configuration exposed in preferences (partially exists since v1.8.0 but incomplete).  
  _Sources: mmozeiko #88, #41_

- [ ] **Data usage per remote** — Show storage consumption per configured remote.  
  _Sources: mmozeiko #65_

- [ ] **Sound notification on job complete** — Audio alert when transfers finish (macOS/cross-platform).  
  _Sources: kapitainsky #61_

- [ ] **Global bandwidth limit control** — UI slider for upload/download speed cap.  
  _Sources: kapitainsky #116_

- [ ] **Middle-click tab to close** — Standard tab behavior.  
  _Sources: mmozeiko #56_

- [ ] **Default exclude list** — Configurable global exclude patterns.  
  _Sources: mmozeiko #75_

- [ ] **Default destination folder** — Remember and pre-fill download destination.  
  _Sources: mmozeiko #122_

- [ ] **Streaming: show filename in player** — Pass filename metadata to VLC/mpv instead of `fd://0`.  
  _Sources: mmozeiko #120_

- [ ] **Create and save connection profiles** — Named profiles with different rclone configs/options.  
  _Sources: mmozeiko #102_

---

## P4 — Platform & Compatibility

- [ ] **Apple Silicon (arm64) native build** — No official arm64 macOS binary exists. The downer08tutti fork provides this. Raise `CMAKE_OSX_DEPLOYMENT_TARGET` to 11.0+.  
  _Sources: kapitainsky #161, #240_

- [ ] **Wayland support (Linux)** — Requires Qt 6 port (see P0).  
  _Sources: kapitainsky #244_

- [ ] **ARM64 Linux builds** — Native aarch64 AppImage/packages.  
  _Sources: kapitainsky #207; docker #17_

- [ ] **ppc64le build support** — Unmerged PR: kapitainsky#152.  
  _Sources: kapitainsky #152_

- [ ] **FreeBSD headless compilation** — Build without Qt GUI for server use.  
  _Sources: kapitainsky #247_

- [ ] **Flatpak packaging** — Distribute via Flathub.  
  _Sources: mmozeiko #112_

- [ ] **Snap packaging** — Distribute via Snap Store.

- [ ] **Homebrew formula fix** — Current install script 404s (needs `.sh` suffix). Unmerged PR: kapitainsky#232.  
  _Sources: kapitainsky #232_

- [ ] **Update CI from Travis/AppVeyor to GitHub Actions** — Travis CI free tier is dead. Move to GHA for Linux, macOS, and Windows builds.

- [ ] **Code signing for releases** — Sign binaries and provide checksums.  
  _Sources: rclone forum_

---

## P5 — Stretch Goals

Features that would differentiate this fork but require significant architecture work.

- [ ] **Web UI mode** — Serve the GUI over HTTP for headless/NAS/Docker deployments.  
  _Sources: mmozeiko #51; rclone forum_

- [ ] **Remote rclone server mode** — Connect to an rclone `rcd` instance running on another machine.  
  _Sources: mmozeiko #38; kapitainsky remote control mentions_

- [ ] **Docker container with web access** — Pre-built container image (successor to romancin/rclonebrowser-docker).  
  _Sources: docker repo, 86 stars_

---

## Competitive Landscape

For context — active alternatives as of June 2026:

| Project | Stars | Stack | Status | Key Differentiator |
|---------|-------|-------|--------|--------------------|
| [rclone-ui](https://github.com/rclone-ui/rclone-ui) | 2,065 | TypeScript/Tauri | Active | Modern GUI, cron, dual-panel, `rcd` control, WinGet/Homebrew/Flathub |
| [rclone-webui-react](https://github.com/rclone/rclone-webui-react) | 1,562 | JavaScript | Low activity | Official rclone web UI, bundled with `rclone rcd` |
| [RcloneShuttle](https://github.com/pieterdd/RcloneShuttle) | 149 | Rust/GTK4 | Active | Linux-native GTK4, upload-focused |
| [MinorMole/Portable](https://github.com/MinorMole/RcloneBrowser-Portable) | 97 | VB.NET | Active | Portable Windows launcher wrapping old RcloneBrowser |
| [romancin/docker](https://github.com/romancin/rclonebrowser-docker) | 86 | Docker/noVNC | Active | Containerized GUI via browser |

RcloneBrowserNG's niche: **lightweight native Qt desktop app** — faster startup, lower memory, no Electron/Tauri runtime, no browser tab. The value proposition is a well-maintained, modern C++/Qt6 app that "just works" with current rclone on all desktop platforms.

---

## Source Cross-Reference

All items trace back to public GitHub issues/PRs:

- `kapitainsky #NNN` = [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser/issues)
- `mmozeiko #NNN` = [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser/issues)
- `docker #NNN` = [romancin/rclonebrowser-docker](https://github.com/romancin/rclonebrowser-docker/issues)
- `PR#NNN` = pull request (repo indicated in context)
- `rclone forum` = [forum.rclone.org](https://forum.rclone.org) discussion threads
