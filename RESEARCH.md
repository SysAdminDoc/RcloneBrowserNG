# Research - RcloneBrowserNG

## Executive Summary
RcloneBrowserNG is a native Qt 6 desktop operator for rclone: it browses remotes, runs and schedules transfers, mounts and serves paths, previews and compares files, saves reusable tasks, and publishes Windows, macOS, and Linux release artifacts. Its strongest current shape is local trust: recent work added capability gates, safe previews, async metadata probes, release SBOM/provenance, pinned AppImage tools, Qt CVE floor checks, high-contrast polish, and per-file job evidence. The highest-value direction is to finish recovery and assurance: protect saved-task secrets, persist staged transfer intent, regression-test all app-started RC endpoints, remove remaining GUI-thread helper-process waits, add post-transfer verification, add config backup/restore guardrails, and turn i18n/accessibility/package metadata into automated release acceptance.

Top opportunities, in priority order:

1. [Verified] Protect saved-task secrets at rest; `src/job_options_store.cpp` still writes heartbeat URLs, webhook URLs, and shell hooks directly into JSON.
2. [Verified] Persist staged transfers; `src/main_window.cpp` stores staged work only in `QListWidget` item data, so restart or crash loses prepared batch intent.
3. [Verified] Add an RC security regression gate; rclone's 2026 RC CVEs make any future unauthenticated `rcd` startup or `--rc-no-auth` regression release-critical.
4. [Verified] Remove remaining GUI-thread waits from user-triggered helper operations; search cancellation, mount backend detection/unmount, and native scheduler calls still block on `QProcess::waitForFinished()`.
5. [Verified] Add an optional post-transfer verification workflow using `rclone check` / `cryptcheck`, with results attached to job evidence.
6. [Verified] Add rclone config backup/restore/migration guardrails; comparable tools treat config backup as a first-class safety workflow, while this app relies on manual file handling.
7. [Verified] Add Qt Linguist scaffolding; the app still has no translation pipeline while active competitors ship localized READMEs or multiple UI languages.
8. [Likely] Add automated keyboard/accessibility smoke tests for main dialogs; accessible names exist, but current tests only cover color contrast helper logic.
9. [Likely] Generate package-manager manifest artifacts from releases; active competitors are easier to install through WinGet, Homebrew, Chocolatey, Flatpak, AUR, and Scoop.
10. [Likely] Add validated operation option profiles per remote/operation after the trust work lands; this is a parity feature for expert users, not a root-cause reliability fix.

## Product Map
- Core workflows: select and manage rclone configs; browse/filter/preview/compare remotes; upload/download/copy/move/delete/archive/dedupe/check files; dry-run, save, enqueue, run, schedule, and audit transfer tasks; mount, serve, stream, and inspect remote paths.
- User personas: experienced rclone users who want a native GUI; backup operators who need dry-run evidence and recoverability; admins moving data between cloud providers; Windows/macOS/Linux users avoiding abandoned, paywalled, or heavy web/Electron wrappers.
- Platforms and distribution: CMake/Qt 6/C++17; Windows x64 and ARM64, macOS arm64 and x86_64, Linux x86_64 and aarch64 AppImage; GitHub Releases with checksums, SPDX SBOM, and provenance attestations.
- Key integrations and data flows: rclone CLI and authenticated local `rcd`; OS schedulers (`schtasks.exe`, systemd user timers, launchd, cron); WinFsp/macFUSE/FUSE; rclone config/password-command handling; JSON task/history stores; release packaging scripts and GitHub Actions.

## Competitive Landscape
- Rclone UI: strong package-manager reach, Docker/homelab host profiles, configurable command workflows, and explicit production RC-auth guidance. Learn from install reach, host profile clarity, and command-profile flexibility; avoid remote-server scope creep and unsafe `--rc-no-auth` defaults.
- RClone Manager: fast-moving Tauri/Angular GUI with Crowdin, localized READMEs, headless mode, ARM packages, auto-download of rclone, and broad package-manager support. Learn from onboarding, localization, and distribution breadth; avoid replacing the native Qt stack with a web shell.
- H4R1B0/rclone-gui: macOS-native app with dual panels, drag/drop, Quick Look, transfer history/restart, schedule logging/export, Keychain/app-lock patterns, and multi-language support. Learn from restartable transfer evidence and native secret storage; avoid macOS-only assumptions and AI/file-organization expansion.
- Official `rclone gui`, `rclone-web`, and yet-another-rclone-dashboard: cover the browser dashboard lane with metrics, remotes, mounts, serves, running/completed jobs, and multi-profile RC management. Learn from status summaries and RC security copy; avoid building a parallel web/PWA product.
- REM and RcloneView: show demand for polished file browsing, preview, multi-window workspaces, batch jobs, config backup/migration, schedule history, and commercial-grade transfer evidence. Learn from config safety and visible job history; avoid paywalled core transfer controls.
- Mountain Duck, GoodSync, WinSCP, and FreeFileSync: adjacent mature tools normalize durable queues, retained background work, post-copy verification, conflict/version recovery, offline/cache status, and clear queue controls. Learn from recovery affordances and verification language; defer full smart-sync/filesystem-cache semantics until the current staging and task stores are restart-safe.

## Security, Privacy, and Reliability
- [Verified] `src/job_options_store.cpp` writes `heartbeatUrl`, `webhookUrl`, `preCommand`, and `postCommand` directly to JSON; support redaction does not protect the task store itself.
- [Verified] `src/main_window.cpp` keeps staged transfers only in `mStagingList`; no atomic store, schema version, restart recovery, or migration path exists for queued intent.
- [Verified] Rclone RC advisories in 2026 (`options/set`, `operations/fsinfo`, and `rcd --rc-serve`) share the same dangerous precondition: reachable RC without global auth. RcloneBrowserNG already uses RC auth in current code, but no regression test proves future app-started RC paths stay authenticated.
- [Verified] Remaining helper-process waits include `src/cross_remote_search.cpp:255`, `src/mount_backend.cpp`, `src/mount_widget.cpp`, and `src/schedule_manager.cpp`; these can stall the UI during cancel, mount checks, unmount, schedule, unschedule, or status work.
- [Verified] Current releases generate SBOM and provenance, and the AppImage/Qt-floor P0s are now shipped; package-manager manifests remain ungenerated artifacts, so store installation paths lag active competitors.
- Missing guardrails: secret-at-rest migration tests, staged-queue restart tests, RC startup security assertions, no-GUI-thread-wait tests for user actions, post-transfer verification result capture, config backup/restore preflight, Qt Linguist extraction checks, and dialog accessibility smoke tests.
- Recovery needs: staged work should survive restart or fail visibly; config restore should first back up the existing config and validate target paths; verification jobs should attach structured results to job history rather than relying on users to rerun CLI commands manually.

## Architecture Assessment
- `src/main_window.cpp` and `src/remote_widget.cpp` remain large workflow controllers. New work should extract focused services for staged-transfer persistence, RC command construction/security assertions, and operation verification instead of adding more inline branches.
- Add a `TaskSecretStore` abstraction around saved-task sensitive values using OS credential storage where practical, with a clearly warned plaintext fallback and JSON references rather than raw tokens.
- Add a `StagedTransferStore` under the existing app data location, written with `QSaveFile`, schema versioning, and the same validation rules used before executing queued transfers.
- Add command-builder tests for app-started RC flows so `rclone rcd`/RC helper paths cannot accidentally drop `--rc-user`/`--rc-pass` or introduce `--rc-no-auth`.
- Move remaining user-triggered helper operations onto async workers or signal-driven helpers. Keep bounded destructor cleanup separate and documented so reliability tests do not require artificial process leaks.
- Add a verification step model to `JobOptions` and job history: `rclone check` for normal copy/sync paths, `cryptcheck` for crypt comparisons, and explicit skipped/unsupported states when hashes or endpoints make verification misleading.
- Add config backup/restore as a small service around rclone config paths: export current config, restore with automatic backup of the current file, validate user-selected paths, and surface clear warnings for encrypted configs.
- Add Qt Linguist through CMake (`Qt6::LinguistTools`, `qt_add_translations`, or equivalent), wrap user-visible strings, and seed a pseudo-locale or smoke translation so CI proves extraction/build/load works.
- Expand `tests/interface_polish_test.cpp` beyond palette assertions to instantiate main, transfer, preferences, schedule, mount/serve, and remote-action dialogs offscreen and assert accessible names, descriptions, focusability, and tab-order basics.
- Generate package-manager manifest artifacts after each release artifact exists and checksums are known; external store submission can remain operator-driven, but manifest generation should be reproducible and validated.

## Rejected Ideas
- New web UI, remote-host dashboard, or PWA/mobile companion: official `rclone gui`, `rclone-web`, Rclone UI, and yet-another-rclone-dashboard already cover that lane.
- Full Dropbox-style smart sync/offline filesystem: Mountain Duck proves value, but it requires a cache daemon, placeholder semantics, conflict resolution, and background reconciliation outside this app's current architecture.
- Plugin marketplace: rclone already owns provider extensibility, and an app plugin ABI would add a large trust and support surface without a current boundary.
- Multi-user/team collaboration server: the product is a local desktop operator around local rclone config, not an authorization/audit server.
- Built-in terminal tab: it increases credential exposure and duplicates the user's shell; safer command-copy and dry-run evidence fit the app better.
- AI-powered file organization/search: not grounded in this repo's safety-first transfer philosophy, and no current source evidence makes it higher value than recovery/security work.
- Store submission as a local roadmap item: Homebrew, WinGet, Chocolatey, and Flathub publication depends on external maintainer workflows; generated validated manifests are the actionable repo-owned step.
- Bundling a private rclone fork or replacing CLI calls with librclone: H4R1B0 shows native FFI can work on macOS, but this project benefits from staying a transparent wrapper over the user's installed rclone across all desktop platforms.

## Sources
Competitors and adjacent products:
- https://github.com/rclone-ui/rclone-ui
- https://forum.rclone.org/t/rclone-ui-v3-slim-but-mighty/52568
- https://github.com/Zarestia-Dev/rclone-manager
- https://forum.rclone.org/t/rclone-manager-a-new-cross-platform-user-frienly-gui/53051
- https://github.com/H4R1B0/rclone-gui
- https://github.com/liriliri/rem
- https://github.com/rclone/rclone-web
- https://github.com/outlook84/yet-another-rclone-dashboard
- https://rcloneview.com/support/blog/config-backup-restore-migrate-rcloneview
- https://rcloneview.com/support/blog/automate-your-backup-routine
- https://docs.duck.sh/mountainduck/sync/
- https://www.goodsync.com/features
- https://winscp.net/eng/docs/transfer_queue
- https://awesome-rclone.com/

Core platform, security, and standards:
- https://rclone.org/changelog/
- https://rclone.org/gui/
- https://rclone.org/rc/
- https://rclone.org/commands/rclone_check/
- https://github.com/rclone/rclone/security/advisories/GHSA-qw24-gh76-8rvv
- https://github.com/rclone/rclone/security/advisories/GHSA-25qr-6mpr-f7qx
- https://github.com/advisories/GHSA-jfwf-28xr-xw6q
- https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling
- https://doc.qt.io/qt-6/internationalization.html
- https://doc.qt.io/qt-6/qaccessible.html
- https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds
- https://scorecard.dev/
- https://raw.githubusercontent.com/Homebrew/homebrew-cask/master/Casks/k/kapitainsky-rclone-browser.rb
- https://learn.microsoft.com/en-us/windows/package-manager/package/manifest
- https://docs.chocolatey.org/en-us/create/functions/get-checksumvalid/

## Open Questions
None block prioritization. Implementation choices remain for the exact cross-platform secret-store backend and whether generated package-manager manifests are submitted manually or by future authenticated release automation.
