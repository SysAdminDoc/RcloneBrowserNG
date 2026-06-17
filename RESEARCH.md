# Research — RcloneBrowserNG

## Executive Summary
RcloneBrowserNG is a native Qt 6 desktop companion for rclone: it browses remotes, runs transfers, stores reusable tasks, mounts/streams files, schedules saved jobs through OS schedulers, and ships release artifacts for Windows, macOS, and Linux. Its strongest current shape is not competing as another web dashboard; it is a lightweight, local, trustworthy desktop operator for current rclone. The highest-value direction is to harden trust and capability depth: prevent unsafe preview/dry-run regressions, make actions backend-aware, make schedules auditable across OSes, and make release artifacts easier to verify.

Top opportunities, in priority order:

1. [Verified] Upgrade Windows x64/release Qt from the vulnerable `6.7.*` line or fail CI when the packaged Qt baseline is affected by Qt SVG CVE-2026-6210.
2. [Verified] Add dry-run/preview non-mutation regression tests around `src/transfer_dialog.cpp`, saved tasks, and staged transfers; rclone-ui issue #218 shows this is a real competitor trust failure.
3. [Verified] Build a backend capability registry from `operations/fsinfo` / `rclone backend features` so trash, public links, server-side copy/move, quota, hashes, and restore/version actions are only exposed when supported.
4. [Verified] Add scheduler catch-up parity and scheduler-definition tests; Linux timers already use `Persistent=true`, but Windows `schtasks.exe` creation does not set `StartWhenAvailable`.
5. [Verified] Expand `src/job_history.cpp` from summary rows to an exportable per-file job ledger for audit, troubleshooting, and proof of backup.
6. [Verified] Add release SBOM artifacts alongside existing checksums and GitHub artifact attestations.
7. [Verified] Turn accessibility from ad hoc names/tooltips into an acceptance pass covering tab order, descriptions, high-contrast behavior, and dialog keyboard paths.
8. [Likely] Add a remote health/repair view that consolidates rclone version, expired-token failures, mount dependencies, quota, unsupported backend features, and reconnect actions.
9. [Likely] Add safe preview and richer search refinements only after trust/reliability work; competitors make these table-stakes, but they are not root-cause safety work.

## Product Map
- Core workflows: configure/open remotes; browse/filter/sort remote files; upload/download/copy/move/delete/archive/dedupe/check; run and monitor transfers; save/dry-run/run/schedule reusable tasks; mount/stream/open files; export lists and diagnostics.
- User personas: rclone power users who want a native GUI; desktop admins managing cross-cloud moves; cautious users avoiding destructive CLI mistakes; Windows/macOS/Linux users who value local native apps over Electron/Tauri/web dashboards.
- Platforms and distribution: Windows x64/ARM64 CI artifacts, Windows x64 installer, macOS arm64/x86_64 DMG/app zip, Linux x86_64/aarch64 AppImage, source builds via CMake and Qt 6.
- Key integrations and data flows: rclone CLI and local `rcd`; OS schedulers (`schtasks.exe`, systemd user timers, launchd plists); WinFsp/macFUSE/FUSE for mounts; GitHub Releases, checksums, and artifact attestations; rclone config/password handling; local JSON stores for tasks and history.
- Hard constraints: MIT project license; Qt 6-only CMake; CMake minimum 3.16; macOS bundle target 11.0; current release version `2.0.0`; `ROADMAP.md` is open-work-only; packaging/signing work that requires external stores, credentials, or screenshots belongs outside this pass.

## Competitive Landscape
- Rclone UI: strong packaging breadth, remote-host control, cron scheduling, and server/homelab positioning. Learn from its distribution and cron UX; avoid its paywall pressure and treat issue #218 as a warning that preview/dry-run safety must be testable.
- RClone Manager: strongest fast-moving OSS competitor with Tauri/Angular, headless mode, file viewer, i18n/Crowdin, package-manager reach, job watcher, serve/mount controls, and current release activity. Learn from remote health, preview, translation, and update UX; avoid importing a heavy web-app architecture or node workflow builder into a native Qt app.
- H4R1B0/rclone-gui: macOS-native competitor with dual-pane browsing, bookmarks, version history, bulk rename, broad search filters, app lock, URL schemes, and a large test count. Learn from native-platform depth and regression coverage; avoid its macOS-only floor and AI/file-organization roadmap.
- Official `rclone/rclone-web` and `yet-another-rclone-dashboard`: prove that browser/PWA/headless/mobile needs are active and increasingly served by the rclone ecosystem. Learn from capability-aware public-link behavior, RC profile management, Playwright/accessibility test expectations, and mobile keyboard shortcuts; avoid duplicating the web UI lane already covered by rclone itself.
- REM and RcloneView: show commercial/Electron-style demand for inline preview, multi-window workspaces, visual sync history, notifications, batch jobs, and detailed error/history panels. Learn from auditability and preview; avoid bundled terminal scope creep and commercial gating.
- Cyberduck, Mountain Duck, WinSCP, and FreeFileSync: adjacent tools show durable patterns for retained transfer queues, resume/overwrite choices, background queues, offline/smart sync, and Windows VSS for locked files. Learn from queue persistence and recovery options; avoid full Dropbox-style offline sync until the rclone-native safety layer is stronger.

## Security, Privacy, and Reliability
- [Verified] `.github/workflows/build.yml` and `.github/workflows/release.yml` still install Qt `6.7.*` for Windows x64, while Qt documents CVE-2026-6210 as affecting Qt 6.7.0 before 6.8.8 and 6.9.0 before 6.11.1.
- [Verified] `src/main_window.cpp` now warns when the detected rclone is older than 1.74.3 for RC CVEs; keep this guard and test it because Windows mount flows rely on authenticated RC.
- [Verified] `src/remote_widget.cpp` still hard-codes Google Drive trash via `--drive-trashed-only` / `--drive-use-trash`; general backend feature discovery is missing.
- [Verified] `src/schedule_manager.cpp` has Linux `Persistent=true`, macOS launchd plists, and Windows `schtasks.exe`, but no golden tests and no Windows missed-run catch-up flag.
- [Verified] `src/job_history.cpp` persists only summary fields; it cannot prove which files were transferred, skipped, deleted, retried, or failed.
- [Verified] There are many `QMessageBox` call sites and synchronous `waitForFinished()` paths; these are not all defects, but they are the rough edge behind modal error interruption and UI-stall risks during long-running workflows.
- Missing guardrails: dry-run contract tests; scheduler-output tests; backend feature gates; release SBOM; static accessibility checks for tab order/names/descriptions; non-modal background job error review beyond the existing error log.
- Recovery needs: reinstall/repair scheduled tasks from a known-good definition; export job evidence with redaction; surface per-backend unsupported states before actions; retain failed job detail enough to retry or explain safely.

## Architecture Assessment
- `src/main_window.cpp` and `src/remote_widget.cpp` are large workflow controllers; new work should extract services rather than add more branching inside them.
- Add `BackendCapabilityService` around `operations/fsinfo` and `rclone backend features`; consumers should include `src/remote_widget.cpp`, `src/main_window.cpp`, and future trash/version/public-link UI.
- Add a scheduler definition layer with testable XML/unit/plist output before expanding cron, missed-run, or condition-gated schedules; current logic lives directly in `src/schedule_manager.cpp`.
- Add a dry-run/transfer command contract harness for `src/transfer_dialog.cpp`, `src/job_options.cpp`, and `src/main_window.cpp` task runners; current tests cover stores/parsers but not the destructive workflow contract.
- Add `JobEventStore` as an append-only JSONL or compact JSON detail file linked from `src/job_history.cpp`; keep existing summary UI, but make detail inspection/export possible.
- Add a release-trust step in `.github/workflows/release.yml` for SBOM generation and validation, building on the existing checksums and artifact attestations.
- Accessibility is partially addressed through `UiPolish`, accessible names, and `.ui` tab stops, but there is no acceptance standard for descriptions, keyboard-only dialog completion, high-contrast behavior, or screen-reader labels.
- Documentation gap: `RESEARCH.md` and `ROADMAP.md` were stale against recent commits; future research passes should reconcile `CHANGELOG.md`, live workflows, and last 200 commits before accepting old roadmap claims.

## Rejected Ideas
- Visual node/DAG workflow builder from RClone Manager issue #232: too heavy for this native Qt app; keep one-off transfers and saved tasks simple.
- New web UI, Docker-first UI, or mobile/PWA companion: already present as existing P5/open ideas and now covered by official `rclone/rclone-web`, Rclone UI, and yet-another-rclone-dashboard.
- AI-powered file organization/search from H4R1B0/rclone-gui roadmap: unclear trust value, high maintenance risk, and poor fit for a safety-focused file-transfer utility.
- Plugin marketplace: no stable app extension boundary exists, rclone already supplies provider extensibility, and a plugin surface would increase security/support load.
- Multi-user/team collaboration: conflicts with the app's local rclone config and desktop-trust model; no authorization/audit model exists.
- Full offline smart-sync/Dropbox-style client: Mountain Duck proves the value, but it requires filesystem integration, conflict semantics, and local cache architecture beyond a browser/transfer GUI.
- Built-in terminal tab: RcloneView ships this, but it expands scope, increases credential exposure risk, and duplicates the user's system shell.
- Flatpak/Snap/Homebrew publishing, AppStream screenshots, and AppImage GPG signing: valuable distribution work, but currently blocked by external store/credential/screenshot constraints and should stay out of this deliverable's new roadmap additions.

## Sources
Competitors:
- https://github.com/rclone-ui/rclone-ui
- https://github.com/rclone-ui/rclone-ui/issues/218
- https://github.com/Zarestia-Dev/rclone-manager
- https://github.com/H4R1B0/rclone-gui
- https://github.com/liriliri/rem
- https://github.com/outlook84/yet-another-rclone-dashboard
- https://github.com/rclone/rclone-web
- https://github.com/retifrav/rclone-rc-web-gui
- https://rcloneview.com/support/release-notes/v1.3/
- https://rcloneview.com/support/blog/visualize-your-storage-track-file-changes-and-sync-history-in-rcloneview
- https://s3drive.app/features

Core platform:
- https://rclone.org/changelog/
- https://rclone.org/rc/
- https://rclone.org/commands/rclone_backend/
- https://rclone.org/s3/
- https://doc.qt.io/qt-6/qaccessibilityhints.html
- https://doc.qt.io/qt-6/sbom.html
- https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling
- https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products
- https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds
- https://docs.github.com/en/actions/reference/security/secure-use
- https://github.com/ossf/scorecard-action

Adjacent tools and community:
- https://docs.duck.sh/cyberduck/transfer/
- https://docs.duck.sh/mountainduck/sync/
- https://winscp.net/eng/docs/transfer_queue
- https://freefilesync.org/manual.php?topic=volume-shadow-copy
- https://creativeprojects.github.io/resticprofile/schedules/configuration/
- https://learn.microsoft.com/en-us/windows/win32/taskschd/tasksettings-startwhenavailable
- https://www.reddit.com/r/rclone/comments/z0jhmu/bestfavorite_gui_wrapper_for_backups_using_rclone/
- https://github.com/rclone/rclone/issues/9375

## Open Questions
None for the recommended roadmap. The remaining unknowns are implementation choices, not research blockers.
