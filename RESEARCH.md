# Research — RcloneBrowserNG

## Executive Summary
RcloneBrowserNG is a native Qt 6/C++17 desktop operator for rclone that browses remotes, runs and schedules transfers, mounts/serves paths, previews and compares files, saves reusable tasks, and publishes signed Windows/macOS/Linux release artifacts with SBOM and provenance attestations. Its strongest current shape is local trust and correctness: capability gates, safe previews, async metadata probes, authenticated RC endpoints, high-contrast polish, per-file job audit detail, and 13 test targets covering parsers, contracts, schedules, and backend features. The project has addressed every open upstream issue and unmerged PR from both abandoned parent repositories. The highest-value direction is to (1) finish the remaining security/trust/recovery stack (secrets at rest, shell hook trust, staged persistence, RC regression gates, GUI-thread wait removal), (2) close the distribution gap (zero package managers vs. competitors in 7+, with a hard Homebrew September 2026 deadline for code signing), and (3) harden CI and build quality (CodeQL build-mode, static analysis, binary hardening flags, QTest migration, AppImage delta updates).

Top 10 opportunities, in priority order:

1. **[Verified]** Protect saved-task secrets at rest — `src/job_options_store.cpp` writes webhook tokens, heartbeat URLs, and shell hooks directly to JSON.
2. **[Verified]** Trust-gate saved-task shell hooks — `src/main_window.cpp` executes `preCommand`/`postCommand` through the user's shell from task JSON.
3. **[Verified]** Persist staged transfers — `src/main_window.cpp` stores staged work only in `QListWidget` item data; restart loses it.
4. **[Verified]** RC security regression gate — rclone's 2026 RC CVEs require continuous proof that app-started RC paths stay authenticated.
5. **[Verified]** Remove GUI-thread waits — `schedule_manager.cpp` (17 calls), `mount_backend.cpp`, `mount_widget.cpp`, `cross_remote_search.cpp` block on `waitForFinished()`.
6. **[Verified]** Strengthen CodeQL to traced build — current `build-mode: none` misses most C++ vulnerabilities; unpinned `@v4` tag violates project SHA-pin policy.
7. **[Verified]** Add OpenSSF binary hardening flags — MSVC lacks `/guard:cf`, `/CETCOMPAT`; Linux lacks full RELRO (`-Wl,-z,relro,-z,now`); `_FORTIFY_SOURCE` at 2, could be 3.
8. **[Likely]** Add AppImage delta-update metadata — no zsync `UPDATE_INFORMATION` embedded; users must re-download full images.
9. **[Likely]** Add clang-tidy CI integration — no static analysis beyond CodeQL exists; bugprone/cppcoreguidelines checks would catch use-after-free and uninitialized patterns that CodeQL extractionless mode misses.
10. **[Likely]** Migrate tests to QTest framework — current `require()` + `std::exit(1)` harness provides no line numbers, no comparison output, and no integration with CTest reporting.

## Product Map
- Core workflows: manage rclone configs; browse/filter/preview/compare/search remotes; upload/download/copy/move/delete/archive/dedupe/check/serve files; dry-run, save, enqueue, schedule, and audit transfer tasks; mount and stream remote paths.
- User personas: experienced rclone CLI users wanting a native GUI; backup operators needing dry-run evidence and recoverability; admins moving data between cloud providers; desktop users avoiding abandoned, paywalled, or heavy web/Electron wrappers.
- Platforms: Windows x64 + ARM64, macOS arm64 + x86_64, Linux x86_64 + aarch64 AppImage; Qt 6.4+ (CI builds at 6.8+); GitHub Releases with SHA256, SPDX SBOM, and provenance attestations.
- Key integrations: rclone CLI and authenticated local `rcd`; OS schedulers (schtasks, systemd timers, launchd, cron); WinFsp/macFUSE/FUSE/nfsmount; rclone config/password-command; JSON task/history stores; GitHub Actions CI/CD.

## Competitive Landscape
- **Rclone UI** (2.1k stars, TypeScript/Tauri, v3.6.0): Strongest distribution (WinGet, Homebrew, Chocolatey, Flathub, Scoop, AUR, npm). Remote server management via rcd profiles. Caution: #218 dry-run may execute real sync. Learn: distribution breadth, host profile clarity, command-profile flexibility. Avoid: web-shell runtime overhead, remote-server scope creep.
- **RClone Manager** (933 stars, Angular/Tauri, v0.2.7): Fastest-growing. FS watchers, alert actions (toast/webhook/email/Telegram/script), Docker headless, Crowdin i18n, performance presets, ARM packages, auto-downloads rclone when missing. Learn: onboarding, localization, distribution. Avoid: replacing native Qt with web shell.
- **H4R1B0/rclone-gui** (low visibility, Swift, v1.6.0, 521 tests): macOS-native with dual panels, Quick Look, cloud trash (10 providers), Keychain app-lock, transfer history/restart, bulk rename. Learn: restartable transfer evidence, native secret storage. Avoid: macOS-only assumptions.
- **Official rclone gui/web** (v1.74.0 embedded `rclone gui` command): Covers browser dashboard lane with remote browsing, jobs, mounts, serves. No scheduling, no saved tasks, no verification, no packaging. Learn: RC endpoint patterns, status summaries. Avoid: building parallel web UI.
- **Mountain Duck / GoodSync / WinSCP / FreeFileSync**: Adjacent mature tools normalizing durable queues, retained background work, post-copy verification (GoodSync Analyze + MD5 check), conflict/version recovery, offline/cache status, and clear queue controls. Learn: recovery affordances, verification language, analyze-before-commit UX. Defer: full smart-sync/filesystem-cache semantics.

## Security, Privacy, and Reliability
- **[Verified]** `src/job_options_store.cpp` writes `heartbeatUrl`, `webhookUrl`, `preCommand`, `postCommand` directly to JSON; support redaction does not protect the task store.
- **[Verified]** `src/main_window.cpp` executes saved `preCommand`/`postCommand` through `cmd.exe /c` or `/bin/sh -c`. No trust state for imported/migrated/edited tasks.
- **[Verified]** `src/main_window.cpp` keeps staged transfers only in `mStagingList` (QListWidget); no persistence, schema, or recovery path.
- **[Verified]** Rclone RC advisories (GHSA-qw24-gh76-8rvv, GHSA-25qr-6mpr-f7qx, GHSA-jfwf-28xr-xw6q) share the precondition of reachable unauthenticated RC. Current code uses auth, but no regression test proves it.
- **[Verified]** `waitForFinished()` in `schedule_manager.cpp` (17 calls), `mount_backend.cpp`, `mount_widget.cpp:173-181`, `cross_remote_search.cpp:255` block the GUI thread during user-initiated operations.
- **[Verified]** CodeQL `build-mode: none` performs extractionless analysis inadequate for C++ (misses interprocedural flows, taint tracking). Unpinned `@v4` tag violates SHA-pin security policy.
- **[Verified]** Build hardening gaps: MSVC linker missing `/guard:cf` (Control Flow Guard), `/CETCOMPAT` (Intel CET Shadow Stack), `/DYNAMICBASE` (explicit ASLR). GCC/Clang missing `-Wl,-z,relro,-z,now` (full RELRO). `_FORTIFY_SOURCE=2` below recommended level 3 per OpenSSF Compiler Hardening Guide.
- **[Verified]** AppImages lack embedded `UPDATE_INFORMATION` for zsync delta updates; users must download full AppImage for each release.
- **[Likely]** No static analysis beyond CodeQL: no clang-tidy, cppcheck, or PVS-Studio integration. The custom test harness (`require()` + `std::exit(1)`) provides no assertion diagnostics.

## Architecture Assessment
- `src/main_window.cpp` (4517 lines) and `src/remote_widget.cpp` (2622 lines) are large workflow controllers. New work should extract focused services (staged-transfer store, shell-hook trust, RC command security assertions, config backup, restartable job snapshots, verification results) rather than adding more inline branches.
- Tests (13 targets, 1450 lines) use a custom `require()` harness instead of QTest. Migration to QTest would add: assertion messages with line numbers, comparison output for failed values, `QSignalSpy` for async flows, CTest XML reporting integration, and offscreen widget instantiation via `QT_QPA_PLATFORM=offscreen`.
- Remote provider icons (`src/images/`) cover only 18 services. New rclone backends since v1.69 (iCloud Drive, iCloud Photos, Archive, Filen, Internxt, Huawei Drive, Shade, Drime, DOI, FileLu, Cloudinary) have no dedicated icons and fall through to `unknown.png`. The generated provider picker (`src/remote_provider.cpp`) dynamically lists backends but the icon system is static.
- `BackendFeatures::defaultForType()` in `src/rclone_capabilities.cpp` hardcodes known backend types. New backends fall through to conservative defaults, which is safe but means features like cloud trash, public links, and about/quota may be hidden for new providers until the async feature query completes.
- The `RcloneRcEngine` (`src/rclone_rc_engine.cpp`, 300 lines) is well-structured with async `postAsync` and bounded `postSync` (used only for startup ping). The rcd engine is ready for `job/batch` (rclone v1.72) and `core/disks` (v1.74) integration.

## Rejected Ideas
- **Web UI / PWA / mobile companion**: official `rclone gui` (v1.74), rclone-web, Rclone UI, and yet-another-rclone-dashboard cover that lane. Source: rclone.org/gui, rclone forum.
- **Full smart-sync / offline filesystem**: Mountain Duck proves value but requires cache daemon, placeholder semantics, conflict resolution, and background reconciliation outside the current architecture. Source: docs.duck.sh/mountainduck/sync.
- **Plugin marketplace**: rclone already owns provider extensibility; an app plugin ABI adds trust and support surface without clear boundaries. Source: rclone provider model.
- **Multi-user / team collaboration server**: the product is a local desktop operator. Source: product philosophy.
- **Built-in terminal tab**: increases credential exposure and duplicates the user's shell; command-copy and dry-run evidence are safer. Source: SECURITY.md scope.
- **AI-powered file organization**: H4R1B0 lists it as a future vision item; not grounded in safety-first transfer philosophy. Source: github.com/H4R1B0/rclone-gui.
- **Bundling private rclone fork / librclone FFI**: the project benefits from staying a transparent wrapper over the user's installed rclone. Source: project philosophy, H4R1B0 FFI experience.
- **Store submission as local roadmap item**: publication depends on external maintainer workflows; generated validated manifests are the repo-owned step. Source: packaging research.
- **Keyboard shortcuts for remote browser actions**: intentionally removed per project design rules (actions exposed through visible buttons, menus, context menus). Source: CLAUDE.md, CHANGELOG.md.

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
- https://www.goodsync.com/features
- https://winscp.net/eng/docs/transfer_queue
- https://awesome-rclone.com/
- https://docs.duck.sh/mountainduck/sync/
- https://duplicacy.com/

Core platform, security, and standards:
- https://rclone.org/changelog/
- https://rclone.org/gui/
- https://rclone.org/rc/
- https://rclone.org/commands/rclone_check/
- https://github.com/rclone/rclone/security/advisories/GHSA-qw24-gh76-8rvv
- https://github.com/rclone/rclone/security/advisories/GHSA-25qr-6mpr-f7qx
- https://github.com/advisories/GHSA-jfwf-28xr-xw6q
- https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling
- https://www.qt.io/blog/qt-6.8-released
- https://doc.qt.io/qt-6/internationalization.html
- https://doc.qt.io/qt-6/qaccessible.html
- https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html

Packaging and distribution:
- https://workbrew.com/blog/homebrew-5-0-0
- https://github.com/orgs/Homebrew/discussions/6482
- https://learn.microsoft.com/en-us/windows/package-manager/package/manifest
- https://github.com/michidk/winget-updater
- https://docs.chocolatey.org/en-us/community-repository/moderation/
- https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines
- https://docs.appimage.org/packaging-guide/optional/updates.html
- https://signpath.org/
- https://repology.org/project/rclone-browser/information
- https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations

Community signal:
- https://github.com/kapitainsky/RcloneBrowser/issues (138 open, 2.9k stars, 252 forks)
- https://github.com/mmozeiko/RcloneBrowser/issues (30 open, original author)
- https://github.com/rclone/rclone/issues/9375
- https://forum.rclone.org/t/a-new-rclone-web-gui-built-for-the-latest-rc-api/53596

## Open Questions
None block prioritization. Implementation choices remain for: the exact cross-platform secret-store backend (`CredRead`/`CredWrite` on Windows, Keychain on macOS, libsecret on Linux); whether optional rclone acquisition performs a verified app-managed download or only opens official install guidance; whether generated package-manager manifests are submitted manually or by future authenticated release automation; and whether the Flathub AI policy's "mature, well-maintained projects" exception applies to this fork.
