# Change Log
## [Unreleased]
### Reliability & Data Safety
-   NEW: Bisync conflict-resolution options — when Bisync is selected in the transfer dialog, a conflict-resolution dropdown appears with strategies (newer/older/larger/smaller/path1/path2); the chosen strategy is passed via `--conflict-resolve`
-   NEW: Backup-dir retention policy — the transfer dialog now has a "Backup dir" field supporting `{date}` placeholders for auto-dated snapshot folders, plus a "Retain backups" count to limit how many snapshots are kept
-   NEW: Bandwidth timetable editor — a "..." button next to the bandwidth field opens a table editor for rclone's time-of-day bandwidth syntax; add/remove rows, see a live preview of the generated `--bwlimit` flag
-   CHANGED: Tasks now stored as human-readable JSON (`tasks.json`) instead of binary `tasks.bin`. Existing binary task files are automatically migrated on first load. The JSON format is indented and includes all task fields for easy hand-editing and version control.
-   NEW: Cross-remote search — File > Search Remotes fans out `rclone lsjson -R --include` across all configured remotes, streaming matches into a sortable results table with remote, path, size, and modification time; cancellable; double-click a result to open its location
-   NEW: Quick bandwidth snail toggle — a "Slow/Full" button in the status bar instantly drops all new transfers to 128K or restores full speed with one click
-   NEW: cryptcheck mode in folder compare — the Compare Folders dialog now has a "Use cryptcheck" checkbox for verifying crypt remotes against their plaintext source
-   NEW: Support bundle now includes `--dump curl` usage tip for capturing HTTP requests as curl commands in bug reports
-   NEW: Path bookmarks — right-click any item in the remote browser to "Bookmark this path"; bookmarks appear in the File > Bookmarks menu for one-click access to deep remote paths
-   NEW: File properties dialog — right-click a file and choose Properties to see size, modification time, MIME type, all available hashes (including BLAKE3/XXH3/XXH128), and backend metadata via `rclone lsjson --stat --hash -M`
-   NEW: Transfer staging queue — an "Enqueue" button in the transfer dialog adds operations to a staging list on the Tasks tab for batch review; "Run All" executes all staged transfers at once, "Clear" discards them
-   NEW: Dual-pane interface — a "Dual Pane" button opens a side-by-side split view with two remote browsers in a QSplitter; pick the left and right remotes (or local filesystem) for Commander-style browsing
-   NEW: Native OS task scheduling — the #1 most requested feature. Select a saved task and click Schedule to install a native OS scheduled task (Windows Task Scheduler, crontab on Linux, launchd on macOS) that runs the task via `--run-task` without the app being open. Supports 15m/30m/hourly/daily/weekly intervals with configurable start time. Unschedule removes the OS entry.
-   NEW: Serve management — right-click a folder to start `rclone serve` with protocol selection (HTTP, WebDAV, FTP, DLNA, S3, NFS) and configurable listen address
-   NEW: SOCKS proxy support — the proxy settings now include a SOCKS proxy field (socks5://host:port) that sets `ALL_PROXY` for rclone, completing the HTTP/HTTPS/SOCKS proxy configuration
-   NEW: Visual exclude filter builder — the transfer dialog's exclude area now has quick-add buttons for common patterns (temp files, OS junk, Git, Node modules, hidden files) that append to the exclude list
-   NEW: Extended mount dialog — the mount action now shows a proper dialog with mount point input, VFS cache mode picker (off/minimal/writes/full), and a read-only toggle; settings persist across sessions
-   NEW: Remote-to-remote transfers — right-click any file or folder and choose "Copy to Remote..." to copy directly between two remotes without downloading locally
-   NEW: Job queue manager — a configurable "Max concurrent transfers" setting in Preferences (default: unlimited) queues excess transfers and starts them as slots free up
-   NEW: Pause and resume running transfers — a Pause button on each job suspends the rclone process (NtSuspendProcess on Windows, SIGSTOP on Unix) and shows a "Paused" status; click again to resume
-   NEW: Live throughput sparkline per job — each running transfer shows a 60-sample speed-over-time graph in the details pane, drawn with the accent color and gradient fill
-   NEW: Bisync (bidirectional sync) GUI — a Bisync radio button in the transfer dialog runs `rclone bisync` between source and destination; disabled when rclone < 1.58
-   NEW: CLI interface to run saved tasks — `--run-task "My Task"` runs a saved task headlessly without opening the GUI and exits with rclone's exit code; `--list-tasks` prints all saved task names
-   NEW: Tiles/icon view for remotes list — a toggle button switches between list and tile/icon grid layout; the preference persists across sessions
-   NEW: Browse Google Drive trash — a Trash button on Google Drive tabs lists trashed files with sizes via `--drive-trashed-only`
-   NEW: Multi-file download auto-generates `--include` filters — selecting multiple files and clicking Download copies the parent folder with `--include` flags for each selected file
-   NEW: Auto-mount remotes on launch — right-click a remote and enable "Auto-mount on launch"; the remote mounts automatically on startup after the remote list loads
-   NEW: Start minimized to system tray — a preference and `--minimized`/`--tray` command-line flags let the app launch hidden in the tray; useful with auto-start at login
-   NEW: Multiple config file support — File > Switch Config menu lets you add rclone.conf files and switch between them; the active config is checkmarked, and switching reloads the remote list immediately
-   NEW: File filter in remote browser — Ctrl+F opens a filter bar above the tree view that hides non-matching files by name within the current directory listing
-   NEW: Dedupe GUI — right-click a folder in the remote browser to run `rclone dedupe` with a mode picker (skip, first, newest, oldest, largest, smallest, rename)
-   NEW: Google Drive deletes now use trash — delete operations on Google Drive remotes automatically include `--drive-use-trash` so files go to Google Drive's trash instead of being permanently deleted
-   NEW: Retry failed jobs — failed transfers show a Retry button that re-runs the same transfer with the original arguments
-   NEW: Storage usage per remote — right-click a remote in the list to see total/used/free/trash space via `rclone about --json`
-   NEW: Remote URL upload (`copyurl`) — right-click a folder in the remote browser to download a file from any URL directly to that remote path via `rclone copyurl`
-   NEW: Global bandwidth limit control — an editable field in the status bar sets a persistent `--bwlimit` that applies to all new transfers when the per-job bandwidth is empty
-   NEW: Real-time error log panel — a collapsible "Error Log" pane on the Jobs tab streams errors and warnings from all running transfers, mounts, and streams as they happen
-   NEW: Default exclude patterns — a multi-line field in Preferences lets you set global exclude patterns (e.g. `*.tmp`, `.DS_Store`) that apply to all transfers automatically
-   NEW: Global transfer statistics — a persistent counter in the status bar shows cumulative bytes, files, and jobs across all sessions
-   NEW: Auto-hide encrypted remote backends — when a crypt remote exists, the underlying unencrypted remote is automatically hidden from the remotes list via `rclone config dump` parsing
-   NEW: Multi-select and batch run for saved tasks — Shift/Ctrl-click to select multiple tasks, then Run or Dry Run all at once; batch delete also supported
-   NEW: Saved tasks list now has a search/filter field that matches task name, source, and destination paths
-   NEW: Streaming now passes the filename to the player window title — mpv gets `--force-media-title` and VLC gets `--meta-title` so the player shows the actual file name instead of `fd://0` or `stdin`
-   NEW: Saved upload tasks can watch a local source folder and rerun after debounced filesystem changes, with active watches rebuilt on restart and pause/resume controls in the tray menu
-   NEW: Remote files can be opened for edit from the browser context menu; the app downloads to a temporary session file, watches saves, prompts on remote conflicts, and re-uploads changes
-   NEW: Remote browser paths now render as clickable breadcrumbs with sibling-folder dropdowns, plus an editable path mode via Ctrl+L, empty-bar click, Enter, and Escape
-   NEW: Folder comparison view runs `rclone check --combined`, filters matched/different/missing/error rows, and queues selected copy/delete repair jobs from the results
-   NEW: Job history now records completed transfer runs with timing, bytes, files, errors, and exit status; File and tray menus expose the history, and the tray icon distinguishes idle, active, and failed states
-   CHANGED: Dropped Qt 5 dual support — Qt 6.4 is now the minimum; all Qt5 ifdefs, QtWinExtras/QtMacExtras imports, High-DPI scaling workarounds, and the CI Qt5 matrix leg are removed
-   NEW: Notification webhooks — saved tasks can POST a JSON status payload (task name, status, error) to Discord, Gotify, or any webhook URL on job completion; includes a Discord-compatible `content` field
-   NEW: Remote context menu with "Test Connection" (bounded lsjson probe) and "Duplicate Remote" (rclone config copy); right-click a remote in the list to access both
-   NEW: OAuth token-expiry detection — when a remote listing fails with a token-expired or unauthorized error, the app offers a one-click "Reconnect" button that runs `rclone config reconnect` in a terminal to re-authenticate
-   NEW: Dry-run diff preview in the transfer dialog — a "Preview Changes" button runs the transfer with --dry-run and shows which files would be added, changed, or deleted inline before committing; JSON log output is parsed into readable messages
-   NEW: Redacted diagnostics and support bundle — Help > Copy Diagnostics copies environment/capability info to the clipboard; Help > Save Support Bundle writes a full report including recent job/mount output with secrets (passwords, tokens, rc credentials) automatically redacted
-   NEW: Runtime capability and dependency gates — a `RcloneCapabilities` helper detects rclone version, config path, Qt version, mount backend, and feature support (--name-transform, --list-cutoff, bisync, job/batch); the About dialog shows environment info; Help > Copy Diagnostics copies a full summary to the clipboard for bug reports; the transfer dialog disables --name-transform when rclone < 1.74
-   NEW: Pre/post job hooks — saved tasks can run a shell command before and/or after the transfer; a failing pre-command prompts to continue or abort; post-commands run detached after job completion
-   NEW: Multi-file selection in the remote file browser — Shift-click and Ctrl-click select multiple files and folders; batch delete works on all selected items; single-item actions (rename, move, mount, stream, link) are disabled when multiple items are selected
-   NEW: Transfer dialog exposes rclone's `--name-transform` flag (rclone >= 1.74) for file name transformations such as lowercase conversion or regex replacements
-   NEW: Heartbeat monitoring integration — saved tasks can have an optional URL (Healthchecks.io, ntfy.sh, or any HTTP endpoint) that is pinged on job completion; success sends GET to the URL, failure sends GET to URL/fail
-   CHANGED: Directory listings now use streaming JSON parsing — each `lsjson` object is parsed incrementally as data arrives instead of accumulating the entire response and parsing it as one block; this eliminates peak memory duplication on directories with hundreds of thousands of entries
-   CHANGED: RcloneRcEngine runtime requests (job status, stats, stop) now use async `QNetworkReply` signal/slot completion instead of blocking nested `QEventLoop` — eliminates GUI freezes and reentrancy risks during concurrent RC-based transfers; synchronous event loop retained only for the one-time rcd startup ping
-   CHANGED: Saved tasks now use a versioned task-store header with task count; legacy headerless `tasks.bin` files are loaded, migrated forward, and rewritten automatically while corrupt/newer schemas still fail safely
-   NEW: Added an optional Windows Credential Manager mode for encrypted `rclone.conf` passwords; rclone now receives the password through `--password-command` and a console helper instead of inherited `RCLONE_CONFIG_PASS`
-   NEW: Added a generated New Remote picker backed by `rclone rc --loopback config/providers`, so the creation flow lists the backend types supported by the installed rclone instead of relying on a stale hardcoded list
-   NEW: Added a lazy `RcloneRcEngine` that runs transfer jobs through a long-lived authenticated `rclone rcd` with `core/command`, `core/stats`, and `job/stop`; legacy process spawning remains the fallback
-   FIXED: Remote New Folder and Rename destination paths now use rclone-style path joining instead of `QDir::filePath`, avoiding macOS/remote path edge cases
-   FIXED: Google Photos album folders now use recursive `lsjson --files-only` listing so album contents appear instead of empty folders
-   FIXED: Linux/AppImage portable startup no longer builds config and lock paths from an empty `XDG_CONFIG_HOME`; AppImage portable mode now matches `$APPIMAGE.config` explicitly
-   FIXED: Startup now removes stale single-instance lock files left by crashed or force-killed sessions and reports the owning pid for real live locks
-   FIXED: Remote browser path state now preserves `lsjson` paths and special punctuation/trailing spaces instead of rebuilding every child path with `QDir::filePath`
-   FIXED: Mount widgets now keep mounts alive by default and automatically remount after unexpected rclone exits with capped backoff
-   FIXED: Windows unmount now checks the mount's rc `vfs/queue`/`vfs/stats` state and warns before unmounting with pending VFS uploads
-   FIXED: Move now uses `rclone moveto` with an explicit destination path so moving a folder preserves the folder wrapper instead of moving only its contents
-   FIXED: Destructive actions now warn when the selected row has duplicate sibling names, preventing silent multi-delete/move/rename surprises on remotes such as Google Drive
-   FIXED: Config editing now warns when jobs, mounts, or streams are active and can defer the change or continue explicitly; modified custom rclone configs note that remotes were reloaded
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
-   SECURITY: Mount startup now warns once per detected version for vulnerable WinFsp (CVE-2026-3006, WinFsp <= 2.1.25156) and for old macFUSE versions before 5.2
-   SECURITY: `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` called early in main on Windows to prevent DLL planting from the application directory
-   SECURITY: rclone executable paths ending in `.bat` or `.cmd` are now rejected (BatBadBut, CVE-2024-24576)
-   SECURITY: Windows mounts now authenticate the rclone remote-control endpoint with a random per-mount credential — prevents CVE-2026-41176 (CVSS 9.2) and CVE-2026-49980 (CVSS 9.8) exploitation via unauthenticated loopback rc
-   SECURITY: Startup warns (once per version) when the detected rclone is older than 1.74.3, with a link to the affected advisories
-   SECURITY: macOS `rclone config` no longer writes a world-readable script with a fixed predictable name in shared `/tmp` (symlink/pre-creation hazard) — it now uses a unique, user-only file in the per-user temp dir
-   SECURITY: Deleting a saved task now asks for confirmation

### Build & Compatibility
-   NEW: Windows ARM64 CI build matrix leg (windows-11-arm runner with Qt 6.8)
-   NEW: Linux ARM64 (aarch64) AppImage release artifact — tagged releases now include a native aarch64 AppImage built on GitHub's ARM64 Ubuntu runner
-   NEW: REUSE 3.3 compliance via `REUSE.toml` with glob-pattern annotations and `LICENSES/MIT.txt` — machine-verifiable SPDX licensing for distro packaging and OpenSSF Scorecard
-   CHANGED: Manual release scripts modernized — Windows targets VS 2022+/Qt 6/x64-only with fail-fast dependency checks; Linux drops CentOS 7/OpenSSL 1.1/i686/armhf assumptions and supports x86_64 and aarch64 with distro Qt 6; macOS supports Apple Silicon Homebrew paths and uses `macdeployqt -dmg`
-   CHANGED: Desktop file, metainfo, and installed icons now use the reverse-DNS app ID (`io.github.sysadmindoc.rclonebrowserng`) required by Flatpak/Flathub; `setDesktopFileName` updated for Wayland focus-stealing compliance
-   CHANGED: macOS CI and release workflows now build both ARM64 (macos-14) and x86_64 (macos-13) artifacts
-   CHANGED: CodeQL workflow upgraded from pinned v3 to v4
-   CHANGED: Consolidated duplicate `getNiceSize()` helper from item_model.cpp, job_widget.cpp, and rc_job_widget.cpp into a single `GetNiceSize()` in utils
-   CHANGED: Removed dead macOS 10.9-10.13 code paths (dark mode and icon sizing) — deployment target has been macOS 11.0 since the Qt 6 port
-   NEW: Added a tag-triggered GitHub release workflow that builds Windows zip/installer, macOS DMG/app zip, Linux AppImage, SHA256 sums, and GitHub provenance attestations
-   NEW: macOS mount startup now detects FSKit-capable macFUSE, fuse-t, and rclone `nfsmount`; it prefers userspace-capable backends and only warns when no modern backend is available
-   FIXED: Installed identity now consistently uses Rclone Browser NG across the app display name, main window, Linux desktop entry, macOS bundle display name, Windows installer metadata, and CI metadata validation
-   NEW: WinFsp detection — mounting on Windows now checks for WinFsp and offers a download link if missing, instead of failing with a cryptic rclone error
-   FIXED: AppStream metainfo rewritten for NG identity (`io.github.sysadmindoc.rclonebrowserng`), installed to `share/metainfo/`, and embedded in AppImage — unblocks Flathub and distro packaging
-   FIXED: Qt 6 Windows/macOS builds did not compile — QtWinExtras `QtWin::fromHICON` replaced with `QImage::fromHICON`, missing Windows shell/COM headers included, `QFileInfo` explicit-constructor errors fixed, COM initialized on the icon worker thread
-   FIXED: Robust rclone version parsing — beta/suffixed versions (e.g. `1.67.0-beta…`) can no longer throw
-   FIXED: Version string read from VERSION file is trimmed (stray newline no longer corrupts the About box / update check)

### UX
-   CHANGED: The New Remote flow now uses a guided dialog with explanatory helper text, inline validation, focus/error styling, clearer provider descriptions, clearable fields, and provider matching by exact rclone prefix as well as display name
-   CHANGED: Transfer and export dialogs now keep required-field feedback inline with highlighted fields instead of interrupting users with modal warning boxes
-   CHANGED: Progress dialogs now show explicit Running/Finished/Failed states and recover cleanly when rclone cannot start
-   CHANGED: The remotes filter now shows a calm no-results row instead of leaving the list blank
-   CHANGED: Remote browser right-click actions now open from one coherent context menu, including folder Archive and Speed Test tools, instead of showing separate menus
-   CHANGED: Removed shortcut-only command bindings and mnemonic markers from the main and remote browser surfaces; actions are now exposed through visible buttons, menus, tabs, and context menus
-   CHANGED: Deepened the native Qt polish system with refined menu/tab/list/tree/scrollbar/dialog styling, stronger focus and disabled states, clearer status badges, consistent action bars, smoother read-only telemetry fields, and better accessible names across the main shell, remote browser, jobs, mounts, streams, transfers, export, progress, and preferences surfaces
-   CHANGED: Added a shared Qt polish layer for cohesive light/dark styling, stronger focus/hover/disabled states, clearer empty states, refined action hierarchy, bounded remote icon sizing, and more accessible labels across remotes, tasks, jobs, mounts, streams, transfers, export, progress, and preferences dialogs
-   FIXED: Job widgets now cap per-file progress rows and show an overflow count so many small active transfers cannot balloon or glitch the layout
-   FIXED: Long rclone version/path status text no longer forces the main window layout wider or overlaps nearby content; the full text remains available as a tooltip
-   NEW: Launching a second instance now focuses the already-running window instead of showing an error dialog (QLocalServer IPC replaces the QLockFile-only approach)
-   NEW: Open remote tabs are saved on app close and restored on next launch; tabs whose remote no longer exists are silently skipped
-   NEW: Typeahead filter field above the remotes list for users with many configured remotes; its clear button resets the filter
-   NEW: Large directory listings pass `--list-cutoff` to rclone (>= 1.74) for on-disk sorting, reducing rclone memory usage on backends with very large directories
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
