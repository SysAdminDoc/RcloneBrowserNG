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

### 2026-06-16 Audit Findings (deferred)






### 2026-06-16 Research Refresh

> Appended from exhaustive codebase audit + ecosystem research. Verified rclone v1.74.3 (still latest), Qt 6.11.1 (new latest), macFUSE 5.3.1, GitHub Actions runner migrations (macos-13 deprecated, macos-latest → macOS 26), competitor updates (rclone-ui 2,100 stars, RClone Manager 933 stars, yet-another-rclone-dashboard 125 stars new entrant), Flatpak 1.16.6 (CVE-2026-34079), Resticprofile v0.33 (missed-task auto-run), Duplicati v2.3, Backrest v1.13. 60+ sources consulted. See RESEARCH.md.
>
> Housekeeping: removed completed items — Linux ARM64 AppImage (in release.yml), RcloneRcEngine async migration (done, only startup ping retains QEventLoop), dead macOS 10.9 code (removed), Flatpak desktop ID alignment (done), streaming JSON parsing (done), --list-cutoff (done). Updated RESEARCH.md competitive landscape and architecture assessment.

#### P1 — CI & security


#### P2 — Code hygiene

#### P3 — Features

- [ ] P3 — Cloud trash for all supporting backends
  Why: trash operations (`--drive-use-trash`) only apply to Google Drive. rclone supports native trash for 10+ providers (OneDrive, Dropbox, etc. via `--<backend>-use-trash` or backend-specific flags). H4R1B0/rclone-gui implements cloud trash for all providers — this is a competitive gap.
  Evidence: H4R1B0/rclone-gui v1.4.6+ (10-provider trash); rclone.org backend docs
  Touches: `src/remote_widget.cpp` (delete action), `src/item_model.cpp` (trash listing)
  Acceptance: delete operations on remotes that support native trash use the appropriate backend flag; Google Drive Trash browser generalizes to other providers.
  Complexity: M



- [ ] P3 — Cron-expression scheduling with human-readable preview
  Why: the scheduler currently offers fixed intervals (15m/30m/hourly/daily/weekly). rclone-ui offers editable cron expressions with a preview of the next N upcoming runs — the UX gold standard. Power users want "every weekday at 2 AM" or "first Sunday of the month."
  Evidence: rclone-ui v3.5.0+ cron scheduling; rcloneui.com/changelog
  Touches: `src/schedule_manager.cpp`, schedule UI in `src/main_window.cpp`
  Acceptance: scheduling dialog offers both simple intervals and a freeform cron expression; a preview shows the next 5 run times; cron expressions are validated before saving.
  Complexity: M





### P0 — Security and data-safety

- [ ] P0 — Raise packaged Qt baseline and fail vulnerable Qt builds
  Why: Windows x64 build/release still installs Qt 6.7.*, which is in the affected range for Qt SVG CVE-2026-6210; users should not receive newly built binaries on a known-vulnerable Qt line.
  Evidence: Qt CVE-2026-6210 advisory; `.github/workflows/build.yml`; `.github/workflows/release.yml`
  Touches: `.github/workflows/build.yml`, `.github/workflows/release.yml`, CMake configure logs, release notes
  Acceptance: Windows x64 CI/release uses Qt >=6.8.8 or >=6.11.1; CI prints and validates the Qt version; release fails if the deployed Qt baseline is vulnerable or bundles a vulnerable SVG plugin.
  Complexity: M

- [ ] P0 — Add dry-run and preview non-mutation contract tests
  Why: rclone-ui issue #218 shows users can lose data if preview/dry-run ever executes a real sync; RcloneBrowserNG has multiple dry-run, saved-task, and staging entry points without GUI contract tests.
  Evidence: rclone-ui #218; `src/transfer_dialog.cpp`; `src/main_window.cpp`; `src/job_options.cpp`
  Touches: `src/transfer_dialog.cpp`, `src/main_window.cpp`, `src/job_options.cpp`, new Qt/CMake test target
  Acceptance: tests simulate Dry Run, Run, Save Task, Run Task dry-run, and staged enqueue paths; preview commands always include `--dry-run`; no preview path starts/enqueues a mutating job; CI runs the suite.
  Complexity: M

### P1 — Reliability and backend truth

- [ ] P1 — Add backend capability registry for action gating
  Why: rclone exposes backend feature flags, but the UI still hard-codes or optimistically offers some backend-dependent actions; this should become a shared capability layer instead of per-action branching.
  Evidence: rclone `operations/fsinfo`; rclone `backend features`; `src/remote_widget.cpp`; existing P3 cloud-trash item
  Touches: new capability service, `src/remote_widget.cpp`, `src/main_window.cpp`, `src/rclone_capabilities.cpp`
  Acceptance: opening a remote fetches/caches feature flags; public link, server-side copy/move, hash, quota, version/restore, cleanup, and future trash actions are enabled, hidden, or explained based on actual backend support with a safe fallback for older rclone.
  Complexity: L

- [ ] P1 — Make native schedules catch up and verify generated definitions
  Why: Linux timers use `Persistent=true`, but Windows schedules do not set `StartWhenAvailable`; scheduler definitions are generated without golden tests despite recent cross-platform scheduler churn.
  Evidence: Microsoft `StartWhenAvailable`; resticprofile missed-run behavior; `src/schedule_manager.cpp`
  Touches: `src/schedule_manager.cpp`, scheduler UI in `src/main_window.cpp`, new scheduler golden tests
  Acceptance: schedule UI includes a "run missed jobs when available" option where supported; Windows task XML/settings use `StartWhenAvailable`; Linux/macOS/Windows generated definitions have tests for hourly/daily/weekly/custom intervals; schedule status surfaces last, next, and missed-run state where the OS exposes it.
  Complexity: M

- [ ] P1 — Store per-file job audit detail with redacted export
  Why: current job history records only summary counts, while competitors emphasize visual sync history and retained transfer evidence; users need to prove what changed and debug failures without parsing transient output.
  Evidence: RcloneView visual sync history; Cyberduck retained transfer list; `src/job_history.cpp`; `src/job_widget.cpp`
  Touches: `src/job_history.cpp`, `src/job_widget.cpp`, `src/main_window.cpp`, job history UI/export
  Acceptance: each completed job links to a detail record of transferred, skipped, deleted, retried, and failed paths with timestamps and redacted secrets; UI can inspect and export the detail; existing summary history remains backward compatible.
  Complexity: L

### P2 — Trust, accessibility, and diagnostics

- [ ] P2 — Publish release SBOMs alongside checksums and attestations
  Why: releases already have SHA256 sums and GitHub artifact attestations; SBOMs close the supply-chain traceability gap for bundled Qt plugins, installers, and native packages.
  Evidence: Qt 6.11 SBOM/CycloneDX docs; GitHub artifact attestation docs; `.github/workflows/release.yml`
  Touches: `.github/workflows/release.yml`, release artifact layout, release verification docs
  Acceptance: each release uploads an SPDX or CycloneDX SBOM covering app binaries, bundled Qt modules/plugins, packaging tools, and key runtime dependencies; CI validates SBOM generation; release notes link checksum, attestation, and SBOM verification steps.
  Complexity: M

- [ ] P2 — Add an accessibility and high-contrast acceptance pass
  Why: the app sets many accessible names but has no systematic acceptance gate for descriptions, tab order, keyboard-only completion, high-contrast palettes, or screen-reader semantics.
  Evidence: Qt `QAccessibilityHints`; `src/interface_polish.cpp`; `.ui` tabstop definitions
  Touches: `src/interface_polish.cpp`, `src/*.ui`, main dialogs/widgets, accessibility test checklist or static test
  Acceptance: primary dialogs pass keyboard-only workflows; controls have meaningful names/descriptions/tooltips where needed; custom colors respect high-contrast/system palette; automated or scripted checks cover tab order and missing labels.
  Complexity: M

- [ ] P2 — Add a remote health and repair view
  Why: users currently discover expired tokens, unsupported backend APIs, old rclone versions, quota issues, and missing mount dependencies piecemeal through dialogs; competitors surface usage and unsupported-remote states more directly.
  Evidence: RClone Manager remote overview; RcloneView unsupported remote fallback; `src/main_window.cpp`; `src/remote_provider.cpp`
  Touches: `src/main_window.cpp`, `src/remote_provider.cpp`, `src/rclone_capabilities.cpp`, preferences/diagnostics UI
  Acceptance: remotes list or a diagnostics panel shows rclone version/security status, token/listing status, quota if supported, mount dependency readiness, and backend capability warnings; repair actions include reconnect, open config, refresh capabilities, and copy diagnostics.
  Complexity: M

### P3 — Product polish and discoverability

- [ ] P3 — Add safe inline preview for common file types
  Why: REM, RClone Manager, yet-another-rclone-dashboard, and H4R1B0/rclone-gui all use preview as a core file-manager affordance; RcloneBrowserNG has open/stream flows but no bounded read-only preview.
  Evidence: REM README; RClone Manager README; yet-another-rclone-dashboard README; `src/remote_widget.cpp`; `src/stream_widget.cpp`
  Touches: `src/remote_widget.cpp`, preview dialog/widget, temporary-download handling, size/type limits
  Acceptance: image, text, PDF, audio, and video previews open read-only within explicit size and backend limits; unsafe or large files show a clear fallback; temp files are cleaned up; preview never mutates remote content.
  Complexity: L

- [ ] P3 — Add saved cross-remote search filters and history
  Why: current search supports one filename pattern across all remotes, while native and web competitors expose richer filters and saved search affordances for large multi-cloud libraries.
  Evidence: H4R1B0/rclone-gui search filters; `src/cross_remote_search.cpp`; rclone `lsjson`
  Touches: `src/cross_remote_search.cpp`, settings persistence, search result actions
  Acceptance: search supports remote selection, file type, size, modified-date filters, and the last N searches; double-click/open-location still works; empty/error states explain skipped or failed remotes.
  Complexity: M
