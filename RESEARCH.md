# Research — RcloneBrowserNG

## Executive Summary
RcloneBrowserNG is a native Qt 6 desktop operator for rclone: it browses remotes, runs and schedules transfers, mounts and streams files, manages saved tasks, and publishes multi-platform artifacts. Its strongest current shape is local trust: it already avoids Electron/web-runtime weight, now has capability gates, dry-run tests, per-file job evidence, release SBOM/provenance, and current rclone security warnings. The highest-value direction is to finish the trust layer that remains: pin release-time AppImage tooling, make local release scripts enforce the same Qt floor as CI, protect sensitive saved-task fields, persist staged transfer intent, remove the last blocking UI probes, add Qt-native i18n, and turn accessibility into automated acceptance instead of ad hoc widget metadata.

Top opportunities, in priority order:

1. [Verified] Pin and verify `linuxdeploy` / plugin downloads in `.github/workflows/release.yml` and `scripts/release_AppImage.sh`; both still execute mutable `continuous` AppImages before provenance is generated.
2. [Verified] Bring `scripts/release_windows.cmd` to CI parity; it still defaults to `C:\Qt\6.7.3\msvc2019_64` even though CI rejects Qt ranges affected by CVE-2026-6210.
3. [Verified] Protect task-store secrets; `src/job_options_store.cpp` serializes heartbeat/webhook URLs and pre/post shell commands into readable JSON.
4. [Verified] Persist the staging queue; `src/main_window.cpp` stores staged transfers only in `QListWidget` item data, so restart loses prepared batch work.
5. [Verified] Move provider, properties, and edit-fingerprint probes off the GUI thread; multiple `waitForFinished()` paths still block the main event loop for up to 15-30 seconds.
6. [Verified] Add a Qt Linguist i18n scaffold; the current codebase has no `tr()`, `QTranslator`, `lupdate`, `.ts`, or `.qm` pipeline while active competitors ship multiple languages.
7. [Likely] Add automated keyboard/accessibility smoke tests for core dialogs; accessible names and high-contrast styling exist, but no test verifies tab order, names, descriptions, or screen-reader-visible states.
8. [Likely] Generate package-manager manifest artifacts from releases; Rclone UI has broad package-manager reach and the old upstream Homebrew cask was disabled for being unmaintained.

## Product Map
- Core workflows: create/select rclone configs; browse, filter, preview, and compare remotes; upload/download/copy/move/delete/archive/dedupe/check files; save, dry-run, schedule, and run reusable tasks; mount, serve, stream, and inspect remote paths; export diagnostics and job evidence.
- User personas: rclone users who prefer a native GUI; desktop admins moving data across cloud providers; cautious backup users who need dry-runs and evidence; Windows/macOS/Linux users avoiding abandoned or paywalled GUI wrappers.
- Platforms and distribution: CMake/Qt 6 C++17; Windows x64/ARM64, macOS arm64/x86_64, Linux x86_64/aarch64 AppImage; GitHub Releases with checksums, SPDX SBOM, and build provenance attestations.
- Key integrations and data flows: rclone CLI and authenticated local `rcd`; OS schedulers (`schtasks.exe`, systemd user timers, launchd); WinFsp/macFUSE/FUSE; rclone config/password-command handling; JSON task/history stores; GitHub Actions release pipeline.

## Competitive Landscape
- Rclone UI: excels at package-manager reach, remote `rcd` host profiles, Docker/homelab usage, and warnings to use RC auth in production. Learn from distribution breadth and host-profile UX; avoid paywall pressure and unsafe `--rc-no-auth` defaults.
- RClone Manager: fast-moving Tauri/Angular GUI with Crowdin/localized READMEs, Docker/headless paths, ARM support, and visual remote/task management. Learn from i18n and onboarding polish; avoid importing a heavy web stack into this native Qt app.
- H4R1B0/rclone-gui: macOS-native app with dual panels, transfer history/restart, schedule logging/export, app lock via Keychain, Touch ID, multi-language support, and broad file-management affordances. Learn from secret storage, restartable queue evidence, and native-platform depth; avoid macOS-only assumptions and AI/file-organization expansion.
- Official `rclone gui` / `rclone-web`: now covers the browser dashboard lane with remotes, mounts, serves, transfers, metrics, and security guidance. Learn from dashboard health summaries and RC security copy; avoid building a parallel web/PWA product.
- REM and RcloneView: demonstrate demand for polished cross-cloud browsing, multi-window workspaces, preview, batch jobs, and commercial-grade transfer history. Learn from preview/history expectations; avoid Electron-terminal scope creep and commercial gating.
- Cyberduck/Mountain Duck, WinSCP, and FreeFileSync: adjacent mature tools show durable patterns for transfer queues, retained background work, offline/cache state, locked-file handling, and clear queue control. Learn from queue durability and recovery affordances; defer full smart-sync/filesystem-cache semantics until the app has stronger staged-work persistence.

## Security, Privacy, and Reliability
- [Verified] `.github/workflows/release.yml` and `scripts/release_AppImage.sh` download `linuxdeploy`, `linuxdeploy-plugin-qt`, and `linuxdeploy-plugin-appimage` from `releases/download/continuous` without an expected hash before executing them.
- [Verified] `scripts/release_windows.cmd` still defaults to Qt 6.7.3 and lacks the qmake-version CVE gate already present in CI; Qt documents CVE-2026-6210 as affecting Qt 6.7.0 before 6.8.8 and 6.9.0 before 6.11.1.
- [Verified] `src/job_options_store.cpp` writes `heartbeatUrl`, `webhookUrl`, `preCommand`, and `postCommand` directly to JSON; support bundles redact logs, but task storage itself is not secret-aware.
- [Verified] `src/main_window.cpp` keeps staged transfers only in `mStagingList` item data; the queue has no atomic store, migration, or restart recovery path.
- [Verified] `src/main_window.cpp::loadRemoteProviders()` blocks up to 15 seconds; `src/remote_widget.cpp::remoteFingerprint()` blocks up to 30 seconds; file properties block up to 15 seconds.
- [Verified] `scripts/prepare_icons.sh` has a `/bin/sh` shebang but uses Bash arrays; it is a small release-maintenance defect to include with script hardening.
- Missing guardrails: AppImage tool pinning, local release script parity tests, secret-at-rest migration tests, staged-queue recovery tests, Qt Linguist extraction checks, and UI accessibility smoke tests.
- Recovery needs: restart should preserve staged transfer intent; failed or interrupted transfer evidence should stay restartable or clearly discarded; release scripts should fail closed before creating artifacts with untrusted or vulnerable toolchains.

## Architecture Assessment
- `src/main_window.cpp` and `src/remote_widget.cpp` remain large workflow controllers. New work should extract services for async provider loading, queued-transfer persistence, and operation preflight instead of adding more branches.
- Add a small `ReleaseToolchain`/script-test surface around local release scripts: one reusable Qt version validator and one pinned-tool downloader should serve CI and local scripts.
- Add a `TaskSecretStore` abstraction using existing credential helper patterns where available, with JSON references and explicit plaintext fallback warnings only if a platform vault is unavailable.
- Add a `StagedTransferStore` under the existing app data location, written with `QSaveFile`, schema versioning, and validation matching the recent task-store hardening style.
- Add async command helpers for short metadata probes so provider loading, properties, and edit conflict checks can show cancellable progress without blocking the GUI thread.
- Add Qt Linguist support through CMake (`Qt6::LinguistTools`/`qt_add_translations` or equivalent), wrap user-visible strings with `tr()` / `QCoreApplication::translate()`, and seed a pseudo-locale for CI validation before real translations.
- Expand `tests/interface_polish_test.cpp` into a UI smoke harness that instantiates core dialogs offscreen and asserts accessible names/descriptions, focusability, tab order, and high-contrast-safe palette roles.
- Add generated package manifest artifacts only after release-trust pinning lands; external store submission can stay operator-driven, but manifests and checksums should be reproducible.

## Rejected Ideas
- New web UI, remote-host dashboard, or mobile/PWA companion: official `rclone gui`, `rclone-web`, Rclone UI, and yet-another-rclone-dashboard already cover that lane.
- Full Dropbox-style offline smart sync: Mountain Duck proves user value, but it requires a filesystem cache, conflict model, and background sync daemon that would change the app's architecture.
- Plugin marketplace: rclone already supplies provider extensibility, and an app plugin ABI would add a large security/support surface without a current boundary.
- Multi-user/team collaboration: the app is built around a local desktop trust model and local rclone config, not an authorization/audit server.
- Built-in terminal tab: useful in some competitors, but it increases credential exposure and duplicates the user's shell.
- AI-powered file organization/search: present in some competitor roadmaps but not grounded in this app's safety-first transfer philosophy.
- Store submission as a roadmap item: Homebrew/Winget/Chocolatey/Flathub publication is valuable but depends on external maintainer workflows; generating validated manifests is the actionable local work.

## Sources
Competitors:
- https://github.com/rclone-ui/rclone-ui
- https://github.com/Zarestia-Dev/rclone-manager
- https://github.com/H4R1B0/rclone-gui
- https://github.com/liriliri/rem
- https://github.com/rclone/rclone-web
- https://github.com/outlook84/yet-another-rclone-dashboard
- https://github.com/rclone/rclone-webui-react
- https://rcloneview.com/
- https://s3drive.app/features

Core platform:
- https://rclone.org/changelog/
- https://rclone.org/gui/
- https://rclone.org/rc/
- https://rclone.org/commands/rclone_backend/
- https://rclone.org/bisync/
- https://rclone.org/commands/rclone_selfupdate/
- https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling
- https://www.qt.io/blog/qt-6.10-released
- https://doc.qt.io/qt-6/internationalization.html
- https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds
- https://github.com/ossf/scorecard/blob/main/docs/checks.md
- https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases

Adjacent tools and community:
- https://winscp.net/eng/docs/transfer_queue
- https://docs.duck.sh/mountainduck/sync/
- https://freefilesync.org/manual.php?topic=volume-shadow-copy
- https://forum.rclone.org/t/rclone-browser-seems-abondaned-any-alternative/6832
- https://news.ycombinator.com/item?id=39153732
- https://awesome-rclone.com/
- https://raw.githubusercontent.com/Homebrew/homebrew-cask/master/Casks/k/kapitainsky-rclone-browser.rb
- https://learn.microsoft.com/en-us/windows/win32/taskschd/tasksettings-startwhenavailable
- https://reuse.software/spec-3.3/

## Open Questions
None block prioritization. Implementation choices remain for cross-platform secret storage and package-manifest publication paths.
