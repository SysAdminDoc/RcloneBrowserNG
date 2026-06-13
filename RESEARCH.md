# Research — RcloneBrowserNG

## Executive Summary

RcloneBrowserNG is a lightweight native Qt Widgets desktop GUI for an installed rclone binary: it browses remotes, runs transfers, mounts filesystems, streams media, and stores reusable jobs without becoming a web UI or bundled sync daemon. The 2.0.0 release and subsequent unreleased work brought the app to a strong position: `lsjson`/`--use-json-log` parsers, authenticated mount rc endpoints, async update checks, AppStream metadata, CodeQL, SLSA attestations, and a comprehensive release workflow. The highest-value direction now is (1) closing the **September 2026 Homebrew deadline** for signed/notarized macOS binaries, (2) expanding platform coverage to macOS Intel and Linux ARM64 release artifacts, (3) gating UI on rclone/driver capability, and (4) adding the scheduling and job history features that represent the strongest unmet user demand across the ecosystem.

Top 10 opportunities in priority order:
1. Code signing + Apple notarization before the September 2026 Homebrew cask cutoff
2. macOS x86_64 and Linux ARM64 release artifacts (currently ARM64-only macOS, x86_64-only Linux)
3. Bump minimum safe rclone version gate to v1.74.3 (CVE-2026-49980)
4. Paged directory listing (`lsjson` ListP) for large remotes
5. Built-in task scheduler — the #1 most-requested feature ecosystem-wide
6. Persistent job run history with tray status states
7. Searchable/filterable remotes selector
8. `core/version`/`core/disks` enrichment for diagnostics and mount UX
9. macOS Intel + universal build coverage
10. Retire stale roadmap items already implemented (drag-and-drop, public links)

## Product Map

- Core workflows: configure rclone path/config/password; browse remote tabs with lazy-cached tree; upload/download/copy/move/rename/delete; export listings via lsjson; run copy/sync/move jobs with json-log progress; mount/unmount with auto-remount; stream with external player; save and export reusable tasks.
- User personas: rclone desktop users who prefer native UI over terminal; Windows/macOS/Linux homelab operators; sysadmins managing many remotes; privacy/crypt users; operators who need visible logs and recoverable transfers.
- Platforms and distribution: C++17, CMake, Qt 5.15/Qt 6 Widgets + Network, external rclone runtime. CI builds all four platform legs (Linux Qt5/Qt6, macOS ARM64, Windows x64). Release workflow produces AppImage, DMG, Windows zip+installer with SHA256 checksums and SLSA attestations.
- Key integrations: QProcess-spawned rclone (transitioning to RcloneRcEngine rcd), QSettings preferences, tasks.bin QDataStream persistence, rclone config file with optional password-command credential helper, WinFsp/macFUSE/fuse-t/nfsmount mount backends, GitHub update checks.

## Competitive Landscape

### rclone-ui (Tauri, ~2,065 stars, active)
What it does well: cron scheduling (v1.1.0), dual-pane Commander file manager (v3.4.0), editable cron expressions with run previews (v3.4.0), bisync alpha (v2.8.0), auto-mount on startup (v0.9.5), searchable remotes, retry failed transfers (v3.4.1), desktop notifications for scheduled tasks, distributed via WinGet/Homebrew/Flathub. What to learn from it: scheduling UX, dual-pane ergonomics, distribution breadth. What to avoid: its Tauri runtime dependency and open reliability issues around bisync and dry-run (#218).

### RcloneView (commercial, closed-source)
What it does well: free folder comparison, 1:N sync, mounting, transfer monitoring. Paywalls scheduling ($9.90/yr) and multi-daemon. Token-expired blog marketing shows this is a high-demand pain point. What to learn from it: comparison view and token management are valuable enough to paywall — free equivalents differentiate. What to avoid: closed-source dynamics, upsell patterns.

### RClone Manager (Angular+Tauri, active)
What it does well: v0.2.4 (April 2026) with generic env vars, Docker/headless improvements, Flathub presence, active issue triage. What to learn from it: watch-folder demand (#204), shell integration demand (#80), sleep-resilient remount. What to avoid: webview stack as a differentiator — native Qt is this project's niche.

### rclone official GUI (`rclone gui`, v1.74.0)
What it does well: ships embedded with rclone, rc-first contract, zero install. What to learn from it: rc API patterns. What to avoid: competing as another browser shell — this app adds desktop-native UX (tray, mounts, saved tasks, file manager integration) that a web UI can't.

### WinSCP / Cyberduck / Mountain Duck / FreeFileSync
What they do well: edit-in-place with auto re-upload (Cyberduck/WinSCP), bookmarks with labels/colors, keep-remote-up-to-date watch folder (WinSCP), folder comparison with visual diff (FreeFileSync), VSS shadow copy for locked files (FreeFileSync), storage-independent file versioning (Mountain Duck 5), batch jobs with RealTimeSync. What to learn from it: specific workflow UX patterns that pair naturally with rclone capabilities. What to avoid: scope-creeping into full commercial mount-client territory before core reliability and distribution are credible.

### Backrest / Vorta (backup tool UX, adjacent)
What they do well: run history with per-run stats, notification webhooks (Discord/Slack/Gotify/Shoutrrr/Telegram), pre/post shell hooks, condition-gated schedules (AC power, unmetered network), diagnostics/support bundle. What to learn from it: operational surface patterns that fit a resident tray app. What to avoid: server-oriented complexity.

## Security, Privacy, and Reliability

- [Verified] **Minimum safe rclone is now v1.74.3** (up from v1.73.5). CVE-2026-49980 (CVSS 9.8) allows inline remotes to bypass `global.*` config restrictions for command execution. The existing startup version warning should be bumped to v1.74.3. `src/main_window.cpp` version-check logic.
- [Verified] **WinFsp CVE-2026-3006 fix only in beta.** WinFsp v2.2B1 (build 2.2.26112, April 22 2026) is the only release with the fix. The current stable (v2.1) remains vulnerable. The app's existing WinFsp version warning is correct to flag v2.1; users should be directed to v2.2B1 until a stable ships.
- [Verified] **macFUSE 5.2.0** (April 2026) ships FSKit backend support via `-o backend=fskit`, enabling fully userspace file systems on macOS 26 without a kernel extension. Limitation: mount points restricted to `/Volumes`; notification API unsupported. fuse-t also now supports FSKit. The existing macOS mount detection code should recognize FSKit-capable builds.
- [Verified] **Homebrew will remove unsigned casks September 1, 2026.** After that date, `brew install --cask` will not work for apps without code signing and Apple notarization. This makes the existing P2 code-signing roadmap item **critical timeline** — miss it and the Homebrew distribution channel is lost. SignPath Foundation provides free OV code signing for OSS (private key on HSM, CI-integrated). Apple notarization requires a $99/yr developer account.
- [Verified] `src/utils.cpp:294-299` still places `RCLONE_CONFIG_PASS` in every child rclone environment. The password-command credential helper is the correct fix; until then diagnostics must redact this environment and child command output.
- [Verified] `src/job_widget.cpp:282-285`, `src/mount_widget.cpp:141-145`, and `src/stream_widget.cpp:87-95` still contain blocking terminate/kill waits, including unbounded final waits.
- [Verified] No test coverage for the two historically-broken surfaces: output parsing and tasks.bin round-trip. Six tests exist (export_list_writer, vfs_upload_state, remote_path, remote_provider, job_options_store, mount_backend) but none cover job_widget JSON log parsing or item_model lsjson parsing.
- [Verified] No Qt translation pipeline (`tr()`, `QTranslator`, `.ts`). Sparse accessibility metadata (some `setAccessibleName` calls exist in recent polish work, but no systematic coverage of `.ui` files or buddies).
- [Likely] rclone config mutation during active jobs or mounts can lose concurrent config edits. Existing config-mutation guard (`confirmConfigMutation`) mitigates but does not block all paths.

## Architecture Assessment

- **Release artifact coverage gap**: `release.yml` builds macOS ARM64 only (macos-14 runner). No x86_64/universal macOS build. Linux is x86_64 only; no ARM64 AppImage. This limits the install base on Intel Macs and ARM Linux (Raspberry Pi, cloud ARM instances).
- **Large directory performance**: `item_model.cpp` loads entire lsjson output in one go. rclone v1.72 added paged listing (ListP) across 12 backends — streaming pages would avoid OOM on million-file directories and enable progressive display.
- **Duplicate utility code**: `getNiceSize()` is identically implemented in both `item_model.cpp:19-31` and `job_widget.cpp:8-20`. Should be consolidated into `utils.h`.
- **Stale roadmap items**: The ROADMAP lists "Drag & drop to upload" and "Public link generation (rclone link)" as open, but both are already implemented (multi-file drag-drop in CHANGELOG [Unreleased]; public link button exists in remote_widget.cpp since v1.4).
- The main boundary issue remains the one-shot `QProcess` model scattered across files. The `RcloneRcEngine` is the correct refactor path and would unlock: `job/batch` for multi-command operations, `core/stats` short mode for lower-overhead polling, `core/disks` for mount destination enumeration, and richer diagnostics via `core/version` (now includes osVersion/osArch/osKernel in v1.74).
- `main_window.cpp` remains a large coordinator. Extractable seams: `UpdateChecker`, `MountManager`, `DiagnosticsCollector`, `ScheduleEngine`.
- Release scripts (`scripts/release_windows.cmd`, `scripts/release_AppImage.sh`) still target Qt 5.13.2, VS2019 32-bit, and CentOS 7. These are now fully superseded by `release.yml` CI but remain in-tree as misleading guidance.
- Testing gap priority: (1) job_widget JSON log parsing golden fixtures, (2) item_model lsjson parsing with edge-case filenames, (3) rclone command builder argument construction, (4) metadata validation CI (AppStream, desktop, installer identity).

## Rejected Ideas

- **Electron/Tauri rewrite** — rclone-ui and RClone Manager already occupy that lane; native Qt is this project's differentiator. Source: rclone-ui architecture.
- **Primary web/NAS/Docker UI** — official `rclone gui` (v1.74.0) now ships embedded; competing as another browser shell has no value. Source: rclone.org/commands/rclone_gui.
- **Mobile client** — Round Sync is the active Android reference; desktop work has higher fit. Source: github.com/newhinton/Round-Sync.
- **Bundling rclone by default** — optional managed download useful but default bundling increases update/security burden. Source: RClone Manager's managed-download approach.
- **Paid tier / telemetry upsells** — commercial competitors prove value but this repo's position is free, local, native, privacy-preserving. Source: RcloneView pricing model.
- **Hand-coded backend config forms** — `config/providers` is the maintainable source; hardcoding ages out. Source: existing remote_provider.cpp implementation.
- **Full plugin ecosystem now** — no stable engine/API boundary yet; revisit after RcloneRcEngine and task JSON migration. Source: architecture assessment.
- **Icon overlays on mounted drives** — requires shell extension (Windows) or Finder Sync Extension (macOS), both complex kernel-adjacent work with high maintenance burden for marginal benefit over tray notifications. Source: rclone#7923, WinFsp limitations.
- **Monthly bandwidth quota tracking** — requires persistent state across sessions for something rclone doesn't natively support; complexity outweighs demand. Source: forum.rclone.org/t/feature-new-option-to-limit-monthly-bandwidth-quota.
- **In-app rich text editor** — OS edit-in-place with re-upload is lower maintenance and matches Cyberduck/WinSCP patterns. Source: docs.cyberduck.io/cyberduck/edit.

## Sources

Rclone and platform:
- https://rclone.org/changelog/
- https://rclone.org/commands/rclone_gui/
- https://rclone.org/rc/
- https://rclone.org/commands/rclone_test_speed/
- https://rclone.org/bisync/
- https://github.com/rclone/rclone/releases/tag/v1.74.3
- https://forum.rclone.org/t/rclone-release-v1-73-5-important-security-fix/53700
- https://forum.rclone.org/t/rclone-release-v1-74-0/53734
- https://ccb.belgium.be/advisories/warning-two-critical-unauthenticated-code-execution-vulnerabilities-rclone-patch
- https://doc.qt.io/qt-6.10/supported-platforms.html
- https://www.qt.io/blog/qt-6.10-released

Competitors and community:
- https://rcloneui.com/changelog
- https://rcloneview.com/
- https://github.com/Zarestia-Dev/rclone-manager
- https://github.com/garethgeorge/backrest
- https://github.com/awesome-rclone/awesome-rclone
- https://forum.rclone.org/t/a-new-rclone-web-gui-built-for-the-latest-rc-api/53596
- https://github.com/rclone/rclone/issues/7923

Security and supply chain:
- https://github.com/winfsp/winfsp/releases/tag/v2.2B1
- https://dbugs.ptsecurity.com/vulnerability/PT-2026-34551
- https://macfuse.github.io/2026/04/09/macfuse-5.2.0.html
- https://www.fuse-t.org/
- https://signpath.io/solutions/open-source-community
- https://signpath.org/
- https://workbrew.com/blog/homebrew-5-0-0
- https://github.com/actions/attest-build-provenance
- https://reuse.software/spec-3.3/

Adjacent products and distribution:
- https://winscp.net/eng/docs/task_keep_up_to_date
- https://docs.cyberduck.io/cyberduck/edit/
- https://freefilesync.org/
- https://s3drive.app/features
- https://docs.weblate.org/en/latest/formats/qt.html
- https://google.github.io/clusterfuzzlite/

## Open Questions

- **Homebrew signing timeline**: Can SignPath Foundation approval + Apple developer enrollment complete before the September 1, 2026 cask deadline? Lead time for SignPath approval is "days to weeks"; Apple developer enrollment is typically 24-48 hours with payment.
- **macOS Intel user base**: Is the Intel Mac user base large enough to justify a universal or separate x86_64 build, given Apple Silicon transition? GitHub's macos-13 runners provide x86_64 builds.
- **Qt5 drop timing**: Should Qt5 support be dropped before the next release to reduce ifdef maintenance, or kept as a distro-build compatibility path for older Linux distributions?
- **tasks.bin migration window**: What compatibility window is required for existing tasks.bin files before JSON task migration becomes the default?
