# Change Log
## [Unreleased]
### Distribution
-   NEW: 2.0.0, 2.0.1 and 2.0.2 are tagged, and 2.0.2 is published on the releases page with a Windows installer, a portable zip, a source archive and a `SHA256SUMS` file. Until now the only way to get the app was to build it
-   NEW: `scripts/release_check.py` writes `release/SHA256SUMS` over whatever artifacts are present, so an unsigned download can still be verified
-   CHANGED: `git archive` no longer puts the internal planning notes (`ROADMAP.md`, `RESEARCH.md`, `Roadmap_Blocked.md`) into source tarballs

### Security
-   SECURITY: The rclone version the app warns about was raised from 1.74.3 to 1.75.1. The old floor was three releases behind and was itself an affected version (GHSA-fqj9-69pf-6pjg, patched in 1.74.4); rclone 1.75.1 closes eleven further advisories, two of them critical. The warning now names them, including the one that exposes the remote-control daemon's command line — and so its credentials — to anything that can reach the port
-   SECURITY: The floor lives in one place with the date it was last checked, and the local release harness refuses to publish once that review is more than 180 days old

### Security
-   SECURITY: The Qt version check now runs in all three release lanes, not just the Windows one, so an AppImage or DMG can no longer ship a Qt nobody validated. It refuses a build on two grounds rather than one: unpatched for CVE-2026-6210 or CVE-2026-9499, or on a branch that has stopped receiving open-source patches
-   CHANGED: Release builds are pinned to Qt 6.11.2 and the reasoning is written down in SECURITY.md. Qt 6.8 LTS looks like the right choice but is not usable: its open-source window closed in April 2025 and the public archive stops at 6.8.4, while the CVE fixes are in 6.8.8, which is commercial-only. The policy is dated and the validator warns for the last 30 days of a branch's support window
-   NEW: CMake states an explicit Qt 6.4 minimum for source builds, matching the README badge, and fails at configure time with a readable message instead of somewhere deep in the build

### Reliability
-   NEW: Copying between two remotes on the same provider can now let the provider do it. When both paths name remotes of the same backend type, the transfer dialog offers "Copy on the server where possible", which adds `--server-side-across-configs` so the bytes never travel through your machine. The option stays hidden when the ends differ, and the job output says how many files were copied server-side and how much that saved
-   NEW: Sync says what it is about to delete before it deletes it. Choosing Sync, or ticking "delete excluded", now runs the same command with `--dry-run` first and shows how many files will be transferred, timestamped and removed, with the first twenty deletions listed and how much space they free. Run and Cancel; nothing is a yes/no prompt, and the summary is skipped entirely when the destination has nothing to remove. An upstream reporter lost already-uploaded data to a misread exclude combined with delete-excluded (kapitainsky#252)
-   NEW: A rotating diagnostic log is written to disk, so a scheduled transfer that fails overnight leaves evidence behind instead of only an in-memory list that dies with the process. It lives in `logs/rclonebrowser.log` under the application data folder, rotates at 5 MB keeping three older generations, and records job starts, exit codes, background errors and Qt messages. Secrets are redacted with the same rules the support bundle uses. Preferences has a log level and an Open Log Folder button (kapitainsky#134, kapitainsky#233, mmozeiko#148)

### Bug Fixes
-   FIX: A transfer that stopped at a limit you set is no longer reported as a failure. Every non-zero rclone exit code rendered as a red "Needs attention" card and a history row reading "Failed", so hitting `--max-transfer`, `--max-duration` or `--error-on-no-transfer` looked like something broke. Each documented code now has its own name and a line saying what happened; the three limit codes read as completed, and a missing path or temporary fault says retrying is worth a try
-   FIX: A remote with a description is no longer mangled or dropped from the remotes list. The list was built by splitting each `rclone listremotes --long` line on `:` and demanding exactly two parts, so a description containing a colon removed the remote entirely and any description at all left a type no icon matched. Remotes are now read from `listremotes --json` where rclone supports it (v1.68.0 and newer), the description appears in the tooltip, and the `--long` fallback parses the columns properly instead of guessing
-   FIX: A failed unmount no longer force-kills the mount ten seconds later. The fallback that stops rclone when the unmount helper does not take effect now stands down once the helper has reported a failure, and a second Unmount click works instead of doing nothing
-   FIX: A post-transfer command that fails is no longer silent. Hooks ran detached, so a command that could not start or exited non-zero looked exactly like one that worked; the exit code and the last line of output now reach the background error list and the job history
-   FIX: A failed unmount no longer leaves the mount card stuck on "Unmounting" with the remote still mounted. `umount` and `fusermount` ran detached with nothing reading their result; the card now says "Unmount failed" with the reason, gives the controls back, and raises a background error
-   FIX: Starting a transfer no longer freezes the window while the rclone remote-control daemon comes up. The startup ran on the window thread and could hold it for up to fifteen seconds with no progress and nothing to cancel; it now polls on the event loop, and two transfers started together share one daemon instead of racing two
-   FIX: Transfer progress no longer reads above 100%. rclone can briefly report more bytes transferred than the total while a file is retried, and the percentage was passed through unclamped
-   FIX: Remotes no longer disappear from the list a moment after it loads. When a crypt remote was configured, the remote holding its encrypted data was hidden unconditionally by a background `rclone config dump`, with no setting and no way to bring it back — a user with seven crypt backends saw eight of their fifteen remotes. Hiding is now opt-in through "Hide remotes used as crypt backends" in Preferences, off by default, and when it is on the remotes list says how many remotes it hid and where to turn it off
-   FIX: Downloading a multi-file selection transferred the wrong files. Each selected name was passed as `--include <name>`, and rclone filter patterns are unanchored globs, so selecting `a.txt` also pulled `sub/a.txt`, selecting `b[1].txt` fetched `b1.txt` instead, and selecting a folder fetched nothing from inside it. The selection is now turned into anchored, escaped `--filter` rules that match exactly what was picked, with folders recursed. A selection too large for one command line is refused with an explanation rather than silently truncated
-   FIX: Four features were built but unreachable because the code that placed their controls cast the parent's layout to a class the `.ui` does not use, so the cast returned null and the widget was never added to any layout. The **Bisync** transfer mode, the **bandwidth timetable editor** button, the **performance preset** picker and the Google Drive **Trash** button are now declared in their `.ui` files and appear where they belong. Same defect as #13, which had only been repaired inside the Preferences dialog
-   FIX: The performance preset picker no longer overwrites restored transfer, checker and bandwidth values when the transfer dialog opens. The preset is an action rather than a remembered setting, so the dialog always opens on Custom
-   NEW: A layout regression suite sweeps the main window, transfer dialog and remote browser for controls that exist outside every layout, so a future `.ui` change cannot silently orphan a feature again
-   CHANGED: The `lsjson` listing parser and the `--use-json-log` progress parser moved out of the widgets that use them into `src/lsjson_parser.cpp` and `src/job_stats.cpp`, and the regression suite now links those instead of carrying its own copies. Both are covered against chunk boundaries at every byte offset, nested metadata, escaped quotes, trailing spaces, 8 TiB sizes and non-ASCII names

## [2.0.2] - 2026-08-24
### Bug Fixes
-   FIX: Starting minimized can no longer leave the app unreachable. The tray icon is now shown before the window hides at startup, even when launched with `--minimized` or `--tray`, and stored settings from older builds that combine start minimized with a hidden tray icon are repaired on launch (#14, thanks Dwgthomson)
-   FIX: The Preferences dialog now reports "Always show in tray" as on whenever "Start minimized" is checked, so the two settings cannot be saved in a contradictory state

## [2.0.1] - 2026-08-03
### Bug Fixes
-   FIX: Preferences dialog layout on Qt 6 (#13) — dynamically created controls (max concurrent transfers, exclude patterns, SOCKS proxy, Backup/Restore Config, Start minimized) were inserted via layout casts that fail against the .ui's QGridLayouts, leaving them floating over the tab bar and the SOCKS field effectively missing; they are now regular .ui widgets in managed layouts, covered by a layout regression test
-   FIX: Transfer defaults moved from the overcrowded General page to a new Transfers tab, so the exclude-pattern editor and Backup/Restore buttons no longer clip on shorter screens
-   FIX: Preferences minimum size now tracks the layout's computed minimum, so dark-theme/large-font metrics can no longer compress the Interface page until checkboxes and help labels overlap
-   NEW: Start minimized to system tray now implies Always show in system tray (and unchecking the latter clears it), since starting hidden without a tray icon left no way to bring the window back

### Security
-   SECURITY: Saved-task webhook tokens, heartbeat URLs, and shell hook commands are now protected at rest using Windows DPAPI encryption (base64 obfuscation on other platforms); no representative secret appears in plaintext in the JSON task store
-   SECURITY: Transfer dialogs now disclose the reversible non-encrypted fallback used for saved-task sensitive fields when platform encryption is unavailable, and diagnostics redaction covers named API/client/access key forms
-   SECURITY: Saved-task shell hooks (preCommand/postCommand) are now trust-gated — imported or migrated tasks with hooks prompt for user review before execution; hooks set directly in the transfer dialog are trusted implicitly
-   SECURITY: OpenSSF-recommended binary hardening flags added — MSVC `/guard:cf` (Control Flow Guard), `/CETCOMPAT` (Intel CET), `/DYNAMICBASE`; GCC/Clang `-fcf-protection=full`, `-Wl,-z,relro,-z,now` (full RELRO), `_FORTIFY_SOURCE` raised from 2 to 3
-   SECURITY: RC auth regression test gate — automated test scans all source files for RC command construction and fails if any path lacks `--rc-user`/`--rc-pass` or introduces `--rc-no-auth`

### Reliability & Data Safety
-   NEW: Staged transfers now persist across restart — enqueued transfers are saved atomically to `staged.json` and restored on launch
-   NEW: Staged transfer persistence now uses schema v2 and retains execution metadata, including protected hook and notification fields, across restart with automatic v1 migration
-   NEW: Headless accessibility smoke coverage now exercises the main window, transfer, preferences, scheduling, search, and folder-compare surfaces for keyboard focus chains, accessible control names, focus styling, and non-zero layouts
-   NEW: Mount configuration now offers Balanced, Streaming, Offline-friendly, and Custom presets with exact rclone flags, preserved expert options, incompatible-option validation, and periodic stale-mount probes with an explicit remount action
-   NEW: Post-transfer verification — a "Verify integrity after transfer" checkbox in the transfer dialog runs `rclone check` (or `cryptcheck` for crypt remotes) after successful copy/sync operations
-   NEW: Restartable job history — completed transfers now store their command arguments in history; failed or interrupted jobs can be restarted or dry-run previewed from the Job History dialog
-   NEW: Operation option profiles — save, load, and delete named rclone flag profiles in the transfer dialog for repeatable per-remote configurations
-   NEW: Config backup and restore — Preferences dialog now has Backup Config and Restore Config buttons; restore automatically backs up the current config before overwriting
-   NEW: First-run rclone repair assistant — missing or broken rclone shows a guided dialog with Browse, Open Download Page, and Open Preferences options instead of a bare warning
-   NEW: `--version` command-line flag for headless version checks and release smoke tests
-   NEW: Qt Linguist i18n scaffolding — QTranslator loading from bundled and external translation files; Qt LinguistTools CMake integration

### Build & Compatibility
-   CHANGED: Release documentation now describes the repository's local build and release scripts; GitHub Actions, CI-only checks, artifact attestations, and automated publication are not provided here
-   NEW: `scripts/release_check.py` now provides one local release-readiness command for metadata, script, Release build, offscreen tests, and executable version checks
-   NEW: Auxiliary release scripts now have Bash syntax coverage, and `release_windows.cmd --dry-run` validates its artifact/build plan without touching the toolchain or release directory
-   NEW: Local Windows deployment and platform release scripts now run the packaged executable's non-GUI `--version` smoke before release handoff
-   NEW: AppImage releases now embed `gh-releases-zsync` metadata, emit a matching `.zsync` delta file, and verify the update section before handoff
-   FIXED: Mount backend probes, VFS unmount safety checks, and native schedule/status operations now use asynchronous or worker-backed helpers instead of blocking the GUI thread
-   NEW: AppStream metainfo now records release history with dated descriptions and local release-history validation
-   NEW: Remote Health panel shows local disk free/total/usage via rclone `core/disks` (rclone >= 1.74)
-   CHANGED: All 17 C++ test targets now use the Qt QTest framework with QVERIFY/QCOMPARE assertions and proper test reporting
-   FIXED: Cross-remote search cancel now uses async process termination instead of blocking `waitForFinished()`

### Security
-   SECURITY: Local release packaging uses pinned, SHA256-verified linuxdeploy AppImages instead of mutable `continuous` downloads, and the local Windows release path validates the Qt CVE floor.
-   SECURITY: Preview download errors now render remote `rclone` output as plain text, preventing backend error text from being interpreted as rich text.
-   SECURITY: File Properties now escapes remote-provided filenames, hashes, and metadata before rendering rich-text details.
-   SECURITY: Transfer, mount, stream, and notice summaries now force plain-text rendering for path and remote-derived labels.
-   SECURITY: Heartbeat, webhook, and copyurl inputs now reject malformed or non-http(s) URLs before creating network requests
-   SECURITY: The local Windows release Qt baseline was raised from 6.7.* to 6.8.* to avoid CVE-2026-6210 (Qt SVG); the release script validates the Qt version and fails on vulnerable ranges
-   SECURITY: Preview temp files now use sanitized filenames (`QFileInfo::fileName()`) preventing path-traversal via malicious remote filenames
-   SECURITY: Schedule XML temp files now use `QTemporaryFile` with random names instead of predictable paths, preventing symlink/replacement attacks
-   FIXED: Use-after-free in backend feature query — `QPointer` guard prevents callback from accessing destroyed RemoteWidget
-   FIXED: Use-after-free in Remote Health dialog — nested async process callbacks now use `QPointer` guards for dialog widgets
-   FIXED: Preview temp files are now cleaned up on download error (previously leaked)
-   FIXED: Preview of 0-byte files shows "File is empty" message instead of a blank dialog
-   FIXED: Cron expression validation now rejects out-of-range values (e.g., minute 60, hour 25, day 32)
-   FIXED: Cross-remote search button is disabled when no remotes are checked (previously silently searched all)
-   FIXED: Progress dialog now flushes remaining process output before reporting completion (prevents last-line data loss)
-   FIXED: Mount unmount process is cleaned up on start failure via `errorOccurred` signal (prevents memory leak)
-   FIXED: Drag-and-drop of multiple files between remote tabs now creates individual transfers instead of silently dropping all but the first
-   FIXED: Free-form rclone option fields now preserve quoted values with spaces instead of splitting them into broken arguments
-   FIXED: Task store migration now preserves backup-dir retention and bisync conflict-resolution fields in the legacy binary serializer
-   FIXED: Task store JSON loading now fails closed on malformed roots or non-object task entries instead of silently dropping bad data
-   FIXED: Cross-remote search now drains final stdout, handles failed-to-start rclone processes, clears stale error tooltips, and accepts compact lsjson arrays
-   FIXED: Saved task dry-runs no longer leave the in-memory task marked dry-run after launching, and pre-job commands report failed starts without leaving the transfer flow stuck
-   FIXED: Remote listing now consumes the final process output chunk before deciding whether a directory load failed
-   FIXED: Backup retention is now enforced for `{date}` backup-dir snapshots after successful transfers instead of only saving the retain count
-   FIXED: Drag-and-drop uploads now honor the dialog's Enqueue action instead of always starting immediately
-   CHANGED: Remote browser back/forward toolbar buttons no longer advertise removed keyboard shortcuts and remain keyboard-focusable
-   FIXED: Transfer detail capture no longer requires colon in message — any rclone log entry with an `object` field is captured

-   FIXED: High-contrast status badge colors now choose palette-contrasting success/warning/error tones instead of fixed colors
-   FIXED: Cron next-run previews now iterate by local civil minutes so repeated DST hours are not duplicated
-   FIXED: Backup-dir `{date}` placeholders now include milliseconds, process id, and a per-command sequence to avoid same-second collisions

### Reliability & Data Safety
-   FIXED: Serve stream cards now close without blocking the UI or touching a stale rclone process after it has already exited.
-   CHANGED: File previews now download into isolated per-preview temporary folders with cancel/timeout feedback instead of a shared filename-based temp path.
-   CHANGED: Create Remote provider loading, Open/Edit version checks, and file Properties metadata reads now run asynchronously with cancelable loading states instead of blocking the GUI thread.
-   NEW: Dry-run and preview non-mutation contract tests — 6 tests verify `--dry-run` is never lost, never persisted, and always produced exactly once for all operation types
-   NEW: Backend capability registry — opening a remote asynchronously queries `rclone backend features` to cache per-remote capabilities (PublicLink, Move, About, etc.) and gates UI actions accordingly
-   NEW: Windows schedules now use XML-based task creation with `StartWhenAvailable=true` so missed runs are caught up when the machine comes back online
-   NEW: Scheduler golden tests — 20+ tests cover Windows XML, systemd timers, and macOS plists for every interval type
-   NEW: Per-file job audit detail — completed jobs record per-file transfer events (transferred, deleted, skipped, error) with timestamps; viewable and exportable (secrets redacted) from Job History
-   NEW: Remote Health diagnostics panel (Help > Remote Health) — shows rclone version, mount backend, per-remote connectivity and quota status with Copy Report
-   NEW: High-contrast palette support — UI colors derive from system palette when contrast ratio exceeds 12:1 (Windows High Contrast, GNOME High Contrast)
-   NEW: Parsing/serialization regression tests — 10 golden-file tests covering lsjson streaming parser (chunked, Unicode, >4GB, nested metadata) and JSON stats parser
-   NEW: Generalized cloud trash — delete operations use backend-specific trash flags for all supporting providers (Drive, OneDrive, Dropbox); non-Google backends get a Clean Up button for `rclone cleanup`
-   NEW: Cron-expression scheduling — Schedule dialog now supports freeform 5-field cron expressions with a next-5-runs preview alongside the existing simple intervals
-   NEW: Non-blocking background error queue — errors from running transfers accumulate in a status bar badge instead of blocking modal dialogs; click to review all queued errors
-   NEW: Drag between remote tabs — drag files from one remote browser to another tab header to initiate a cross-remote copy
-   NEW: Cross-remote search filters and history — search dialog gains per-remote selection, file type presets, min/max size filters, and persistent search history
-   NEW: Safe inline preview — right-click a file > Preview to view images, text, and code inline; media files open in the system player; 50 MB limit; temp files auto-cleaned
-   NEW: Versioning/snapshot browser — right-click a file > Versions to browse S3/B2/versioned backend versions with timestamps, sizes, and one-click restore to current path
-   SECURITY: Build hardening — added `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, and position-independent code on Linux/macOS; replaced MSVC `/GS-` (stack protection disabled) with `/GS` (enabled)
-   FIXED: Windows schedule listing now uses proper CSV parsing that respects quoted fields with commas; status column is read by index instead of string-searching for "Disabled"
-   FIXED: JSON task store now checks the schema version on read and rejects newer schemas with an informative error instead of silently losing fields
-   FIXED: JSON enum casts from untrusted integers are now clamped to valid ranges instead of producing undefined behavior
-   FIXED: cryptcheck mode in folder compare now labels Source as "Plaintext" and Destination as "Crypt remote" to clarify the required argument order
-   FIXED: rclone selfupdate no longer blocks the UI thread for up to 60 seconds — runs asynchronously with status bar feedback
-   FIXED: Pre-job commands now run asynchronously instead of blocking the UI thread for up to 30 seconds
-   FIXED: File filter (Ctrl+F) now re-applies after directory navigation — newly loaded children are filtered automatically
-   FIXED: Send-To now prompts for a subfolder path within the selected remote instead of always uploading to the root
-   NEW: Back/forward navigation in the remote browser — toolbar buttons and Alt+Left/Right keyboard shortcuts navigate browsing history
-   NEW: Staging queue now supports drag-and-drop reorder
-   NEW: Bookmark manager dialog — Bookmarks > Manage Bookmarks lets you reorder bookmarks via drag-and-drop and delete with the Delete key
-   NEW: Bisync resync recovery — failed bisync jobs show a Resync button that re-runs with `--resync` to reset bisync state
-   NEW: Transfer performance presets — a Preset dropdown in the transfer dialog offers Default, Large Files, Many Small Files, and Low Bandwidth configurations that auto-fill transfers/checkers/bandwidth
-   NEW: macOS scheduler uses `StartCalendarInterval` for daily/weekly schedules so users can set a specific hour; uses `launchctl bootstrap`/`bootout` with fallback to deprecated `load`/`unload`
-   NEW: Linux scheduler now generates systemd user timers with `Persistent=true` on systems with systemd, falling back to crontab when systemd is unavailable

### Build & Compatibility
-   NEW: SECURITY.md vulnerability disclosure policy
-   CHANGED: Top-level CMakeLists.txt cleaned up — removed dead Qt5/Qt5WinExtras/Qt5MacExtras fallback code
-   SECURITY: Schedule manager task name sanitizer now allowlists alphanumeric, dash, underscore, dot, and space only — blocks crontab newline injection, XML injection in macOS plists, and shell metachar injection across all platforms
-   SECURITY: Linux crontab entries now use single-quote shell escaping for task names, preventing command injection through task names containing `$(...)` or backticks
-   SECURITY: macOS launchd plist generation now XML-escapes `&`, `<`, `>` in task names and paths
-   SECURITY: Remote URL upload (copyurl) now validates URL scheme — only http:// and https:// are allowed, blocking `file://` local-file-read attacks
-   FIXED: Google Drive trash listing was broken — `readAllStandardOutput()` returned empty data because `MergedChannels` mixed stderr into the output buffer; switched to `SeparateChannels` and made the call async to stop blocking the GUI for up to 30 seconds
-   FIXED: File properties dialog switched from `MergedChannels` to `SeparateChannels` so hash/metadata output is parsed correctly
-   FIXED: Transfer dialog grid row collision — backup-dir, bisync conflict-resolve, watch-folder, validation, and preview widgets were all fighting for rows 13-14; reassigned to distinct rows 14-19 so all fields are visible
-   FIXED: Cross-remote search results table corruption — sorting was enabled during row insertion, causing Qt to reorder rows before all columns were populated; now disables sorting during each insertion
-   FIXED: Cross-remote search cancel now shows a proper "Search cancelled" status instead of a stale "Searching..." message
-   FIXED: Cross-remote search uses `SeparateChannels` so rclone error messages don't interfere with JSON parsing
-   FIXED: Pause button now hides when a transfer finishes — previously remained visible and clickable (no-op but confusing)
-   FIXED: Auto-mount on launch now validates that each remote still exists before attempting to mount, and requires a non-empty mount point — previously attempted mounts on deleted remotes with empty paths, entering a futile retry loop
-   FIXED: Staging queue "Run All" on an empty queue now shows "Staging queue is empty" instead of the misleading "All staged transfers started"
-   FIXED: Storage usage dialog now shows "N/A" for negative quota values (returned by some backends for unknown quotas) instead of displaying "16 EiB"
-   FIXED: Send To handler now includes default exclude patterns and uploads to the remote root correctly
-   NEW: Staleness detection and overdue-job alerting — a background timer checks job history against scheduled tasks every 5 minutes; if a scheduled task hasn't run successfully within its expected interval + margin, a tray notification warns about the overdue task(s)
-   NEW: Auto-update capability — rclone update dialog now offers a "Run selfupdate" button that upgrades rclone in place; browser update dialog offers an "Open Downloads" button that launches the GitHub releases page directly
-   NEW: Windows Explorer "Send to remote" integration — Help > Install Explorer Send To creates a shortcut in the Windows SendTo folder; right-click files in Explorer > Send to > Upload to Remote starts an upload to a chosen remote
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
-   CHANGED: Dropped Qt 5 dual support — Qt 6.4 is now the minimum; all Qt5 ifdefs, QtWinExtras/QtMacExtras imports, High-DPI scaling workarounds, and the former Qt5 build-matrix leg are removed
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
-   NEW: REUSE 3.3 compliance via `REUSE.toml` with glob-pattern annotations and `LICENSES/MIT.txt` — machine-verifiable SPDX licensing for distro packaging and license checks
-   CHANGED: Manual release scripts modernized — Windows targets VS 2022+/Qt 6/x64-only with fail-fast dependency checks; Linux drops CentOS 7/OpenSSL 1.1/i686/armhf assumptions and supports x86_64 and aarch64 with distro Qt 6; macOS supports Apple Silicon Homebrew paths and uses `macdeployqt -dmg`
-   CHANGED: Desktop file, metainfo, and installed icons now use the reverse-DNS app ID (`io.github.sysadmindoc.rclonebrowserng`) required by Flatpak/Flathub; `setDesktopFileName` updated for Wayland focus-stealing compliance
-   CHANGED: Consolidated duplicate `getNiceSize()` helper from item_model.cpp, job_widget.cpp, and rc_job_widget.cpp into a single `GetNiceSize()` in utils
-   CHANGED: Removed dead macOS 10.9-10.13 code paths (dark mode and icon sizing) — deployment target has been macOS 11.0 since the Qt 6 port
-   NEW: macOS mount startup now detects FSKit-capable macFUSE, fuse-t, and rclone `nfsmount`; it prefers userspace-capable backends and only warns when no modern backend is available
-   FIXED: Installed identity now consistently uses Rclone Browser NG across the app display name, main window, Linux desktop entry, macOS bundle display name, Windows installer metadata, and local metadata validation
-   NEW: WinFsp detection — mounting on Windows now checks for WinFsp and offers a download link if missing, instead of failing with a cryptic rclone error
-   FIXED: AppStream metainfo rewritten for NG identity (`io.github.sysadmindoc.rclonebrowserng`), installed to `share/metainfo/`, and embedded in AppImage — unblocks Flathub and distro packaging
-   FIXED: Qt 6 Windows/macOS builds did not compile — QtWinExtras `QtWin::fromHICON` replaced with `QImage::fromHICON`, missing Windows shell/COM headers included, `QFileInfo` explicit-constructor errors fixed, COM initialized on the icon worker thread
-   FIXED: Robust rclone version parsing — beta/suffixed versions (e.g. `1.67.0-beta…`) can no longer throw
-   FIXED: Version string read from VERSION file is trimmed (stray newline no longer corrupts the About box / update check)

### UX
-   CHANGED: Main remotes, saved tasks, and staging queue panels now use consistent framed empty, loading, and no-match states instead of blank panes or disabled placeholder rows; the remotes filter and view toggle share a compact tools row.
-   CHANGED: Refined remote-browser and job-card interactions: disabled toolbar actions now explain what selection/state is required, stop/cancel/unmount controls show immediate "Stopping" feedback without routine confirmation dialogs, and long-running stop paths no longer block the UI thread
-   CHANGED: Extended the native polish system to data tables and dialog button bars; cross-remote search, folder compare, job history, bandwidth schedules, and saved-task filtering now have clearer empty states, disabled states, row selection, focus styling, and calmer inline feedback
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

## Roadmap archive — 2026-08-10 — ROADMAP.md

<details>
<summary>Original roadmap snapshot</summary>

```markdown
# ROADMAP — RcloneBrowserNG

> Community continuation of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser) (itself a fork of [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser)).  
> Both upstream repos are abandoned (last activity: Dec 2020 / 2018 respectively).  
> This roadmap consolidates **every** open issue, unmerged PR, and community request from the entire upstream/fork ecosystem.
>
> Items blocked on credentials, external submissions, unimplemented dependencies, or hardware → `Roadmap_Blocked.md`.

## Competitive Landscape

For context — active alternatives as of June 2026:

| Project | Stars | Stack | Status | Key Differentiator |
|---------|-------|-------|--------|--------------------|
| [rclone-ui](https://github.com/rclone-ui/rclone-ui) | 2,100 | TypeScript/Tauri | Active (v3.6.0) | Cron scheduling, dual-panel Commander, 7 langs, WinGet/Homebrew/Flathub. **Caution: #218 dry-run may execute real sync** |
| [RClone Manager](https://github.com/Zarestia-Dev/rclone-manager) | 933 | Angular/Tauri | Active (v0.2.7) | Fastest-growing. FS watchers, alert actions (toast/webhook/email/Telegram/script), Docker headless, Crowdin i18n, performance presets |
| [Celeste](https://github.com/hwittenborn/celeste) | 1,616 | Rust/GTK4 | **Archived Nov 2025** | Was Linux-only bisync — lane now open |
| [rclone-webui-react](https://github.com/rclone/rclone-webui-react) | 1,562 | JavaScript | Stale (5yr no release) | Superseded by `rclone gui` (v1.74.0) |
| [rem](https://github.com/liriliri/rem) | 603 | Electron | Active (v1.4.0, Dec 2025) | "Open source RcloneView" — file preview, multi-window, media preview |
| [rclone-webui-angular](https://github.com/yuudi/rclone-webui-angular) | 359 | Angular | Slowing (10mo no release) | Web alternative, GPG-signed releases |
| [H4R1B0/rclone-gui](https://github.com/H4R1B0/rclone-gui) | Low | Swift | Active (v1.6.0, 521 tests) | macOS-native. Cloud trash (10 providers), bookmarks, bulk rename, back/forward nav. Zero visibility |
| [yet-another-rclone-dashboard](https://github.com/outlook84/yet-another-rclone-dashboard) | 125 | TypeScript | Active (v0.4.4) | New entrant. Lightweight web RC GUI, multiple themes, mobile-responsive, PWA |
| [RcloneShuttle](https://github.com/pieterdd/RcloneShuttle) | 150 | Rust/GTK4 | Active (v0.1.9) | Linux-only via Flathub, upload-only, anti-AI policy |
| [rclone-rc-web-gui](https://github.com/retifrav/rclone-rc-web-gui) | 125 | TypeScript | Active (v2026.1.2) | Lightweight RC web GUI, no sync by design |

RcloneBrowserNG's niche: **lightweight native Qt desktop app** — faster startup, lower memory, no Electron/Tauri runtime, no browser tab. The value proposition is a well-maintained, modern C++/Qt6 app that "just works" with current rclone on all desktop platforms. Community signal: strong anti-paywall and anti-AI-vibe-coded sentiment favors a free, well-engineered native GUI.

---

## Source Cross-Reference

All items trace back to public GitHub issues/PRs:

- `kapitainsky #NNN` = [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser/issues)
- `mmozeiko #NNN` = [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser/issues)
- `docker #NNN` = [romancin/rclonebrowser-docker](https://github.com/romancin/rclonebrowser-docker/issues)
- `PR#NNN` = pull request (repo indicated in context)
- `rclone forum` = [forum.rclone.org](https://forum.rclone.org) discussion threads

## Research-Driven Additions

### P1

## Research-Driven Additions (2026-06-20)

## Research-Driven Additions
```

</details>
