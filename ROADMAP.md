# ROADMAP — RcloneBrowserNG

> Community continuation of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser) (itself a fork of [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser)).  
> Both upstream repos are abandoned (last activity: Dec 2020 / 2018 respectively).  
> This roadmap consolidates **every** open issue, unmerged PR, and community request from the entire upstream/fork ecosystem.

Status legend: `[ ]` = open, `[x]` = done, `[-]` = won't fix / deferred

---

## P1 — High Priority Bugs

Bugs that affect core functionality for existing users.


---

## P2 — UI / Display Bugs


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


- [ ] **Map as network drive on Windows** — Mount as a Windows network drive letter.  
  _Sources: kapitainsky #210_

- [ ] **Extended mount UI** — Directory navigation, cache settings, read-only toggle in mount dialog.  
  _Sources: kapitainsky #52_

### Monitoring & Logging
- [ ] **Real-time error log** — Live error log panel during transfers.  
  _Sources: kapitainsky #233, #134_

- [ ] **Global upload/download statistics** — Cumulative transfer counter across all jobs.  
  _Sources: kapitainsky #111_


### Miscellaneous
- [ ] **i18n / multi-language support** — Internationalization framework with translations.  
  _Sources: kapitainsky #138, #47; rclone forum (Portuguese, others)_

- [ ] **Hide/group encrypted remotes** — Hide the underlying unencrypted remote when a crypt remote exists.  
  _Sources: kapitainsky #206, #178_

- [ ] **Multiple config file support** — Switch between different rclone.conf files.  
  _Sources: kapitainsky #19_


- [ ] **Reopen existing instance instead of error** — Clicking the exe when already running should focus the existing window.  
  _Sources: kapitainsky #214_

- [ ] **Proxy support in GUI** — HTTP/SOCKS proxy configuration exposed in preferences (partially exists since v1.8.0 but incomplete).  
  _Sources: mmozeiko #88, #41_

- [ ] **Data usage per remote** — Show storage consumption per configured remote.  
  _Sources: mmozeiko #65_


- [ ] **Global bandwidth limit control** — UI slider for upload/download speed cap.  
  _Sources: kapitainsky #116_


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

---

## Research-Driven Additions

> Appended 2026-06-10 from full repo audit + ecosystem research (see RESEARCH.md). Net-new technical/architectural/security/distribution items not already enumerated above; feature requests already listed in P3 are not duplicated.

### P1 — Reliability root-cause fixes


### P2 — Architecture & engine

### P2 — Platform & mount modernization

### P2 — Distribution & CI


- [ ] P2 — Publish to winget, Scoop, and a -ng AUR package
  Why: inherited Repology entries point at the dead upstream; peers (Syncthing Tray, Cyberduck) ship these channels.
  Evidence: repology.org/project/rclone-browser/packages; microsoft/winget-pkgs
  Touches: packaging manifests, CI (wingetcreate)
  Acceptance: winget/scoop/AUR all install the current NG release; version bumps automated in CI.
  Complexity: M

### P2 — Trust / differentiator features

- [ ] P2 — Dry-run diff preview before sync
  Why: highest-trust feature; RcloneView uses it as a free-tier draw, rclone-ui's dry-run is buggy (#218); `_config:{dry_run:true}` over rc makes it cheap.
  Evidence: rcloneview.com; rclone-ui #218; rclone.org/rc
  Touches: transfer_dialog.cpp, engine
  Acceptance: a "Preview" button shows files that would be added/changed/deleted before the real run.
  Complexity: M

- [ ] P2 — Notification webhooks (Discord / Gotify / Shoutrrr / Healthchecks)
  Why: no rclone desktop GUI offers it; Backrest proves homelab demand; complements the existing tray-notification feature.
  Evidence: github.com/garethgeorge/backrest
  Touches: preferences_dialog.cpp, job completion handling
  Acceptance: on job finish/failure the app can POST to a configured webhook in addition to the local toast.
  Complexity: M

- [ ] P3 — Pre/post job hooks
  Why: run a script before/after a sync; trivial to add, strong homelab appeal (Backrest pattern).
  Evidence: github.com/garethgeorge/backrest
  Touches: job_options.cpp/.h, transfer_dialog.cpp, job execution
  Acceptance: tasks store optional pre/post commands that run around the transfer; failures logged.
  Complexity: S

- [ ] P3 — Serve management tab (HTTP/WebDAV/FTP/DLNA/NFS/S3)
  Why: rclone serve now spans 9 protocols; the official GUI and RClone Manager expose it; old GUIs don't.
  Evidence: rclone.org/commands/rclone_serve/; rclone.org/gui
  Touches: new serve widget, engine
  Acceptance: start/stop a serve from the GUI with status and resulting URL shown.
  Complexity: M

- [ ] P3 — Versioning / snapshot browser (S3 versions / --backup-dir) with restore
  Why: only S3Drive (paid) does it; rclone backends support it; zero free desktop competition.
  Evidence: s3drive.app
  Touches: remote_widget.cpp, engine
  Acceptance: for capable remotes, browse prior versions / backup-dir snapshots and restore a selected one.
  Complexity: L

### P3 — Hygiene

- [ ] P3 — Drop Qt5 dual support; floor Qt 6.4, build official binaries on Qt 6.8.3
  Why: Qt 5.15 OSS EOL was May 2025; KDE dropped Qt5 CI; the ifdef debt is pure maintenance cost. Note: Qt 6.8 LTS patches beyond the initial releases are commercial-only, so official binaries must track the current stable minor (6.9/6.10+), not pin 6.8.x.
  Evidence: endoflife.date/qt; doc.qt.io/qt-6/qt-releases.html; pch.h:16-23, main.cpp:6-14, preferences_dialog.cpp:15-21
  Touches: CMakeLists.txt, pch.h, main.cpp, preferences_dialog.cpp, .github/workflows
  Acceptance: Qt5 paths removed; builds against distro Qt 6.4 and ships on the current stable Qt minor; CI drops the Qt5 matrix leg.
  Complexity: M

- [ ] P3 — Parsing/serialization regression tests
  Why: the two things that have historically broken (output parsing, tasks.bin round-trip) have zero coverage.
  Evidence: RESEARCH.md test gap; CHANGELOG 2.0.0 (parsing broke 2021-2026)
  Touches: new test target in CMake, CI
  Acceptance: golden-file tests parse recorded rclone outputs across versions; JobOptions round-trips; CI runs them.
  Complexity: M

- [ ] P3 — i18n retrofit (wrap strings in tr(), Weblate integration)
  Why: zero tr() calls today; prerequisite for the existing P3 i18n item; Weblate natively supports Qt .ts, free for OSS.
  Evidence: RESEARCH.md i18n; docs.weblate.org Qt format
  Touches: all UI strings, CMake (lupdate/lrelease), Weblate config
  Acceptance: UI strings translatable; a .ts workflow exists and the English template is generated in CI.
  Complexity: L

- [ ] P3 — Accessibility pass (accessibleName / setBuddy / tab order)
  Why: zero accessibility metadata; Qt's UIA/VoiceOver/Orca bridges make a minimal pass cheap.
  Evidence: RESEARCH.md accessibility; doc.qt.io/qt-6/accessible-qwidget.html
  Touches: *.ui files, widget constructors
  Acceptance: icon-only buttons have accessible names, form fields have buddies, tab order is logical; a screen reader can drive core flows.
  Complexity: M

- [ ] P3 — Single-instance via QLocalServer/SingleApplication with focus-existing
  Why: clicking the exe when already running should focus the existing window (existing P3 request); current per-user lockfile only blocks, doesn't focus.
  Evidence: src/main.cpp:203-214; github.com/itay-grudev/SingleApplication
  Touches: main.cpp
  Acceptance: launching a second instance raises/focuses the running one instead of erroring; on Wayland the raise uses an xdg-activation token and QGuiApplication::setDesktopFileName matches the installed .desktop ID (KDE Plasma is hard-enforcing focus-stealing prevention).
  Complexity: S


### P2 — Mount & remote trust (supplemental)

- [ ] P2 — OAuth token-expiry detection with guided reconnect
  Why: expired Drive/OneDrive tokens are a top recurring rclone forum pain; errors surface as cryptic listing failures; only commercial RcloneView markets a fix. `rclone config reconnect <remote>:` is the underlying mechanism.
  Evidence: forum.rclone.org gdrive-oauth2 threads; rcloneview.com token-expired blog
  Touches: item_model.cpp / remote_widget.cpp error paths, main_window.cpp
  Acceptance: a token-expired/unauthorized error badges the remote and offers a one-click Reconnect that runs `rclone config reconnect` in the existing terminal flow.
  Complexity: M

- [ ] P2 — Remote wizard: test-connection probe and clone-remote action
  Why: half-configured remotes fail later with confusing errors; a bounded `lsjson --max-depth 1` probe at save time catches it, and "duplicate this remote" is a cheap common ask.
  Evidence: newhinton/Round-Sync#243, #32; rcloneview forum thread (users stuck with broken connections)
  Touches: remote-creation flow in main_window.cpp, remote_widget.cpp
  Acceptance: after config the wizard verifies the remote lists within a timeout and reports failure with the rclone error; context menu offers "Duplicate remote…" pre-filling a copy.
  Complexity: M

### P2 — Differentiator features (supplemental)

- [ ] P2 — Folder comparison view (rclone check --combined)
  Why: side-by-side diff of two locations with per-row actions is table-stakes in FreeFileSync/WinSCP and paywalled by RcloneView Plus; broader than the existing task-scoped dry-run-preview item.
  Evidence: rcloneview.com free-vs-plus; freefilesync.org comparison docs; rclone.org/commands/rclone_check (--combined output)
  Touches: new compare widget, remote_widget.cpp entry point
  Acceptance: pick two paths (local or remote), see matched/differing/missing files in a filterable list, and enqueue copy/delete actions per selection.
  Complexity: L

- [ ] P2 — Job run history with per-run stats and tray status states
  Why: every backup-class competitor (Backrest, Vorta) persists run history; the tray icon should reflect idle/syncing/error; rclone upstream asks for exactly this for bisync (#7474).
  Evidence: github.com/rclone/rclone/issues/7474; github.com/garethgeorge/backrest; github.com/borgbase/vorta
  Touches: job_widget.cpp, main_window.cpp tray code, new history store (JSON)
  Acceptance: finished jobs persist (start/end, bytes, files, errors, exit status) and are browsable; tray icon shows distinct idle/active/error states.
  Complexity: M

- [ ] P2 — Edit-in-place: open remote file, auto re-upload on save
  Why: table-stakes in WinSCP/Cyberduck, absent from every rclone GUI; pairs with the existing file-preview item.
  Evidence: docs.cyberduck.io (Edit); winscp.net docs; miroshnikov/filefive
  Touches: remote_widget.cpp, new temp-file watcher (QFileSystemWatcher + `rclone copyto`)
  Acceptance: "Open/Edit" downloads to a temp dir, opens the OS default app, and re-uploads on save with a toast; conflicts (remote changed meanwhile) prompt.
  Complexity: M

- [ ] P2 — Watch-folder live sync (keep remote up to date)
  Why: WinSCP's "keep up to date" / FreeFileSync RealTimeSync equivalent is the most requested missing workflow in rclone GUI land (RClone Manager #204) and fits a resident tray app perfectly.
  Evidence: winscp.net/eng/docs/task_keep_up_to_date; Zarestia-Dev/rclone-manager#204
  Touches: new watcher service (QFileSystemWatcher + debounce), task model, tray
  Acceptance: a task type watches a local folder and runs debounced `rclone copy/sync` on changes; survives app restart; pause/resume from tray.
  Complexity: L

- [ ] P2 — Cross-remote search
  Why: searching a filename across all configured remotes is a genuine differentiator only one niche macOS app ships; streaming results keep it responsive.
  Evidence: github.com/H4R1B0/rclone-gui (BFS streaming search); mmozeiko #64 (single-remote search already listed)
  Touches: new search dialog, item_model.cpp / lsjson workers
  Acceptance: a query fans out `lsjson -R --filter` per selected remote, streams matches into one sortable list with open-location actions; cancellable.
  Complexity: L

### P2 — CI & supply chain (supplemental)

- [ ] P3 — Fuzz the three parsers with ClusterFuzzLite
  Why: listing parser, stats/json-log parser, and the tasks.bin loader are the historically-broken surfaces; ClusterFuzzLite runs libFuzzer+ASan inside Actions (10-min PR mode) with no OSS-Fuzz onboarding.
  Evidence: google.github.io/clusterfuzzlite; item_model.cpp/job_widget.cpp/list_of_job_options.cpp parse paths
  Touches: new fuzz targets dir, CMake option, .clusterfuzzlite/ config
  Acceptance: three fuzz targets build and run in PR CI; seed corpora from recorded rclone outputs; depends on the parsing-regression-tests item for harness extraction.
  Complexity: M

### P3 — Platform & polish (supplemental)

- [ ] P3 — Windows ARM64 release artifact
  Why: Qt 6.10 ships prebuilt ARM64 packages and rclone already publishes windows-arm64 — adding the artifact is a CI matrix entry; roadmap has ARM64 Linux but not Windows.
  Evidence: doc.qt.io/qt-6/whatsnew610.html; qt.io blog Windows-on-ARM; rclone.org/downloads
  Touches: .github/workflows/build.yml
  Acceptance: tagged releases include a windows-arm64 zip that runs on a Snapdragon X device (or documented emulation-tested).
  Complexity: S

- [ ] P3 — Speed test for a remote (rclone test speed)
  Why: "is my remote slow or is it me" is a constant support question; rclone ≥1.72 ships `test speed`; no GUI exposes it.
  Evidence: rclone.org/changelog (v1.72 test speed); rclone forum perf threads
  Touches: remote_widget.cpp context menu, ProgressDialog
  Acceptance: right-click a remote → Speed test runs upload/download probes and shows MB/s results.
  Complexity: S

- [ ] P3 — Graceful stop: finish in-flight files, then stop
  Why: the only stop today is kill; finishing current files avoids partial-transfer waste — a heavily-reacted upstream ask a GUI can deliver via rc job semantics.
  Evidence: github.com/rclone/rclone/issues/966
  Touches: job_widget.cpp, engine (depends on rcd engine item)
  Acceptance: jobs offer Stop (immediate) and Finish current files; the latter completes active transfers and skips queued ones.
  Complexity: M

- [ ] P3 — Live throughput sparkline per job
  Why: speed-over-time beats an instantaneous number for diagnosing throttling; common in download managers, absent in rclone GUIs; cheap off sampled core/stats or json-log stats events.
  Evidence: github.com/rclone/rclone/issues/2899
  Touches: job_widget.cpp
  Acceptance: each running job renders a small speed-history graph alongside the existing numbers.
  Complexity: S

- [ ] P3 — Live tuning of running jobs (bwlimit / transfers / checkers)
  Why: rc exposes `core/bwlimit` and option tuning on running daemons; no GUI surfaces it; natural once the rcd engine lands.
  Evidence: github.com/rclone/rclone/issues/3898; rclone.org/rc
  Touches: job_widget.cpp, engine (depends on rcd engine item)
  Acceptance: a running job's panel lets the user change bandwidth limit and transfer concurrency without restarting the job.
  Complexity: M

- [ ] P3 — Bandwidth timetable editor
  Why: rclone's `--bwlimit` timetable syntax ("08:00,512k 19:00,off") is powerful and undiscoverable; extends the existing bandwidth-limit roadmap item with a week-grid editor.
  Evidence: rclone.org/docs (--bwlimit timetable); github.com/H4R1B0/rclone-gui
  Touches: preferences_dialog.cpp or transfer_dialog.cpp
  Acceptance: a grid editor produces/parses the timetable string; tooltip shows the generated flag.
  Complexity: M

- [ ] P3 — OS shell integration: Explorer "Send to remote…" / Finder Services
  Why: pushing files without opening the app is the "native feel" differentiator a Qt app can own against webview competitors.
  Evidence: Zarestia-Dev/rclone-manager#80; github.com/H4R1B0/rclone-gui (Finder Services)
  Touches: installer/registry (Windows SendTo), macOS Services plist, main.cpp argument handling (depends on SingleApplication item)
  Acceptance: right-click a file in Explorer/Finder → send to a chosen remote folder; routes into the running instance's job queue.
  Complexity: M

- [ ] P3 — Condition-gated schedules: unmetered network / AC power
  Why: Vorta/Round-Sync gate runs on power/network conditions; laptops syncing over hotspots is the failure mode; extends the scheduled-tasks item once it exists.
  Evidence: github.com/borgbase/vorta (unmetered-only); newhinton/Round-Sync#96
  Touches: scheduler (depends on scheduled-tasks item), platform power/network probes
  Acceptance: a scheduled task can require AC power and/or non-metered network; skipped runs are logged with the reason.
  Complexity: M

- [ ] P3 — VSS snapshot option for locked files (Windows)
  Why: copying open files (Outlook PSTs, VMs) fails without Volume Shadow Copy; FreeFileSync ships it; rclone can read from a vshadow-exposed path.
  Evidence: github.com/rclone/rclone/issues/990; freefilesync.org
  Touches: transfer_dialog.cpp (option), elevated helper for vshadow
  Acceptance: a local-source task can opt into "snapshot locked files"; requires elevation; gracefully explains when unavailable.
  Complexity: L

- [ ] P3 — cryptcheck mode in the check UI
  Why: crypt users cannot verify integrity via plain check; one extra mode on the planned check feature.
  Evidence: Zarestia-Dev/rclone-manager#218; rclone.org/commands/rclone_cryptcheck
  Touches: check feature (depends on existing rclone-check item)
  Acceptance: when the target is a crypt remote, the check dialog offers cryptcheck against the plaintext source.
  Complexity: S

- [ ] P3 — Backup-dir retention policy UI
  Why: `--backup-dir` with dated folders + retention (keep N / prune older than X) is the script-land standard (rclone_jobber/dfb) no GUI offers; pairs with the version-browser item.
  Evidence: github.com/awesome-rclone/awesome-rclone (rclone_jobber, dfb); newhinton/Round-Sync#182
  Touches: job_options.cpp/.h, transfer_dialog.cpp, a prune step on job completion
  Acceptance: a task can enable dated backup-dirs with a retention rule the app enforces after successful runs.
  Complexity: M

- [ ] P3 — Bookmarks for deep remote paths with per-remote accent colors
  Why: FTP-client table-stakes (Cyberduck bookmarks, WinSCP session colors) missing from rclone GUIs; colors are a cheap wrong-target-sync safety affordance.
  Evidence: docs.cyberduck.io/cyberduck/bookmarks/; miroshnikov/filefive; newhinton/Round-Sync#334
  Touches: remote_widget.cpp, main_window.cpp tabs, settings
  Acceptance: pin remote:path bookmarks reachable from the main list and tab context menu; each remote can have an accent color shown on its tab.
  Complexity: S

- [ ] P3 — File properties pane with rclone metadata
  Why: rclone 1.70+ syncs/exposes real metadata (`-M`, lsjson directory metadata, hashes via operations/hashsum) — a properties dialog showing modtime/hashes/metadata is now possible and aids verification.
  Evidence: rclone.org/changelog (v1.70 metadata era; operations/hashsum)
  Touches: remote_widget.cpp context menu, new properties dialog
  Acceptance: Properties on a file shows size, modtime, available hashes (computed on demand), and backend metadata when supported.
  Complexity: M

- [ ] P3 — Bisync conflict-resolution dialog
  Why: bisync is stable since rclone 1.71 with `--conflict-resolve/--conflict-loser/--conflict-suffix`; an interactive keep-local/keep-remote/keep-both chooser was archived Celeste's signature feature and de-risks the planned bisync GUI.
  Evidence: forum.rclone.org rclone 1.71 release notes; github.com/hwittenborn/celeste
  Touches: bisync feature (depends on existing bisync item)
  Acceptance: conflicts detected in a bisync run present a per-file resolution list before re-running with the chosen strategy.
  Complexity: M

- [ ] P3 — REUSE 3.3 licensing compliance
  Why: machine-verifiable SPDX licensing is increasingly expected by distros (KDE/Fedora) and OpenSSF Scorecard; one-day chore.
  Evidence: reuse.software/spec-3.3; github.com/fsfe/reuse-tool
  Touches: REUSE.toml (new), per-file SPDX headers, CI lint step
  Acceptance: `reuse lint` passes in CI.
  Complexity: S

### 2026-06-12 Refresh — P1

- [ ] P1 — Bump minimum safe rclone version warning to v1.74.3
  Why: CVE-2026-49980 (CVSS 9.8) allows inline remotes to bypass global config restrictions for arbitrary command execution; v1.73.5 only fixed CVE-2026-41176/41179.
  Evidence: github.com/rclone/rclone/releases/tag/v1.74.3; ccb.belgium.be rclone advisory
  Touches: `src/main_window.cpp` rclone version check logic
  Acceptance: startup warning fires for rclone < 1.74.3 (not 1.73.5); advisory link updated.
  Complexity: S

- [ ] P1 — macOS x86_64 release artifact
  Why: release.yml builds ARM64 only (macos-14 runner); Intel Mac users cannot use official releases. Universal or separate x86_64 build needed.
  Evidence: `.github/workflows/release.yml:73-100`; GitHub macos-13 runners provide x86_64
  Touches: `.github/workflows/release.yml` (add macos-13 matrix leg or universal build)
  Acceptance: tagged releases include a macOS x86_64 (or universal) DMG + app zip alongside the ARM64 artifacts.
  Complexity: S


### 2026-06-12 Refresh — P2

- [ ] P2 — Add runtime capability and dependency gates
  Why: rclone, WinFsp, macFUSE/fuse-t, and Qt features now change quickly; the UI should disable unsupported actions with exact reasons instead of failing at runtime.
  Evidence: rclone changelog; Qt release table; WinFsp CVE-2026-3006 advisory; `src/main_window.cpp:1271-1289`
  Touches: new `RcloneCapabilities` helper, `src/main_window.cpp`, preferences/about dialog, mount and feature action enablement
  Acceptance: About/Preferences show detected rclone version, config path, Qt version, mount backend/version, and key feature support; unsupported actions are disabled or warned with the minimum version/backend required.
  Complexity: M

- [ ] P2 — Modernize manual release scripts to the supported toolchain
  Why: Release helper scripts still encode Qt 5.13.2, Visual Studio 2019 32-bit paths, OpenSSL 1.1.1d, and CentOS 7 assumptions that conflict with current CI and dependency security policy.
  Evidence: `scripts/release_windows.cmd`; `scripts/release_AppImage.sh`; Qt release table; Qt/OpenSSL advisory cadence
  Touches: `scripts/release_windows.cmd`, `scripts/release_AppImage.sh`, release documentation once scripts are accurate
  Acceptance: scripts target the same Qt/architecture/version metadata as CI, avoid OpenSSL 1.1.1 references, fail fast on missing dependencies, and produce artifacts matching the planned release workflow.
  Complexity: M

- [ ] P2 — Add redacted diagnostics and support bundle
  Why: Competitor issue trackers repeatedly show users lose finished-job errors or cannot report enough context; this app currently keeps most process output inside transient UI widgets.
  Evidence: RClone Manager finished-job error reports; Round Sync no-error-log reports; `src/job_widget.cpp`; `src/mount_widget.cpp`
  Touches: new diagnostics collector, Help/About menu, `src/job_widget.cpp`, `src/mount_widget.cpp`, config redaction utilities
  Acceptance: Help offers Copy Diagnostics and Save Support Bundle with app/Qt/rclone versions, OS, config path, mount backend, recent job/mount stderr, and redacted secrets including config passwords, tokens, rc credentials, and command-line sensitive flags.
  Complexity: M

- [ ] P2 — Paged directory listing for large remotes (lsjson ListP)
  Why: item_model.cpp loads entire lsjson output in one QProcess read; rclone v1.72 added paged listing (ListP) across 12 backends. Million-file directories can OOM or hang the UI; streaming pages enables progressive display.
  Evidence: rclone.org/changelog v1.72 (ListP); `src/item_model.cpp` QProcess readAll pattern
  Touches: `src/item_model.cpp`, lsjson invocation args
  Acceptance: listing streams pages into the tree model progressively; UI remains responsive during large directory loads; fallback to single-shot for rclone < 1.72.
  Complexity: M

- [ ] P2 — Enrich diagnostics with core/version and core/disks RC endpoints
  Why: rclone v1.74 `core/version` now reports osVersion, osArch, osKernel; v1.73 `core/disks` enumerates attached disks. Both improve support bundle quality and mount destination UX.
  Evidence: rclone.org/rc core/version, core/disks; existing diagnostics roadmap item
  Touches: diagnostics collector (depends on diagnostics item), mount dialog disk picker
  Acceptance: diagnostics bundle includes rclone-reported OS and architecture; mount dialog can offer detected local disks/drives.
  Complexity: S

- [ ] P2 — Linux ARM64 (aarch64) AppImage release artifact
  Why: existing roadmap has ARM64 Linux builds but release.yml only has x86_64; Raspberry Pi 5, ARM cloud instances, and Asahi Linux are growing targets.
  Evidence: `.github/workflows/release.yml`; existing P4 ARM64 Linux item; Ubuntu 24.04 has native aarch64 Qt6 packages
  Touches: `.github/workflows/release.yml` (add ubuntu-24.04-arm64 or QEMU cross-build matrix leg)
  Acceptance: tagged releases include a linux-aarch64 AppImage; tested on ARM64 runner or documented QEMU-validated.
  Complexity: M
  Note: Implements the existing P4 ARM64 Linux builds item with a concrete CI plan.

### 2026-06-12 Refresh — P3

- [ ] P3 — Searchable and filterable remotes selector
  Why: users with 10+ remotes waste time scrolling; rclone-ui added searchable remotes in v3.5.4; cheap typeahead filter on the existing remotes list.
  Evidence: rcloneui.com/changelog v3.5.4; rclone forum usability thread
  Touches: `src/main_window.cpp` remotes list, `src/main_window.ui`
  Acceptance: a filter/search field above or inline with the remotes list narrows displayed remotes as the user types; clears on Escape.
  Complexity: S

- [ ] P3 — Expose rclone archive command in file browser
  Why: rclone v1.72 added `archive` for moving old files to a dated archive structure; no GUI exposes it. Natural right-click action on remote folders.
  Evidence: rclone.org/commands/rclone_archive (v1.72)
  Touches: `src/remote_widget.cpp` context menu, ProgressDialog
  Acceptance: right-click a remote folder > Archive runs `rclone archive` with configurable age threshold; gated on rclone >= 1.72.
  Complexity: S

- [ ] P3 — Consolidate duplicated getNiceSize() utility
  Why: identical size-formatting function exists in both `item_model.cpp:19-31` and `job_widget.cpp:8-20`; DRY violation and divergence risk.
  Evidence: `src/item_model.cpp:19-31`; `src/job_widget.cpp:8-20`
  Touches: `src/utils.h`, `src/utils.cpp`, `src/item_model.cpp`, `src/job_widget.cpp`
  Acceptance: single `getNiceSize()` in utils; both callers use it; build green.
  Complexity: S

- [ ] P3 — Detect macFUSE FSKit backend capability
  Why: macFUSE 5.2.0 (April 2026) ships FSKit backend for fully userspace mounts on macOS 26; fuse-t also supports FSKit. Current mount detection doesn't distinguish FSKit-capable vs kext-only builds.
  Evidence: macfuse.github.io/2026/04/09/macfuse-5.2.0.html; fuse-t.org; `src/mount_backend.cpp`
  Touches: `src/mount_backend.cpp`, mount dialog status/help text
  Acceptance: mount backend detection reports FSKit availability on macOS 26+; mount dialog notes when running in FSKit vs kext mode.
  Complexity: S

- [ ] P3 — Retire stale roadmap items already implemented
  Why: ROADMAP lists "Drag & drop to upload" (P3) and "Public link generation" (P3) as open, but both are already implemented (multi-file drag-drop in CHANGELOG Unreleased; public link button exists since v1.4).
  Evidence: CHANGELOG.md Unreleased section; `src/remote_widget.cpp` link button
  Touches: ROADMAP.md
  Acceptance: stale items removed or marked done; no regressions.
  Complexity: S

- [ ] P3 — Leverage job/batch RC endpoint for multi-command operations
  Why: rclone v1.72 added `job/batch` for concurrent RC command batches; the rcd engine can use this for multi-file operations (delete multiple, move multiple) in a single round-trip instead of sequential spawns.
  Evidence: rclone.org/rc (job/batch endpoint, v1.72); existing rcd engine roadmap
  Touches: `src/rclone_rc_engine.cpp` (depends on rcd engine maturity)
  Acceptance: batch-capable operations (multi-delete, multi-move) use job/batch when available; fallback to sequential for older rclone.
  Complexity: M

- [ ] P3 — Show new hash algorithms in file properties (BLAKE3, XXH3, XXH128)
  Why: rclone v1.71 added BLAKE3, XXH3, XXH128 hashes; the planned file properties dialog should expose all available hash types, not just MD5/SHA1.
  Evidence: rclone.org/changelog v1.71; existing file properties roadmap item
  Touches: file properties dialog (depends on existing properties item)
  Acceptance: properties dialog lists all hash algorithms the backend supports; hash computed on demand; new algorithms displayed when available.
  Complexity: S
