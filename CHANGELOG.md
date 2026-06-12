# Change Log
## [Unreleased]
### Reliability & Data Safety
-   CHANGED: Export file lists now use `rclone lsjson --recursive --files-only` instead of regex-parsing `rclone lsl`; CSV output quotes fields safely and malformed JSON produces a visible error instead of silently dropping entries
-   CHANGED: Transfer progress now uses `--use-json-log` instead of regex-parsing `--stats` text output — eliminates the recurring "new rclone broke progress display" failure mode; per-file progress bars work reliably across all rclone versions
-   CHANGED: File listing now uses `rclone lsjson` instead of regex-parsing `lsd`/`lsl` text output — filenames with special characters (`[](){}`, colons, slashes, trailing spaces, Unicode) no longer silently disappear from the browser; one process per listing instead of two
-   FIXED: Drag and drop of multiple files now works — previously only single-file drops were accepted; all URLs are validated as local files
-   FIXED: Corrupt or incompatible `tasks.bin` no longer silently discards every saved task — the bad file is renamed aside (`tasks.bin.corrupt`) and a warning dialog explains what happened
-   FIXED: Streaming player and rclone cat processes are now parented to the main window — no orphaned processes on app close; player error path also cleans up the rclone pipe; stream cancel kills a hung player after 2 s
-   FIXED: Icon caches are bounded (defensive 1024-extension cap) to prevent unbounded memory growth in edge-case sessions
-   FIXED: Saved tasks could be lost — task file is now written atomically (QSaveFile), so a crash mid-write no longer wipes every saved task
-   FIXED: App silently closed at startup when `rclone version` failed for any reason other than a missing password (Win10/11 "auto-close on startup") — it now reports the actual rclone error and opens Preferences; cancelling the encrypted-config password prompt also keeps the app open
-   FIXED: Use-after-free crash when deleting a folder while file icons were still loading (Item destructor checked the parent's state instead of the child's)
-   FIXED: First directory listing sorted with uninitialized sort state (undefined behavior on first load)
-   FIXED: Crash when an action (refresh/rename/move/delete/mount/etc.) fired with nothing selected in the file tree or remotes list
-   FIXED: rclone listing failures now surface a warning with rclone's stderr instead of showing a silently empty folder
-   FIXED: Closing the app with multiple running jobs could skip cancelling some of them (layout mutated during iteration)
-   FIXED: "Dry run" followed by a validation error then "Run" silently ran the job as a dry run (sticky flag)
-   FIXED: A player command that fails to start now stops the rclone stream and reports it (previously undetectable — rclone kept piping into a dead process)
-   FIXED: Stray empty arguments passed to rclone when extra options contained multiple spaces; blank `--exclude` patterns from empty lines in the Export dialog
-   FIXED: "Drive shared with me" checkbox is now per-tab — two open Google Drive tabs with different Shared states no longer race each other through a global setting
-   FIXED: Transfer mode (Copy/Move/Sync) was never remembered between transfers (radio buttons missing from the settings writer)
-   FIXED: Clearing a multi-line option (e.g. exclude list) did not remove the previously saved value
-   FIXED: Long-running transfers, streams and mounts no longer grow the in-memory log unbounded (capped at 10k lines, old lines scroll away instead of the whole log being wiped)
-   FIXED: Transfer dialog leaked a JobOptions object on every plain (non-saved) run

### Security
-   SECURITY: `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` called early in main on Windows to prevent DLL planting from the application directory
-   SECURITY: rclone executable paths ending in `.bat` or `.cmd` are now rejected (BatBadBut, CVE-2024-24576)
-   SECURITY: Windows mounts now authenticate the rclone remote-control endpoint with a random per-mount credential — prevents CVE-2026-41176 (CVSS 9.2) and CVE-2026-49980 (CVSS 9.8) exploitation via unauthenticated loopback rc
-   SECURITY: Startup warns (once per version) when the detected rclone is older than 1.74.3, with a link to the affected advisories
-   SECURITY: macOS `rclone config` no longer writes a world-readable script with a fixed predictable name in shared `/tmp` (symlink/pre-creation hazard) — it now uses a unique, user-only file in the per-user temp dir
-   SECURITY: Deleting a saved task now asks for confirmation

### Build & Compatibility
-   FIXED: Installed identity now consistently uses Rclone Browser NG across the app display name, main window, Linux desktop entry, macOS bundle display name, Windows installer metadata, and CI metadata validation
-   NEW: WinFsp detection — mounting on Windows now checks for WinFsp and offers a download link if missing, instead of failing with a cryptic rclone error
-   FIXED: AppStream metainfo rewritten for NG identity (`io.github.sysadmindoc.rclonebrowserng`), installed to `share/metainfo/`, and embedded in AppImage — unblocks Flathub and distro packaging
-   FIXED: Qt 6 Windows/macOS builds did not compile — QtWinExtras `QtWin::fromHICON` replaced with `QImage::fromHICON`, missing Windows shell/COM headers included, `QFileInfo` explicit-constructor errors fixed, COM initialized on the icon worker thread
-   FIXED: Robust rclone version parsing — beta/suffixed versions (e.g. `1.67.0-beta…`) can no longer throw
-   FIXED: Version string read from VERSION file is trimmed (stray newline no longer corrupts the About box / update check)

### UX
-   NEW: Right-click a saved task to export it as a standalone .sh, .bat, or .ps1 script for OS schedulers
-   NEW: Confirmation dialog before deleting a saved task
-   NEW: Failed jobs auto-expand their details and output so the cause is immediately visible
-   NEW: Inverted (light) remote icons are picked from the effective palette, so OS-level dark themes (Windows 10/11, macOS Mojave+) get readable icons — not just the app's own dark mode
-   CHANGED: File-browser action buttons wrap to two rows — the window now fits laptop screens and tiling WMs (was forced to ~1230px minimum width)
-   CHANGED: Dark mode now follows the OS dark theme on Windows/Linux (Qt 6.5+) without needing the manual checkbox; the checkbox still works as an override
-   CHANGED: Dark mode covers disabled controls, placeholder text and tooltips; "Finished" status no longer renders black-on-dark; status colors are theme-safe in both modes
-   CHANGED: Rebranded About box, update check, and release links to RcloneBrowserNG
-   CHANGED: Clearer empty-jobs message; preferences icon-size and proxy options are proper radio groups; file sizes show one decimal (e.g. "1.5 G")
-   FIXED: GitHub update checks are now fully async — no GUI freeze at startup (was up to 20 s with two blocking QEventLoop calls); check-date is only recorded after a successful response so an offline launch retries next time
-   FIXED: Preferences showed "alternating row colours" and "dark mode" defaults that didn't match actual behavior
-   FIXED: Stream job mislabeled the player command as "Folder:"; assorted label typos ("transfering", "locaction")

## [2.0.0] - 2026-06-10
### Critical Fixes
-   FIXED: rclone v1.56+ output parsing — Size, Bandwidth, ETA, and transfer progress fields work again with modern rclone (broken since 2021)
-   FIXED: Qt 6 port — builds with both Qt 5.15+ and Qt 6, required for Wayland and modern Linux distros
-   FIXED: Qt 5.15 deprecated API compile errors (QProcess::start, QString::split, QRegExp removal)

### Bug Fixes
-   FIXED: Config button broken on modern Linux — added support for gnome-terminal 3.38+ (-- flag), kitty, alacritty, wezterm, foot, tilix, and other modern terminals
-   FIXED: DELETE command blocks entire GUI — now runs as async background job in the Jobs tab
-   FIXED: rclone keeps running after job cancellation — graceful SIGTERM with 5s timeout before SIGKILL
-   FIXED: Clipboard "copy command" broken with spaces in path — arguments now properly quoted
-   FIXED: Global lockfile blocks multi-user environments — per-user lock file names
-   FIXED: Mount multiple remotes fails — RC port derived from full path hash instead of first character
-   FIXED: UI freezes on failed fusermount unmount — 10s timeout with forced termination fallback
-   FIXED: Crash after closing stream then stream job — prevented double-close segfault
-   FIXED: Exclude filter empty lines passed as blank --exclude arguments
-   FIXED: Export output UTF-8 encoding on non-English Windows (Qt 6 default)
-   FIXED: Notification title shows proper "Rclone Browser" instead of "rclone-browser"

### Build System
-   CMake modernized: AUTOMOC/AUTOUIC/AUTORCC, dual Qt5/Qt6 discovery
-   C++17 standard on non-MSVC platforms
-   macOS deployment target raised to 11.0 (Apple Silicon / Qt 6 requirement)
-   Replaced QRegExp with QRegularExpression throughout
-   macOS: modernized dock icon API, replaced QtMac::fromCGImageRef

## [1.8.0][1.8.0] - 2020-02-17
-   NEW: http(s) proxy configuration for rclone
-   NEW: remotes icons size option selector
-   NEW: directories tree display for remotes
-   NEW: rclone extra default options for all operations (e.g. --fast-list)
-   NEW: added "Public Link" button to remote view
-   FIXED: option to show hidden files and folders was not always working as expected
-   FIXED: for sftp server default to home user directory (as normal sftp would do)
-   FIXED: an issue when on Windows local remote only allowed to browse drive C:
-   FIXED: problem using rclone and rclone.conf when path contained spaces
-   FIXED: bandwidth box on jobs tab is too small for fast connections
-   bunch of usual small tweaks and fixes

## [1.7.0][1.7.0] - 2019-11-27
-   NEW: built all releases with the latest Qt 5.13.2
-   NEW: changed Linux releases format to AppImage only
-   NEW: changed macOS release format to dmg image file
-   NEW: added installer for Windows releases - implemented using [Inno Setup](https://github.com/jrsoftware/issrc)
-   NEW: added Linux i386 release
-   NEW: changed macOS release compilation options to make it work on all macOS versions starting with 10.9
-   NEW: added portable mode for macOS and Linux
-   NEW: on Linux multiple terminals are tried for rclone config ($TERMINAL then gnome-terminal followed by xfce4-terminal, xterm, x-terminal-emulator and konsole)
-   NEW: enabled Qt HighDpiScaling - should help people with high DPI monitors
-   NEW: added dark mode - configurable via preferences or system setting (newer macOS) - thank you @noaione for initial PR
-   changed preferences window - added tabs to create more space for new options
-   fixed Windows portable mode
-   fixed mount/unmount on FreeBSD
-   disabled mount on OpenBSD and NetBSD (as not supported by rclone)
-   updated build and install for Linux - now all files will be installed in /usr/local root
-   fixed possible crashes when old rclone is used (with different version information output)
-   fixed an issue with long file names leading sometimes to inaccurate transfer progress bar display
-   added additional info to file progress bar tooltip - individual file stats
-   changed program icon
-   bunch of usual small tweaks and fixes

## [1.6.0][1.6.0] - 2019-10-27
-   fixed Windows mount/unmount (requires rclone v1.50+)
-   Rclone Browser checks now for used rclone version (mount is disabled in Windows if rclone <v1.50)
-   added default download/upload folders - configurable in settings
-   add default download/upload extra options - configurable in settings
-   added available updates' notifications for both Rclone Browser and rclone - can be turned on/off in settings
-   all mount options are configurable via settings - generic "rclone mount remote local" is used without any options specified
-   default mount option (in settings) is "--vfs-cache-mode writes"
-   Google Drive with "shared with me" option on is always mounted as read-only
-   Windows deployment includes now all required runtime files for users without MSVCR installed
-   added ftp, MS Azureblob and Google Photos remote icons
-   modified main application window status bar to save space
-   released binary for Windows 32 bits
-   released binary for armhf 32 bits - for Raspberry Pi running raspbian
-   bunch of usual small tweaks and fixes

## [1.5.3][1.5.3] - 2019-10-24
-   Windows only update - include all required runtime dll files

## [1.5.2][1.5.2] - 2019-09-27
-   code cleanup - clean compilation with -Werror enabled, GCC8 compilation fixed
-   add tooltips showing rclone options used to all transfer window options
-   Google "drive shared with me" caused multiple of issues - now all should work
-   as always small cosmetic UI improvements - still plenty to do but core functionality was first

## [1.5.1][1.5.1] - 2019-09-25
-   after task edit initiated by double click main window does not get proper focus back and subsequent Run click might lead to wrong task execution. For time being I disable double click edit - until proper fix is produced.

## [1.5][1.5] - 2019-09-25
-   tasks - jobs can be saved/edited/run/deleted. No need creating the same job again and again.
-   on Google drive DriveSharedWithMe can be mounted to local filesystem
-   DriveSharedWithMe checkbox is now disabled for non Google destinations - it is Google only feature and turning it on for other destinations does not make sense - could even crash the browser.
-   verbose option is now always on and has been removed from UI - which means that stats will be always displayed. No more wondering how long it is going to take for some long job to finish.
-   fixed an issue with local remote on Windows when local drive content was not properly displayed
-   replaced remote Amazon icon with generic S3 one. S3 became name on its own and almost de-facto standard in cloud access used by many rclone supported destinations
-   new application logo

## [1.4.1][1.4.1] - 2019-09-18
-   small GUI tweaks to make all progress fields always visible (they were too small for large transfers) and adjust some screen sizes to make all GUI elements visible
-   update all builds with latest Qt (5.13.1)

## [1.4][1.4] - 2019-08-23
-   Fix compliation errors and update all builds with latest Qt (5.13)
-   Fix Config button command
-   Further fix and tweak progress display. Add ETA and Total Size fields
-   Fix remotes icons display
-   Add sftp icon
-   Fix progress display for rclone > 1.37 (by DinCahill)
-   Add a Public Link option to the right-click menu (by DinCahill)
-   Add preference: Show hidden files and folders (by DinCahill)
-   Add Mega icon (by DinCahill)
-   Refresh when Shared is toggled (by DinCahill)
-   Disable Upload button for Shared (by DinCahill)
-   Support for shared Google Drive files. Enable the checkbox when you open a remote, and all rclone commands will be passed --drive-shared-with-me (by DinCahill)
-   Set cache mode for mounts (by DinCahill)
-   Fixed missing leading / in path (required for some SFTP servers) (by DinCahill)

## [1.2][1.2] - 2017-03-11
-   Calculate size of folders, issue #4
-   Copy transfer command to clipboard, issue #20
-   Support custom .rclone.conf location, #21
-   Export list of files, issue #27
-   Bugfix for folder refresh not working after rename, issue #30
-   Remember empty text fields in transfer dialog, issue #32
-   Error message when too old rclone version is selected
-   Support portable mode, issue #28
-   Create .deb packages, issue #26

## [1.1][1.1] - 2017-01-31
-   Added `--transfer` option in UI, issue #1
-   Supports encrypted `.rclone.conf` configuration file, issue #2
-   Fixed crash when canceling active stream
-   Added ETA tooltip for transfer progress bars
-   Allow to specify extra arguments for rclone, issue #7
-   Fix for browsing Hubic remotes, issue #10
-   Support high-dpi mode for macOS

## [1.0.0][1.0.0] - 2017-01-29
-   Allows to browse and modify any rclone remote, including encrypted ones
-   Uses same configuration file as rclone, no extra configuration required
-   Simultaneously navigate multiple repositories in separate tabs
-   Lists files hierarchically with file name, size and modify date
-   All rclone commands are executed asynchronously, no freezing GUI
-   File hierarchy is lazily cached in memory, for faster traversal of folders
-   Allows to upload, download, create new folders, rename or delete files and folders
-   Can process multiple upload or download jobs in background
-   Drag & drop support for dragging files from local file explorer for uploading
-   Streaming media files for playback in player like mpv or similar
-   Mount and unmount folders on macOS and GNU/Linux
-   Optionally minimizes to tray, with notifications when upload/download finishes

[1.8.0]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.8.0
[1.7.0]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.7.0
[1.6.0]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.6.0
[1.5.3]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5.3
[1.5.2]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5.2
[1.5.1]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5.1
[1.5]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5
[1.4.1]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.4.1
[1.4]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.4
[1.2]: https://github.com/mmozeiko/RcloneBrowser/releases/tag/1.2
[1.1]: https://github.com/mmozeiko/RcloneBrowser/releases/tag/1.1
[1.0.0]: https://github.com/mmozeiko/RcloneBrowser/releases/tag/1.0.0
