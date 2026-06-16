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



- [ ] **Auto-update capability** — Check for and apply updates to both RcloneBrowserNG and rclone.  
  _Sources: kapitainsky #249, #195_

### Task Management






- [ ] **Convert tasks.bin to human-readable format** — Replace binary task storage with JSON/XML.  
  _Sources: kapitainsky #143_


### File Browser / Navigation
- [ ] **Dual-pane interface** — Side-by-side local/remote or remote/remote browsing.  
  _Sources: kapitainsky #71; mmozeiko #80, #98_






### Advanced Operations








### Mount Enhancements




### Monitoring & Logging



### Miscellaneous
- [ ] **i18n / multi-language support** — Internationalization framework with translations.  
  _Sources: kapitainsky #138, #47; rclone forum (Portuguese, others)_











---

## P4 — Platform & Compatibility

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
| [rclone-ui](https://github.com/rclone-ui/rclone-ui) | 2,076 | TypeScript/Tauri | Active (v3.6.0) | Cron, dual-panel Commander, Cloudflare tunnel mobile, 7 langs, WinGet/Homebrew/Flathub. **Caution: #218 dry-run may execute real sync** |
| [RClone Manager](https://github.com/Zarestia-Dev/rclone-manager) | 922 | Angular/Tauri | Active (v0.2.7) | Fastest-growing (0→922 in 14mo). FS watchers, webhook/email/Telegram alerts, Docker headless, Crowdin i18n |
| [Celeste](https://github.com/hwittenborn/celeste) | 1,616 | Rust/GTK4 | **Archived Nov 2025** | Was Linux-only bisync — lane now open |
| [rclone-webui-react](https://github.com/rclone/rclone-webui-react) | 1,562 | JavaScript | Stale (5yr no release) | Official rclone web UI, bundled with `rclone rcd` |
| [rem](https://github.com/liriliri/rem) | 602 | Electron | Dormant (6mo) | "Open source RcloneView" — file preview, two-panel |
| [rclone-webui-angular](https://github.com/yuudi/rclone-webui-angular) | 359 | Angular | Active | Web alternative to official GUI |
| [H4R1B0/rclone-gui](https://github.com/H4R1B0/rclone-gui) | 0 | Swift | Active (v1.6.0, 521 tests) | macOS-native. Cloud trash, bookmarks, bulk rename, search. Zero visibility despite quality |
| [RcloneShuttle](https://github.com/pieterdd/RcloneShuttle) | 149 | Rust/GTK4 | Active | Linux-native GTK4, upload-only, deliberately minimal |
| [rclone-rc-web-gui](https://github.com/retifrav/rclone-rc-web-gui) | 125 | TypeScript | Active | Lightweight RC web GUI |
| [Motuz](https://github.com/FredHutch/motuz) | 114 | JavaScript | Active | Enterprise/scientific multi-user TB-scale transfers |

RcloneBrowserNG's niche: **lightweight native Qt desktop app** — faster startup, lower memory, no Electron/Tauri runtime, no browser tab. The value proposition is a well-maintained, modern C++/Qt6 app that "just works" with current rclone on all desktop platforms. Community signal: strong anti-paywall and anti-AI-vibe-coded sentiment favors a free, well-engineered native GUI.

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

### P2 — Trust / differentiator features





- [ ] P3 — Versioning / snapshot browser (S3 versions / --backup-dir) with restore
  Why: only S3Drive (paid) does it; rclone backends support it; zero free desktop competition.
  Evidence: s3drive.app
  Touches: remote_widget.cpp, engine
  Acceptance: for capable remotes, browse prior versions / backup-dir snapshots and restore a selected one.
  Complexity: L

### P3 — Hygiene


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



### P2 — Mount & remote trust (supplemental)



### P2 — Differentiator features (supplemental)

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


- [ ] P3 — Graceful stop: finish in-flight files, then stop
  Why: the only stop today is kill; finishing current files avoids partial-transfer waste — a heavily-reacted upstream ask a GUI can deliver via rc job semantics.
  Evidence: github.com/rclone/rclone/issues/966
  Touches: job_widget.cpp, engine (depends on rcd engine item)
  Acceptance: jobs offer Stop (immediate) and Finish current files; the latter completes active transfers and skips queued ones.
  Complexity: M


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


### 2026-06-12 Refresh — P1

### 2026-06-12 Refresh — P2







### 2026-06-12 Refresh — P3

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

### 2026-06-13 Research Refresh

> Appended from exhaustive competitive landscape research, community signal mining (Reddit/HN/rclone forum), platform ecosystem scan (Qt 6.9-6.10, rclone v1.74.x, GitHub Actions), and adjacent-domain pattern analysis (FreeFileSync, Vorta, Dolphin, JDownloader, Resticprofile, WinSCP, Cyberduck, Pika Backup). 60+ distinct sources consulted. See RESEARCH.md for full findings.
>
> Housekeeping: removed stale items "Drag & drop to upload" (implemented) and "Public link generation" (implemented since v1.4).

#### P2 — Onboarding & UX

#### P2 — Config & trust

#### P2 — Transfer UX

- [ ] P2 — Transfer staging queue with batch review before execution
  Why: JDownloader's LinkGrabber pattern — operations go to a review queue before executing. Users build up a batch of copy/sync/move operations across multiple remotes, review the set, then commit. Distinct from the "job queue manager" item (which is about concurrency control for running jobs). FreeFileSync and Krusader also use staging.
  Evidence: jdownloader.org (LinkGrabber); freefilesync.org; krusader.org
  Touches: new staging model, `src/main_window.cpp` (queue panel), `src/transfer_dialog.cpp`
  Acceptance: transfers can be enqueued for review instead of immediate execution; staging list shows all pending ops; user can reorder, remove, or batch-execute; cleared after execution.
  Complexity: M

#### P3 — CI & supply chain


#### P3 — Scheduler enhancements

- [ ] P3 — Staleness detection and overdue-job alerting
  Why: BorgBase's killer feature — continuously watches last-sync timestamps and alerts if a backup is overdue. "Nothing happened" is the hardest failure mode to catch. No rclone GUI does this. Depends on the scheduler item.
  Evidence: borgbase.com (staleness monitoring); github.com/borgbase/vorta; creativeprojects.github.io/resticprofile (status file)
  Touches: scheduler (depends on scheduled-tasks item), tray icon, notification system
  Acceptance: scheduled tasks track last-run timestamp; when a task exceeds its expected interval by a configurable margin, badge the tray icon and show a notification; overdue tasks highlighted in the task list.
  Complexity: S (once scheduler exists)

- [ ] P3 — Adaptive bandwidth throttling based on user network activity
  Why: Free Download Manager's "traffic usage modes" auto-throttle downloads when the user is actively browsing and use full speed when idle. Extends the existing bandwidth limit and timetable editor items with an automatic mode.
  Evidence: freedownloadmanager.org; rclone.org/docs (--bwlimit); existing bandwidth roadmap items
  Touches: rclone rc `core/bwlimit`, network activity detection (platform-specific)
  Acceptance: an "Adaptive" bandwidth mode monitors outbound traffic; throttles rclone when other apps are using bandwidth; ramps to full speed during idle; user can configure floor/ceiling speeds.
  Complexity: L

### 2026-06-15 Research Refresh

> Appended from exhaustive codebase audit + ecosystem research. Verified rclone v1.74.3 (latest stable), Qt 6.10.3, GitHub Actions VS2026 migration, macFUSE 5.3.1 dev preview, Flathub AI ban, competitor updates (rclone-ui 2,100 stars, RClone Manager 928 stars). 40+ sources consulted. See RESEARCH.md.
>
> Housekeeping: removed completed items — rclone 1.74.3 version check (implemented), macOS x86_64 release artifact (release.yml has macos-13 matrix), CodeQL v4 (deployed), getNiceSize() consolidation (in utils.cpp), macFUSE FSKit detection (mount_backend.cpp).

#### P2 — Distribution

#### P3 — Cleanup


#### P2 — Scheduler & automation

- [ ] P2 — Generate native OS scheduled tasks instead of an in-app timer
  Why: the #1 community request ("Scheduled tasks / cron", P3) needs a reliable implementation. Resticprofile's pattern — one config, four native backends (systemd timer+service, launchd plist, Windows Task Scheduler XML, crontab) — is the gold standard. Vorta's QTimer-only approach is a cautionary tale: if the app isn't running, no backups occur. Native tasks survive reboots, crashes, and logouts without the app running as a tray daemon.
  Evidence: creativeprojects.github.io/resticprofile/schedules; github.com/borgbase/vorta/issues/294 (users begging for OS-level scheduling); forum.rclone.org scheduler threads
  Touches: new `ScheduleGenerator` module, saved tasks model, platform-specific generators (systemd .timer/.service, launchd .plist, schtasks.exe XML), `src/main_window.cpp` task panel
  Acceptance: a saved task can be "installed" as a native OS scheduled task; the app lists installed schedules with status; uninstall removes the OS entry; works when the app is not running.
  Complexity: L
  Note: Supersedes the existing P3 "Scheduled tasks / cron" item with a concrete architecture. The three-tier permission model (system/user/user_logged_on from Resticprofile) should be adopted.


#### P2 — Monitoring


#### P3 — UX refinements

- [ ] P3 — Non-blocking background error queue
  Why: WinSCP's two-mode error prompt system — errors from background transfers queue for review instead of popping modal dialogs. Auto-popup when the main connection is idle, or show a visual indicator on the queue row for manual response. Currently RcloneBrowserNG uses modal `QMessageBox` for all error/confirmation paths, which blocks the GUI during multi-job workflows.
  Evidence: winscp.net/eng/docs/transfer_queue (queue interaction model); `src/job_widget.cpp`; `src/mount_widget.cpp`
  Touches: new error queue model, `src/job_widget.cpp`, `src/mount_widget.cpp`, status bar indicator
  Acceptance: errors from running jobs accumulate in a non-modal queue; a badge on the Jobs tab or status bar shows the count; clicking opens the queue for review; no modal dialogs block the GUI during background transfers.
  Complexity: M

- [ ] P3 — Quick bandwidth snail toggle
  Why: Free Download Manager's one-click "Snail" icon drops all operations to a crawl without disconnecting — simpler than the existing bandwidth slider and timetable editor items. A single toolbar/tray icon that toggles between full-speed and a configurable floor rate via `core/bwlimit` RC. Solves the "I need my connection back NOW" use case without opening preferences.
  Evidence: freedownloadmanager.org (Snail mode); rclone.org/rc (core/bwlimit)
  Touches: `src/main_window.cpp` (toolbar/tray action), `src/rclone_rc_engine.cpp` (core/bwlimit call), preferences (floor speed)
  Acceptance: a toggle button in the toolbar or tray context menu switches all running rclone jobs between full-speed and a configurable throttle; visual state change (icon/color) confirms the mode; depends on rcd engine for rc-managed jobs.
  Complexity: S

- [ ] P3 — Drag between remote tabs for cross-remote transfer
  Why: remote-to-remote copy is an existing P3 feature request. Dragging a file from one open remote tab to another is the most intuitive initiation path. Qt's drag-and-drop framework supports cross-widget drops natively — the tab bar accepts a drop to switch to the target tab, then the tree view accepts the drop to set the destination path.
  Evidence: existing P3 "Remote-to-remote transfers" item; mmozeiko #27; Qt drag-and-drop docs
  Touches: `src/remote_widget.cpp` (drag source), `src/main_window.cpp` (tab bar drop target), transfer dialog (pre-fill source and dest from drag)
  Acceptance: drag a file/folder from one remote tab to the tab header of another remote; the target tab activates; dropping into the tree opens a transfer dialog pre-filled with source and destination paths.
  Complexity: M

- [ ] P3 — Expose --dump curl in diagnostics support bundle
  Why: rclone v1.74.0 added `--dump curl` to export HTTP requests as curl commands for debugging. Including a curl-dump capture in the diagnostics/support bundle (existing P2 item) makes bug reports self-contained and reproducible.
  Evidence: rclone.org/changelog v1.74.0 (--dump curl); existing P2 diagnostics item
  Touches: diagnostics collector (depends on diagnostics item), optional `--dump curl` flag on jobs
  Acceptance: the support bundle can optionally include a curl-dump log from the last job; sensitive headers (auth tokens) are redacted.
  Complexity: S
