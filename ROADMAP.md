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

### P0

- [ ] P0 — Pin and verify AppImage packaging tools
  Why: Release and local AppImage builds execute mutable `linuxdeploy` continuous artifacts before checksums/provenance are generated.
  Evidence: `.github/workflows/release.yml`, `scripts/release_AppImage.sh`, linuxdeploy continuous releases, GitHub artifact attestation docs, OpenSSF Token-Permissions guidance.
  Touches: `.github/workflows/release.yml`, `scripts/release_AppImage.sh`, optional release-tool helper script/test.
  Acceptance: CI and local AppImage builds fetch versioned or commit-addressed linuxdeploy/plugin artifacts, verify expected SHA256 before `chmod`/execution, and fail closed with a clear error when a hash or version drifts.
  Complexity: M

- [ ] P0 — Enforce the safe Qt floor in local Windows release builds
  Why: The local Windows release script can still package Qt 6.7.3 even though CI rejects Qt versions affected by CVE-2026-6210.
  Evidence: `scripts/release_windows.cmd`, `.github/workflows/build.yml`, `.github/workflows/release.yml`, Qt CVE-2026-6210 advisory.
  Touches: `scripts/release_windows.cmd`, `.github/workflows/release.yml`, optional shared Qt-version validation helper.
  Acceptance: Windows x64 and ARM64 release paths query qmake, reject vulnerable Qt ranges (`<6.8.8` for 6.8, `6.9.x`, `<6.11.1` for 6.11), remove the 6.7.3 default, and print the required remediation.
  Complexity: S

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

- [ ] P1 — Make provider and metadata probes asynchronous
  Why: Remote creation, edit conflict checks, and file properties can freeze the GUI for 15-30 seconds when rclone or a backend stalls.
  Evidence: `src/main_window.cpp::loadRemoteProviders`, `src/remote_widget.cpp::remoteFingerprint`, `src/remote_widget.cpp` properties action, official rclone GUI/dashboard status patterns.
  Touches: `src/main_window.cpp`, `src/main_window.h`, `src/remote_widget.cpp`, `src/remote_widget.h`, `src/progress_dialog.*`, tests for async helpers.
  Acceptance: Provider loading, remote fingerprint, and properties use cancellable async `QProcess` flows with loading/error states; no GUI-thread `waitForFinished()` remains for network-backed metadata probes.
  Complexity: M

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
