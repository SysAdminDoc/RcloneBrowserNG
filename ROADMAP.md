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

- [ ] P1 — Protect sensitive saved-task fields at rest
  Why: Saved-task JSON can expose webhook tokens, heartbeat URLs, and shell hook commands to anyone who can read the task store.
  Evidence: `src/job_options_store.cpp`, `src/job_options.h`, H4R1B0/rclone-gui Keychain app-lock pattern, rclone RC auth guidance.
  Touches: `src/job_options_store.cpp`, `src/job_options_store.h`, `src/job_options.h`, `src/transfer_dialog.cpp`, `src/utils.cpp`, `tests/job_options_store_test.cpp`.
  Acceptance: New and migrated tasks store sensitive URL tokens and hook secrets through an OS credential/vault abstraction or a clearly warned plaintext fallback; JSON exports and support bundles redact secrets; tests prove no representative token is written in clear text.
  Complexity: L

- [ ] P1 — Remove remaining GUI-thread process waits from user-triggered helper operations
  Why: Search cancellation, mount backend checks/unmount, and native scheduler operations still block on helper processes and can freeze the desktop shell.
  Evidence: `src/cross_remote_search.cpp:255`, `src/mount_backend.cpp`, `src/mount_widget.cpp`, `src/schedule_manager.cpp`, Mountain Duck mount UX, GoodSync scheduling UX.
  Touches: `src/cross_remote_search.*`, `src/mount_backend.*`, `src/mount_widget.*`, `src/schedule_manager.*`, tests for process-helper behavior.
  Acceptance: Cancel search, detect mount backend, unmount, schedule, unschedule, and schedule-status flows use async or worker-backed helpers with progress/error states; no user-triggered GUI path blocks on `waitForFinished()` except documented bounded teardown.
  Complexity: L

### P2

- [ ] P2 — Add automated keyboard/accessibility smoke tests
  Why: Accessible names and high-contrast styling exist, but no test proves core dialogs remain keyboard navigable or screen-reader describable as UI changes.
  Evidence: `src/interface_polish.cpp`, `tests/interface_polish_test.cpp`, Qt 6.10 accessibility/high-contrast notes.
  Touches: `tests/interface_polish_test.cpp`, `tests/`, `src/interface_polish.*`, core dialog constructors as needed.
  Acceptance: Headless/offscreen tests instantiate main, transfer, preferences, schedule, and remote-action dialogs; assert non-empty accessible names/descriptions for interactive controls, usable tab order/focus, visible focus styling hooks, and no zero-size critical controls.
  Complexity: M

- [ ] P2 — Generate validated package-manager manifests from release artifacts
  Why: Active competitors are easier to install through package managers, and the old upstream Homebrew cask was disabled for being unmaintained.
  Evidence: Rclone UI package-manager list, Homebrew disabled `kapitainsky-rclone-browser` cask, rclone forum/winget install requests.
  Touches: `.github/workflows/release.yml`, `scripts/`, `assets/`, release artifact naming/checksum logic.
  Acceptance: Release workflow emits non-submitted Winget, Homebrew Cask, Chocolatey, and Flatpak manifest artifacts with current version, URLs, and SHA256 values; generated manifests are syntax-validated where tooling is available.
  Complexity: M

- [ ] P2 — Add release-script smoke tests for auxiliary tooling
  Why: Small script defects can silently break packaging maintenance, including the Bash-array usage under a `/bin/sh` shebang in `scripts/prepare_icons.sh`.
  Evidence: `scripts/prepare_icons.sh`, `scripts/release_AppImage.sh`, `scripts/release_windows.cmd`.
  Touches: `scripts/prepare_icons.sh`, release scripts, `.github/workflows/build.yml` or a script-test workflow step.
  Acceptance: Shell scripts pass `bash -n` or compatible checks, Windows batch has a dry-run validation path, icon preparation uses a matching Bash shebang or POSIX syntax, and CI fails on script syntax drift.
  Complexity: S

- [ ] P2 — Add packaged-artifact launch smoke tests before release publish
  Why: Release jobs build, deploy, checksum, and attest artifacts, but package regressions can still ship if the packaged binary is never executed after `linuxdeploy`, `macdeployqt`, `windeployqt`, zip, or installer staging.
  Evidence: `.github/workflows/release.yml`, `scripts/release_AppImage.sh`, `scripts/release_macOS.sh`, `scripts/release_windows.cmd`, GitHub artifact attestation guidance.
  Touches: `.github/workflows/release.yml`, `scripts/`, app startup/version flag handling if a dedicated smoke flag is needed.
  Acceptance: Linux AppImage, macOS app/DMG staging, Windows x64 zip/installer staging, and Windows ARM64 zip each run a non-GUI packaged-binary smoke such as `--version` or an equivalent release-smoke flag on compatible runners; publish depends on the smoke result; failures identify missing runtime/deployment files.
  Complexity: M

## Research-Driven Additions (2026-06-20)

### P2

- [ ] P2 — Embed AppImage zsync update metadata for delta updates
  Why: Users must download the full AppImage (~30MB) for each release. The AppImage ecosystem supports zsync-based delta updates that transfer only changed blocks, but the release pipeline does not embed `UPDATE_INFORMATION`.
  Evidence: No `UPDATE_INFORMATION` in `.github/workflows/release.yml` or `scripts/release_AppImage.sh`; docs.appimage.org/packaging-guide/optional/updates.html; linuxdeploy supports `LDAI_UPDATE_INFORMATION` env var.
  Touches: `.github/workflows/release.yml` (linux-appimage job), `scripts/release_AppImage.sh`.
  Acceptance: Released AppImages contain embedded zsync update information pointing to the GitHub releases URL pattern; `readelf` or `appimagetool --appimage-updateinformation` on the built AppImage returns a valid gh-releases-zsync URL; AppImageUpdate clients can find and apply delta updates.
  Complexity: S

- [ ] P2 — Migrate test harness to QTest framework
  Why: All 13 test targets use a custom `require()` function that calls `std::exit(1)` on failure, providing no assertion line numbers, no expected-vs-actual comparison output, and no CTest XML reporting integration. QTest provides all of these plus `QSignalSpy` for async signal verification, `QTEST_MAIN` for proper Qt event loop setup, and `QT_QPA_PLATFORM=offscreen` for headless widget tests.
  Evidence: `tests/interface_polish_test.cpp`, `tests/dryrun_contract_test.cpp`, and all other test files define `require()` + `std::exit(1)`; no `QTest` or `QTEST_MAIN` usage anywhere in the repo.
  Touches: All 13 files in `tests/`, `CMakeLists.txt` (link `Qt6::Test`).
  Acceptance: All existing test assertions use `QVERIFY`/`QCOMPARE` with descriptive messages; test binaries report pass/fail with assertion locations; `ctest --output-on-failure` produces clear diagnostic output on failures; no behavioral change in what is tested.
  Complexity: M

### P3

- [ ] P3 — Update AppStream metainfo with release entries for each tagged version
  Why: The AppStream metainfo file contains only the initial v2.0.0 release entry. Flathub and Linux distribution app stores use `<releases>` data to show version history, update recency, and release notes. A stale single entry makes the app look unmaintained.
  Evidence: `assets/io.github.sysadmindoc.rclonebrowserng.metainfo.xml` has one `<release>` for v2.0.0; Flathub metainfo guidelines require current release data; `appstreamcli validate` in CI already checks this file.
  Touches: `assets/io.github.sysadmindoc.rclonebrowserng.metainfo.xml`, potentially `.github/workflows/release.yml` (auto-insert release entry on tag).
  Acceptance: Each tagged release has a corresponding `<release>` entry with version, date, and brief description; entries are ordered newest-first; `appstreamcli validate` passes; at least the 3 most recent releases are present.
  Complexity: S

## Research-Driven Additions

### P1

- [ ] P1 — Add a local release verification harness
  Why: After workflow removal, the repo lacks one local entry point that tells maintainers which release guarantees still run, which former CI guarantees are absent, and whether artifacts are safe to publish.
  Evidence: `5f722b9`, missing `.github/`, `scripts/release_windows.cmd`, `scripts/release_AppImage.sh`, `scripts/release_macOS.sh`, rclone-ui v3.6.0 release assets, RClone Manager v0.2.8 release assets, GitHub artifact attestation docs.
  Touches: `scripts/`, `CMakeLists.txt`, `src/CMakeLists.txt`, `README.md`, `SECURITY.md`, release artifact staging.
  Acceptance: One documented local command orchestrates the existing build/test/release-script checks that still apply without GitHub Actions, emits a pass/fail release readiness report, fails when a claimed guarantee is not actually performed, and explicitly lists any CI-only guarantees that must be restored or removed from public docs.
  Complexity: M

- [ ] P1 — Add version-2 staged-transfer schema for execution metadata
  Why: `staged.json` persists basic queued transfers but drops heartbeat URL, webhook URL, post-command, task label, and verify-after-transfer state even though the runtime queue can execute those fields.
  Evidence: `src/main_window.cpp:2707`, `src/main_window.h:76`, `src/main_window.h:77`, `src/main_window.h:78`, `src/main_window.h:79`, `src/main_window.h:127`, `src/main_window.cpp:3975`, WinSCP transfer queue, GoodSync recovery patterns.
  Touches: `src/main_window.cpp`, `src/main_window.h`, staged-transfer schema/tests.
  Acceptance: Staged transfers round-trip all execution-affecting fields across app restart; schema migration handles existing version-1 staged files; tests prove notifications, post hooks, task labels, backup retention, and verification settings survive restore without cleartext leakage beyond the chosen secret-storage policy.
  Complexity: M

### P2

- [ ] P2 — Add safe mount presets and stale-mount recovery probes
  Why: Mount users need clearer VFS cache defaults and recovery when a mounted process survives but the mount becomes unusable after sleep/wake or backend disruption.
  Evidence: `src/remote_widget.cpp:1213`, `src/preferences_dialog.cpp:63`, `src/main_window.cpp:4478`, `src/main_window.cpp:4555`, RClone Manager issue #225, RClone Manager issue #5, Mountain Duck sync/cache docs.
  Touches: `src/remote_widget.cpp`, `src/preferences_dialog.*`, `src/main_window.*`, `src/mount_widget.*`, mount option tests.
  Acceptance: Mount UI offers named presets that show the exact rclone flags, preserves expert custom flags, validates incompatible options before starting rclone, and periodically probes keep-mounted mount points after wake/interval so stale-but-running mounts notify the user and offer remount.
  Complexity: M
