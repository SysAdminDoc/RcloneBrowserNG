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



- [ ] **Auto-update capability** — Check for and apply updates to both RcloneBrowserNG and rclone.  
  _Sources: kapitainsky #249, #195_

### Task Management








### File Browser / Navigation






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


- [ ] P3 — Backup-dir retention policy UI
  Why: `--backup-dir` with dated folders + retention (keep N / prune older than X) is the script-land standard (rclone_jobber/dfb) no GUI offers; pairs with the version-browser item.
  Evidence: github.com/awesome-rclone/awesome-rclone (rclone_jobber, dfb); newhinton/Round-Sync#182
  Touches: job_options.cpp/.h, transfer_dialog.cpp, a prune step on job completion
  Acceptance: a task can enable dated backup-dirs with a retention rule the app enforces after successful runs.
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


### 2026-06-13 Research Refresh

> Appended from exhaustive competitive landscape research, community signal mining (Reddit/HN/rclone forum), platform ecosystem scan (Qt 6.9-6.10, rclone v1.74.x, GitHub Actions), and adjacent-domain pattern analysis (FreeFileSync, Vorta, Dolphin, JDownloader, Resticprofile, WinSCP, Cyberduck, Pika Backup). 60+ distinct sources consulted. See RESEARCH.md for full findings.
>
> Housekeeping: removed stale items "Drag & drop to upload" (implemented) and "Public link generation" (implemented since v1.4).

#### P2 — Onboarding & UX

#### P2 — Config & trust

#### P2 — Transfer UX


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



#### P2 — Monitoring


#### P3 — UX refinements

- [ ] P3 — Non-blocking background error queue
  Why: WinSCP's two-mode error prompt system — errors from background transfers queue for review instead of popping modal dialogs. Auto-popup when the main connection is idle, or show a visual indicator on the queue row for manual response. Currently RcloneBrowserNG uses modal `QMessageBox` for all error/confirmation paths, which blocks the GUI during multi-job workflows.
  Evidence: winscp.net/eng/docs/transfer_queue (queue interaction model); `src/job_widget.cpp`; `src/mount_widget.cpp`
  Touches: new error queue model, `src/job_widget.cpp`, `src/mount_widget.cpp`, status bar indicator
  Acceptance: errors from running jobs accumulate in a non-modal queue; a badge on the Jobs tab or status bar shows the count; clicking opens the queue for review; no modal dialogs block the GUI during background transfers.
  Complexity: M


- [ ] P3 — Drag between remote tabs for cross-remote transfer
  Why: remote-to-remote copy is an existing P3 feature request. Dragging a file from one open remote tab to another is the most intuitive initiation path. Qt's drag-and-drop framework supports cross-widget drops natively — the tab bar accepts a drop to switch to the target tab, then the tree view accepts the drop to set the destination path.
  Evidence: existing P3 "Remote-to-remote transfers" item; mmozeiko #27; Qt drag-and-drop docs
  Touches: `src/remote_widget.cpp` (drag source), `src/main_window.cpp` (tab bar drop target), transfer dialog (pre-fill source and dest from drag)
  Acceptance: drag a file/folder from one remote tab to the tab header of another remote; the target tab activates; dropping into the tree opens a transfer dialog pre-filled with source and destination paths.
  Complexity: M

