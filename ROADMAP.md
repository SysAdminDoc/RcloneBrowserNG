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

- [ ] P1 — Persist staged transfers across restart
  Why: The staging queue is useful for batch review but currently disappears on crash or normal restart because it only lives in `QListWidget` item data.
  Evidence: `src/main_window.cpp`, `src/remote_widget.cpp`, WinSCP transfer queue, H4R1B0/rclone-gui transfer history/restart.
  Touches: `src/main_window.cpp`, `src/main_window.h`, `src/remote_widget.cpp`, new store/test code, `tests/`.
  Acceptance: Enqueued transfers are written atomically with schema/version data, restored on launch as pending staged items, validated before execution, removable by the user, and covered by a restart-style unit test.
  Complexity: M

- [ ] P1 — Add RC security regression gates for app-started RC endpoints
  Why: Rclone's 2026 RC advisories make any future unauthenticated app-started RC path a critical command-execution risk.
  Evidence: rclone GHSA-qw24-gh76-8rvv, GHSA-25qr-6mpr-f7qx, GHSA-jfwf-28xr-xw6q; `src/rclone_rc_engine.cpp`; `src/mount_widget.cpp`; `src/main_window.cpp`.
  Touches: `tests/`, `src/rclone_rc_engine.*`, `src/mount_widget.cpp`, `src/main_window.cpp`, any shared rclone command-builder code.
  Acceptance: Automated tests or a release check enumerate every app-created RC startup/helper command and fail if it lacks `--rc-user`/`--rc-pass`, includes `--rc-no-auth`, or weakens the existing vulnerable-rclone warning path.
  Complexity: M

- [ ] P1 — Trust-gate saved-task shell hooks before execution
  Why: Saved or imported tasks can execute `preCommand` and `postCommand` through the user's shell, so command execution needs an explicit trust boundary separate from normal task loading.
  Evidence: `src/main_window.cpp`, `src/transfer_dialog.cpp`, `src/job_options_store.cpp`, H4R1B0/rclone-gui Keychain/app-lock pattern, GoodSync post-job automation patterns.
  Touches: `src/job_options.*`, `src/job_options_store.*`, `src/transfer_dialog.*`, `src/main_window.cpp`, `src/job_history.*`, `tests/job_options_store_test.cpp`, `tests/`.
  Acceptance: Imported, migrated, or edited tasks with shell hooks are marked untrusted until reviewed; untrusted hooks do not execute; transfer UI warns that hooks are local code execution; job history/support output records redacted hook status and exit result; tests prove untrusted hook commands are blocked.
  Complexity: M

- [ ] P1 — Remove remaining GUI-thread process waits from user-triggered helper operations
  Why: Search cancellation, mount backend checks/unmount, and native scheduler operations still block on helper processes and can freeze the desktop shell.
  Evidence: `src/cross_remote_search.cpp:255`, `src/mount_backend.cpp`, `src/mount_widget.cpp`, `src/schedule_manager.cpp`, Mountain Duck mount UX, GoodSync scheduling UX.
  Touches: `src/cross_remote_search.*`, `src/mount_backend.*`, `src/mount_widget.*`, `src/schedule_manager.*`, tests for process-helper behavior.
  Acceptance: Cancel search, detect mount backend, unmount, schedule, unschedule, and schedule-status flows use async or worker-backed helpers with progress/error states; no user-triggered GUI path blocks on `waitForFinished()` except documented bounded teardown.
  Complexity: L

### P2

- [ ] P2 — Add Qt Linguist internationalization scaffolding
  Why: The app is English-only at source level while current competitors ship multiple languages and old community feedback asked how to translate Rclone Browser.
  Evidence: `rg "tr\(|QTranslator|lupdate"` scan, Qt internationalization docs, RClone Manager Crowdin/localized READMEs, H4R1B0/rclone-gui Korean/English support.
  Touches: `CMakeLists.txt`, `src/CMakeLists.txt`, `src/main.cpp`, `src/**/*.cpp`, `src/**/*.ui`, translation resources.
  Acceptance: User-visible strings are extractable with Qt Linguist tooling, the app loads compiled `.qm` files from a standard location, CI verifies extraction/build of at least one pseudo-locale, and untranslated strings fall back cleanly.
  Complexity: L

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

- [ ] P2 — Add optional post-transfer verification workflow
  Why: Backup users need a first-class integrity answer after copy/sync instead of manually running rclone CLI checks after the GUI job completes.
  Evidence: `rclone check` docs, GoodSync file-copy verification, `src/transfer_dialog.cpp`, `src/job_options.h`, `src/job_history.*`.
  Touches: `src/transfer_dialog.*`, `src/job_options.*`, `src/job_widget.*`, `src/job_history.*`, command construction/tests for copy, sync, and cryptcheck paths.
  Acceptance: Transfer and saved-task options can run a non-mutating verify step after successful copy/sync; the app chooses `check` or `cryptcheck` where appropriate, records pass/fail/skipped evidence in job history, and tests prove verification is not run for unsupported/destructive operations.
  Complexity: M

- [ ] P2 — Add rclone config backup and restore guardrails
  Why: Config loss or a bad restore can break every remote, and comparable rclone GUIs expose backup/migration as a dedicated safety workflow.
  Evidence: RcloneView config backup/restore/migration docs, RClone Manager config import/export, `src/preferences_dialog.cpp`, `src/utils.cpp`.
  Touches: `src/preferences_dialog.*`, `src/utils.*`, new config-backup helper/test code, README usage notes if the workflow changes setup.
  Acceptance: Users can export the active rclone config, restore from a selected file only after the current config is backed up, validate paths before writing, and receive clear warnings for encrypted/password-protected configs; tests cover backup naming and unsafe path rejection.
  Complexity: M

- [ ] P2 — Add first-run rclone acquisition and repair assistant
  Why: Users still have to resolve missing or broken rclone manually, while current competitors auto-download or guide install repair and community demand includes simpler GUI onboarding for cloud download workflows.
  Evidence: `README.md`, `src/main_window.cpp`, `src/preferences_dialog.cpp`, RClone Manager system requirements, rclone download/install/selfupdate docs, rclone/rclone#9375.
  Touches: `src/main_window.*`, `src/preferences_dialog.*`, `src/utils.*`, rclone capability/version checks, README usage notes if setup changes, tests for missing/invalid executable states.
  Acceptance: Missing, invalid, or too-old rclone shows a first-run repair panel with Locate existing rclone, Open official install/download guidance, and optional verified app-managed download only if checksum/signature validation is implemented; capability/version checks re-run after repair; tests cover missing-path and invalid-binary states.
  Complexity: L

- [ ] P2 — Make job history restartable for failed or interrupted transfers
  Why: In-session retry exists, but persisted history is evidence-only; backup operators need a safe restart path after app restart or crash once secrets and command context are protected.
  Evidence: `src/job_history.*`, `src/job_widget.*`, `src/main_window.cpp`, H4R1B0/rclone-gui transfer restart from history, WinSCP transfer queue recovery patterns.
  Touches: `src/job_history.*`, `src/job_widget.*`, `src/main_window.*`, `src/job_options.*`, command serialization/redaction tests.
  Acceptance: Failed, canceled, or interrupted persisted history entries with safe command snapshots expose Restart and Dry Run actions after app relaunch; secret-bearing fields are redacted or vaulted; destructive sync/move reruns require clear confirmation; tests prove restart snapshots survive reload without cleartext secrets.
  Complexity: L

### P3

- [ ] P3 — Add validated operation option profiles per remote
  Why: Expert users need repeatable per-remote copy/sync/mount flags without retyping advanced rclone options, but this should follow the higher-priority safety and persistence work.
  Evidence: Rclone UI configurable command workflows, H4R1B0/rclone-gui sync rule profiles, `src/transfer_dialog.cpp`, `src/preferences_dialog.cpp`.
  Touches: `src/preferences_dialog.*`, `src/transfer_dialog.*`, task serialization code, command-building tests.
  Acceptance: Users can save, edit, and remove validated default flags per remote and operation; transfer dialogs show the active profile, preserve explicit per-job overrides, and tests prove quoting/merge order remains stable.
  Complexity: L
