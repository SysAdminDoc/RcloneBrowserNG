# ROADMAP — RcloneBrowserNG

Actionable work only. Historical and completed roadmap material is archived in CHANGELOG.md; blocked work is kept in Roadmap_Blocked.md.

## Research-Driven Additions

Added 2026-09-04 and 2026-11-15 from the research passes. RESEARCH.md holds the 2026-11-15 pass.

### P2

- [ ] P2 — Offer "finish current files, then stop" alongside kill
  Why: the only stop today terminates the process mid-file, wasting partial transfers. Moved from Roadmap_Blocked.md for the same reason as the item above.
  Evidence: `src/job_widget.cpp` stop path terminates the process; rclone.org/rc `job/stop` and `job/stopgroup` stop scheduling new work rather than killing in-flight transfers; rclone#966.
  Touches: `src/job_widget.cpp`, `src/rc_job_widget.cpp`, `src/rclone_rc_engine.{h,cpp}`
  Acceptance: RC-backed jobs show both Stop and Finish current files; the latter posts `job/stopgroup` and the panel reports "finishing" until the in-flight transfers complete; process-backed jobs continue to show Stop only.
  Complexity: M

- [ ] P2 — Retrofit `tr()` across the UI and generate an English translation template
  Why: `QTranslator` is loaded and `LinguistTools` is found, but there are two `tr()` calls in hand-written code, so the i18n scaffolding has nothing to translate; both live competitors ship eight and thirty languages. The Weblate enrolment recorded in Roadmap_Blocked.md blocks translator onboarding, not the retrofit itself.
  Evidence: `src/main_window.cpp:2709-2710` are the only `tr()` calls in `src/`; `src/main.cpp:43-54` loads translations from a `translations/` directory that does not exist; `src/CMakeLists.txt:31` finds `LinguistTools` as an optional component with no `lupdate`/`lrelease` target.
  Touches: every `src/*.cpp` with user-visible strings, `src/CMakeLists.txt`, new `translations/rclonebrowser_en.ts`, `src/resources.qrc`
  Acceptance: all `QMessageBox` text, `setText`/`setToolTip`/`setAccessibleName` literals and status strings are wrapped in `tr()`; `qt_add_translations` (or `lupdate`/`lrelease` targets) regenerates `translations/rclonebrowser_en.ts`; the generated `.qm` is embedded via `resources.qrc` and loads at startup; a test asserts the `.ts` contains more than 400 source strings.
  Complexity: L

- [ ] P2 — Expose the transfer-efficiency flags the dialog is missing
  Why: the transfer dialog covers filtering and retries but omits the flags that decide how long a large sync takes and whether it is safe to interrupt.
  Evidence: `src/job_options.cpp` builds arguments from a fixed set that contains no `--track-renames`, `--check-first`, `--order-by`, `--max-duration`, `--cutoff-mode`, `--max-transfer`, `--partial-suffix` or `--inplace`; all are documented global flags in rclone v1.75.
  Touches: `src/transfer_dialog.{ui,cpp}`, `src/job_options.{h,cpp}`, `src/job_options_store.cpp`, `tests/job_options_store_test.cpp`, `tests/dryrun_contract_test.cpp`
  Acceptance: each flag has a control on the transfer dialog's performance tab, round-trips through the saved-task store with a schema bump and migration, and appears in the copy-command-to-clipboard output; the dry-run contract test covers the new flags.
  Complexity: M

- [ ] P2 — Preserve metadata on transfers
  Why: without `--metadata`, permissions, ownership and extended timestamps are dropped on backends that support them, which quietly breaks backup-and-restore use.
  Evidence: no `--metadata` or `--metadata-set` in `src/job_options.cpp`; `rclone lsjson --metadata` and the `--metadata` transfer flag are supported by the backends this app already lists; `operations/fsinfo` reports per-backend metadata capability.
  Touches: `src/transfer_dialog.{ui,cpp}`, `src/job_options.{h,cpp}`, `src/rclone_capabilities.{h,cpp}`
  Acceptance: a "Preserve metadata" checkbox adds `--metadata` and is enabled only when both ends report metadata support; the File Properties dialog shows metadata read via `lsjson --metadata`.
  Complexity: M

- [ ] P2 — Add Windows network-drive mount controls
  Why: mounting to a drive letter without `--network-mode` presents a local disk that several Windows applications refuse to treat as a share, and there is no way to set the volume label; the flag exists only as placeholder text.
  Evidence: `src/preferences_dialog.cpp:65` and `src/remote_widget.cpp:1243` mention `--network-mode` only in a placeholder string; `--volname` appears nowhere; `kapitainsky#210`.
  Touches: `src/mount_options.{h,cpp}`, `src/mount_widget.{ui,cpp}`, `src/preferences_dialog.{ui,cpp}`, `tests/mount_backend_test.cpp`
  Acceptance: on Windows the mount dialog offers a network-drive toggle, a drive-letter picker restricted to free letters, and a volume label field, producing `--network-mode` and `--volname`; the controls are absent on other platforms; the preset validator rejects a network-mode mount to a directory path.
  Complexity: M

- [ ] P2 — Warm and inspect the VFS cache on active mounts
  Why: first access to a large mounted directory is slow because the listing is cold, and pending uploads are invisible until unmount warns about them; both are already solved by rc endpoints the app does not call.
  Evidence: `src/mount_widget.cpp` and `src/vfs_upload_state.cpp` use no rc endpoints; rclone.org/rc documents `vfs/refresh`, `vfs/queue`, `vfs/stats` and `mount/listmounts`; `mmozeiko#132`.
  Touches: `src/mount_widget.{ui,cpp}`, `src/rclone_rc_engine.{h,cpp}`, `src/vfs_upload_state.{h,cpp}`
  Acceptance: a mounted remote's panel offers "Refresh listing" (posting `vfs/refresh` with `recursive`) and shows the pending-upload count and cache size from `vfs/queue` and `vfs/stats`, refreshed on a timer; the existing dirty-cache unmount warning quotes the same numbers.
  Complexity: M
  Note (2026-11-15): the author of Monti, another rclone GUI, wrote it after a "VFS cache that had grown to 40 GB" went unnoticed (forum.rclone.org topic 54504).

- [ ] P2 — Fan a saved task out to several destinations
  Why: one source to many destinations is a paid feature in the closest direct competitor and maps cleanly onto the existing saved-task and staged-queue model.
  Evidence: rcloneview.com lists "1:N cloud synchronization" in its paid tier; `src/job_options.h` carries a single `dest`; `src/staged_transfer.cpp` already sequences multiple queued transfers.
  Touches: `src/job_options.{h,cpp}`, `src/job_options_store.cpp`, `src/transfer_dialog.{ui,cpp}`, `src/main_window.cpp`, `tests/job_options_store_test.cpp`
  Acceptance: a saved task accepts an ordered destination list; running it enqueues one job per destination sharing the source and flags; the store bumps its schema with migration from the single-destination form; job history records each destination separately.
  Complexity: M

- [ ] P2 — Build the remote-creation form from `config/providers` instead of handing off to a terminal
  Why: the dialog already fetches the full provider schema and then throws it away, telling the user it will open rclone's interactive setup in a terminal — which is the single largest source of friction for anyone who came to a GUI to avoid the CLI, and it is the reason `rclone config` needs a working terminal emulator at all.
  Evidence: `src/main_window.cpp:2330-2334` runs `rclone rc --loopback config/providers`; `showCreateRemoteDialog` (from `src/main_window.cpp:2339`) uses it only for the provider list and description. `fs/registry.go` in the rclone tree shows `fs.Option` marshalling `Type`, `DefaultStr`, `ValueStr` next to `Required`, `Advanced`, `Exclusive`, `Sensitive`, `IsPassword` and `Examples`; `fs/backend_config.go` documents `MatchProvider` (comma-separated list, leading `!` negates, blank matches all). Multi-step and OAuth backends are drivable through `rclone config create --non-interactive` then `rclone config update --continue --state <s> --result <r>`, with a reference loop in rclone's `bin/config.py`.
  Touches: `src/remote_provider.{h,cpp}`, `src/main_window.cpp`, new `src/provider_form.{h,cpp}`, `CMakeLists.txt`, new `tests/provider_form_test.cpp`
  Acceptance: choosing a provider renders a form generated from its options, with the widget picked by `Type`, advanced options on a second tab, `Exclusive` options as a closed combo of `Examples`, `Sensitive` and `IsPassword` fields masked and excluded from the copy-command output, and options filtered by `MatchProvider` against the selected provider variant; simple backends (local, alias, crypt, S3, SFTP, WebDAV) are created entirely in-app; backends that need OAuth or extra steps fall into the `--continue` state loop and surface each question in the same form, still opening a browser for OAuth; the terminal handoff remains as the explicit fallback; a test drives the form against a recorded `config/providers` payload and asserts widget type, visibility and masking per option.
  Complexity: L

- [ ] P2 — Use fuse-t's FSKit backend only when its extension is enabled
  Why: on macOS 26 the app adds `-o backend=fskit` to every fuse-t mount where the user hasn't picked a backend, but fuse-t calls FSKit experimental and has the user enable the extension by hand.
  Evidence: `src/mount_backend.cpp:160-167` (Verified); fuse-t 1.1.0 release notes: "Run fuse-t.app from /Applications and follow instructions to manually enable the FSKit extension" (Verified); what a mount does with the extension off (Needs live validation, Open Question 4 in RESEARCH.md); detecting the extension through `pluginkit` (Assumption).
  Touches: `src/mount_backend.{h,cpp}`, `tests/mount_backend_test.cpp`
  Acceptance: with the extension enabled the mount uses `backend=fskit`; with it off the app uses fuse-t's default backend and the mount card shows one line on enabling FSKit; checked on a macOS 26 Mac with fuse-t 1.2.9.
  Complexity: S

- [ ] P2 — Keep the computer awake while jobs run
  Why: a laptop that sleeps mid-sync stops the job, and both a native rclone GUI and a mature backup GUI now block sleep while they work.
  Evidence: no `SetThreadExecutionState`, IOKit power assertion or inhibit call in `src/` (Verified); Vorta v0.12.0 "Block system sleep while a backup is running (#2410)" (Verified); RcloneExplorer 5.4.3 "the computer stays awake during transfers through the desktop portal" (Verified); Rclone Manager v0.3.3 power management (Verified).
  Touches: new `src/sleep_inhibitor.{h,cpp}`, `src/main_window.cpp` (job start and finish), `src/preferences_dialog.{ui,cpp}`, `src/CMakeLists.txt` (IOKit on macOS, QtDBus on Linux)
  Acceptance: a "Keep the computer awake while transfers run" preference, on by default, holds one inhibitor from the first job start to the last job end, and mounts alone don't hold it; Windows uses `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)`, macOS an `IOPMAssertionCreateWithName` assertion of type `PreventUserIdleSystemSleep`, Linux the `org.freedesktop.portal.Inhibit` portal; `powercfg /requests`, `pmset -g assertions` and `systemd-inhibit --list` show it during a job and not after.
  Complexity: M

- [ ] P2 — Start RcloneBrowserNG at login
  Why: auto-mount on launch only survives a reboot if the app starts by itself, and users of four other projects ask for exactly that.
  Evidence (Verified): auto-mount at `src/main_window.cpp:350-363` and `--minimized` at `src/main.cpp:449-453`, but no login entry anywhere (LaunchAgents are written only for schedules, `src/schedule_manager.cpp:459`); kapitainsky/RcloneBrowser #250; Reddit r/rclone 1wayift; rclone #9245; Zarestia-Dev/rclone-manager #91; HN 46075442 on paying for "auto-remount on login". PR #14 made start-minimized depend on always-show-in-tray.
  Touches: new `src/login_item.{h,cpp}`, `src/preferences_dialog.{ui,cpp}`, `src/main_window.cpp`, new `tests/login_item_test.cpp`, `CMakeLists.txt`
  Acceptance: a "Start at login" preference writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` on Windows, a LaunchAgent plist on macOS and `~/.config/autostart/rclone-browser-ng.desktop` on Linux, each launching this binary with `--minimized`, and turns on always-show-in-tray with it; the checkbox reads its state from the entry and repairs one that points at a moved binary; a portable copy registers its own path; an auto-mount that fails at login shows a tray message naming the remote and rclone's error (rclone #9899); tests cover entry generation per platform.
  Complexity: M

- [ ] P2 — Announce job start, progress and finish to screen readers
  Why: a blind user asked for a screen-reader-accessible rclone app with "accurate progress info", and NG's job cards change without telling assistive technology.
  Evidence: Reddit r/rclone 1v14e3v (Verified); `QAccessibleAnnouncementEvent` is "Since: Qt 6.8", while source builds floor at Qt 6.4 (`CMakeLists.txt:17`) (Verified); `src/` sets accessible names but posts no announcements (Verified).
  Touches: `src/job_widget.cpp`, `src/rc_job_widget.cpp`, `src/main_window.cpp`, `tests/accessibility_smoke_test.cpp`
  Acceptance: on Qt 6.8 and newer, starting, finishing and failing a job each post a polite `QAccessibleAnnouncementEvent` naming the job and result, and progress is announced at most every 25%; on older Qt a status label's accessible-name change carries the same text; an NVDA and Narrator pass on Windows 11 (add remote, browse, upload, job finish) hears each one, with findings recorded in CHANGELOG or as ROADMAP items; the smoke test asserts the events fire.
  Complexity: M

- [ ] P2 — Show what a finished sync, copy or move changed
  Why: after a run the job card gives counts but not which files were added, replaced or deleted, which a per-run report in Rclone Manager v0.3.6 and a forum request both cover.
  Evidence (Verified): `rclone sync --combined FILE` writes one line per file marked `=`, `-`, `+`, `*` or `!` (rclone_sync.md at v1.76.0); rclone 1.76.0 added report parameters (`combined`, `missingOnSrc`, `missingOnDst`, `match`, `differ`, `error`, `destAfter`) to `sync/sync`, `sync/copy` and `sync/move`; `src/folder_compare.cpp:315` already reads `check --combined -` output; Rclone Manager v0.3.6 File Report; forum.rclone.org topic 53728.
  Touches: `src/job_options.cpp`, `src/rc_sync_request.cpp`, `src/job_widget.cpp`, `src/rc_job_widget.cpp`, `src/job_history.cpp`, `src/folder_compare.cpp` (share the parser), new `tests/run_report_test.cpp`
  Acceptance: each sync, copy or move keeps a report in the app data folder (CLI `--combined`, rc `combined` on rclone 1.76.0 and later); the job card's "Changes" view lists added, replaced, deleted and failed files with a filter; reports are pruned with job history; with an older rc daemon the view says the report needs rclone 1.76.0.
  Complexity: M

- [ ] P2 — Warn when replacing files into a backup can trip the delete cap
  Why: a sync with `--backup-dir` (or `--suffix`) that only replaces files never opens the deletion summary, yet each replaced file whose name the backup already holds makes rclone delete that older copy first and count it against `--max-delete`, so a daily sync into the same backup folder can stop at the cap of 100 with no warning.
  Evidence (Verified): on rclone v1.76.0 a dry run logs a replaced file's old copy as `"skipped":"move"` (`"move to a.txt.bak"` with `--suffix`) and logs nothing for the older backup copy; with two extra destination files and the backup already holding an older `old1.txt`, the dry run lists two moves but the real run fails at `--max-delete 2` and finishes at 3, and a replaced file whose older copy sits in the backup adds one the same way, with `--backup-dir` and with `--suffix`; `TransferDialog::accept` opens the summary only when `SyncPreview::Summary::deletesAnything()`, and `SyncPreview::CapWarnings` already counts `replacedIntoBackup`.
  Touches: `src/transfer_dialog.cpp`, `src/sync_preview.{h,cpp}`, `tests/sync_preview_test.cpp`
  Acceptance: when the preview finds no deletes but `CapWarnings` returns a warning (replaced files into a backup plus a count cap below their number, or any size cap), Run shows a short dialog with the headline and that warning and Run Sync / Cancel; with no cap, or fewer replaced files than the cap, Run goes straight through as now.
  Complexity: S

- [ ] P2 — Add "Download rclone" to Preferences
  Why: after "Not now" the only way back to the download is restarting the app, and a deleted or broken rclone can't be replaced without finding a binary by hand.
  Evidence (Verified): the "Not now" status in `src/main_window.cpp` (`offerRcloneDownload`) says "restart the app to download it"; `src/preferences_dialog.cpp` has no download entry; `RcloneInstaller::RunDownload` already takes any parent and folder.
  Touches: `src/preferences_dialog.{ui,cpp}`, `src/main_window.cpp`, `tests/accessibility_smoke_test.cpp`
  Acceptance: Preferences shows "Download rclone" beside the rclone path; it runs the same download into the app folder (or the per-user folder), fills the path field, and the remotes load after OK; the "Not now" status points to Preferences instead of a restart.
  Complexity: S

- [ ] P2 — Send the rclone download through the proxy, with GitHub as a second source
  Why: on a network that needs a proxy, or one that blocks downloads.rclone.org, the download fails even when a proxy is saved in Preferences.
  Evidence (Verified): `src/rclone_installer.cpp` has no proxy handling, while `src/main_window.cpp:673-680` saves `useProxy`, `http_proxy`, `https_proxy`, `no_proxy` and `socksProxy`; `Download::Source` holds one base URL; `github.com/rclone/rclone/releases/download/v1.76.0/SHA256SUMS` serves the same signed list.
  Touches: `src/rclone_installer.{h,cpp}`, `tests/rclone_installer_test.cpp`
  Acceptance: with a proxy saved, every download request goes through it (a local proxy stub in the test sees them), and the system proxy is used otherwise; when rclone.org can't be reached or answers 404, the download tries the GitHub release once with the same checks, the signature check from the P1 item included.
  Complexity: S

- [ ] P2 — Offer to install WinFsp when a mount needs it
  Why: mounting on Windows without WinFsp opens the WinFsp download page in a browser and stops, so a new user still has to find, download and run an installer by hand before the mount works.
  Evidence (Verified): `src/main_window.cpp:5244-5254` shows "WinFsp required" and calls `QDesktopServices::openUrl(WinFspFloor::kDownloadUrl)`; `src/winfsp_floor.cpp` already knows the minimum safe WinFsp version.
  Touches: `src/main_window.cpp`, `src/winfsp_floor.{h,cpp}`, `src/rclone_installer.{h,cpp}` (shared download and progress code), `tests/winfsp_floor_test.cpp`
  Acceptance: the dialog offers "Install WinFsp", downloads the current stable MSI from WinFsp's GitHub release, checks its Authenticode signature and publisher, runs it elevated with basic UI and carries on with the mount when it finishes; Cancel keeps today's behavior.
  Complexity: M

### P3

- [ ] P3 — Replace the Open/Edit nested event loop with a callback flow
  Why: `remoteFingerprint()` spins a nested `QEventLoop` on the Open/Edit path. It is window-modal with a working Cancel and a 30-second timeout, so the window stays responsive and this is not a freeze, but a nested loop still re-enters the stack and is the last one left on a user-click path.
  Evidence: `src/remote_widget.cpp:211` (`QEventLoop loop; ... loop.exec()`), reached from `src/remote_widget.cpp:174`, `:297` and `:333`. Assessed and deliberately deferred on 2026-09-05 when the rest of the blocking work landed: the RC engine freeze it was grouped with is fixed, and converting this one changes three call sites into continuations for no user-visible gain. Everything else left in `src/` is destructor teardown (`folder_compare.cpp:271`, `rclone_rc_engine.cpp:19`) or the headless `--run-task` CLI (`main.cpp:324`).
  Touches: `src/remote_widget.cpp`
  Acceptance: `remoteFingerprint` takes a completion callback and returns immediately; the three call sites continue from that callback; the progress dialog, its Cancel and the 30-second timeout still behave as they do now; no `QEventLoop::exec` remains in `src/remote_widget.cpp`.
  Complexity: M

- [ ] P3 — Kill immediately on Windows instead of waiting out a terminate that cannot arrive
  Why: `QProcess::terminate()` posts `WM_CLOSE` to top-level windows and the main thread, which a console child such as rclone never receives, so every cancel on Windows sits through the full five-second fallback before anything happens.
  Evidence: doc.qt.io/qt-6/qprocess.html states terminate has no effect on a process with no event loop or window; `src/job_widget.cpp:538` calls `terminate()` then `kill()` after 5000 ms, and the same pattern appears at `src/mount_widget.cpp:399`, `src/remote_widget.cpp:1927` and `src/stream_widget.cpp:118-119`.
  Touches: `src/job_widget.cpp`, `src/mount_widget.cpp`, `src/remote_widget.cpp`, `src/stream_widget.cpp`
  Acceptance: on Windows the cancel path calls `kill()` directly and the "Stopping" state clears within a second; on POSIX `terminate()` plus the existing fallback is retained so `SIGTERM` still gets its chance; mount unmount keeps its existing dirty-VFS-cache warning ahead of either path.
  Complexity: S

- [ ] P3 — Surface bisync conflicts as a resolvable list
  Why: bisync conflicts are exposed only as a `--conflict-resolve` policy chosen before the run, so a user who picks "none (report only)" gets a log line and no way to act; the field's best-documented model is a per-file conflict list with a stated remedy.
  Evidence: `src/transfer_dialog.cpp:322-330` offers the policy combo and nothing else; Mountain Duck documents nine conflict scenarios each mapped to a state and a manual remedy (docs.mountainduck.io/mountainduck/connect/sync/); rclone writes `..path1`/`..path2` conflict copies.
  Touches: `src/folder_compare.{h,cpp}`, `src/job_widget.cpp`, new conflict dialog
  Acceptance: after a bisync run that reported conflicts, the job panel offers "Review conflicts", listing each pair with size, modification time and hash, and per-file keep-path1 / keep-path2 / keep-both actions that issue the corresponding copy or delete.
  Complexity: L
  Note (2026-11-15): Rclone Manager v0.3.6 shipped resolution of Check and BiSync conflicts, and Bisync Hub (forum.rclone.org topic 54406) asks for the same thing.

- [ ] P3 — Add an in-app update path for the application itself
  Why: the app checks GitHub and opens a browser, which is the least likely of the update routes to be followed through; rclone gets `selfupdate` but the GUI does not.
  Evidence: `src/main_window.cpp:4450` and `:4486` fetch the latest release and open the releases page; `kapitainsky#249`. The AppImage already embeds `gh-releases-zsync` metadata (`scripts/release_AppImage.sh`).
  Touches: `src/main_window.cpp`, `scripts/release_AppImage.sh`, `scripts/release_windows.cmd`
  Acceptance: on Linux the AppImage offers "Download and apply update" using the embedded zsync metadata and verifies the SHA256 before swapping; on Windows and macOS the app downloads the release asset, verifies its `SHA256SUMS` entry, and opens the verified installer; a failed hash aborts with a visible error.
  Complexity: M
  Note (2026-11-15): the release-publishing dependency is met. v2.0.2 was published on 2026-09-05 with a `SHA256SUMS` file, so there is something to update to.

- [ ] P3 — Fix the latent memory-safety and lifetime defects found in the audit
  Why: none is reachable today, but each is one call-site change away from an out-of-bounds access or a per-invocation leak, and the project has no sanitizer build to catch them.
  Evidence: `src/item_model.cpp:8-16` computes `(int)((size_t)text.length() - 2)` with no length guard and can index `spinner[5]` on a four-element array; `src/job_widget.cpp:225` computes a percentage with no clamp; `src/remote_widget.cpp:1906-1916` allocates an unused second `QProcess` per `serve` invocation and never deletes it; `src/item_model.cpp:399-412` returns `false` from `dropMimeData` after emitting `drop(...)`.
  Touches: `src/item_model.cpp`, `src/job_widget.cpp`, `src/remote_widget.cpp`
  Acceptance: `advanceSpinner` returns unchanged for inputs shorter than two characters and clamps the index; the percentage is bounded to 0-100; the serve path constructs no unused process; `dropMimeData` returns `true` when it emitted `drop`; each fix has a test that fails before it.
  Complexity: S

- [ ] P3 — Add a sanitizer build option and local fuzz targets for the three parsers
  Why: the listing parser, the stats parser and the task-store loader are the historically broken surfaces and all three consume input the app does not control; no sanitizer or fuzzing configuration exists. Moved from Roadmap_Blocked.md — its prerequisite (parser test infrastructure) exists once the P1 parser-extraction item lands, and local targets need no CI.
  Evidence: `-fsanitize` appears nowhere in `CMakeLists.txt` or `src/CMakeLists.txt`; the parsers live in `src/item_model.cpp`, `src/job_widget.cpp` and `src/job_options_store.cpp`.
  Touches: `CMakeLists.txt`, new `fuzz/` directory, `scripts/release_check.py`
  Acceptance: `-DENABLE_SANITIZERS=ON` builds the app and tests with ASan and UBSan on GCC/Clang and the full suite passes under them; three libFuzzer targets build behind `-DENABLE_FUZZING=ON` with seed corpora taken from the recorded fixtures; each runs 60 seconds clean.
  Complexity: M

- [ ] P3 — Bring MSVC and GCC warning policy into line
  Why: unused locals are a build failure on Linux and invisible on Windows, so the two legs of the build disagree about what counts as a warning and Windows-only code drifts.
  Evidence: `src/CMakeLists.txt:11` sets `/W4 /WX /wd4100 /wd4189 /wd4996`; `src/CMakeLists.txt:16` sets `-pedantic -Wall -Wextra -Werror -Wno-deprecated-declarations`.
  Touches: `src/CMakeLists.txt`, whichever sources the newly enabled warnings flag
  Acceptance: `/wd4189` is removed and the resulting warnings are fixed rather than suppressed; `/wd4100` is either removed or matched by `-Wno-unused-parameter` on the other compilers so both legs agree; a Release build is clean on MSVC and GCC.
  Complexity: S
  Note (2026-11-15): PR #19 (giflw, merged 2026-11-14) needed source changes to build with warnings as errors, so outside contributors do build that way.

- [ ] P3 — Require Qt Test only when tests are being built
  Why: `find_package` demands the Test component unconditionally, so a Qt installation without QtTest cannot configure the application even with testing disabled.
  Evidence: `CMakeLists.txt:11` lists `Test` among required components outside any `BUILD_TESTING` guard.
  Touches: `CMakeLists.txt`
  Acceptance: configuring with `-DBUILD_TESTING=OFF` succeeds against a Qt install lacking QtTest and still builds the application; with testing on, a missing QtTest fails with a message naming the component.
  Complexity: S

- [ ] P3 — Give the CLI a `--help` and reject unknown arguments
  Why: the app accepts six command-line flags, prints none of them, and silently launches the GUI when given a typo, which contradicts the project's own rule that failures must not be silent.
  Evidence: `src/main.cpp:55` handles `--version` before `QApplication` exists; `:295` `--run-task`; `:393` `--list-tasks`; `:438` `--send-to`; `:450-451` `--minimized` and `--tray`; no `--help` and no unknown-argument branch. `--help` belongs with `--version`, so it works with no display.
  Touches: `src/main.cpp`
  Acceptance: `--help` prints every supported flag with one line each and exits 0; an unrecognised argument beginning with `--` prints the same list to stderr and exits non-zero; a smoke test asserts both exit codes.
  Complexity: S

- [ ] P3 — Keep test runs out of the developer's real app data folder
  Why: six QTest targets rename the app (`rclone-browser-log-test`, `-layout-test`, `-remotes-test` and others) without test mode and then write logs and `job-history.json` through `AppLocalDataLocation`, so every run leaves folders in `%LOCALAPPDATA%` that nothing removes. The two that use test mode leave theirs under `%LOCALAPPDATA%\qttest`. Six such folders were found and cleared on 2026-11-15, the oldest from 2026-09-05.
  Evidence: `setApplicationName` without `QStandardPaths::setTestModeEnabled` in `tests/app_log_test.cpp`, `async_helpers_test.cpp`, `layout_regression_test.cpp`, `rc_route_test.cpp`, `remote_daemon_test.cpp` and `remotes_list_test.cpp`; `accessibility_smoke_test.cpp` and `adaptive_bandwidth_test.cpp` use test mode, and none of the eight removes its folder. The writers are `src/app_log.cpp:133`, `src/job_history.cpp:84` and `src/identity_migration.cpp:205`.
  Touches: those eight tests (test mode in `initTestCase` where it's missing, `removeRecursively()` of the app's own folder in `cleanupTestCase`); `identity_migration_test.cpp` needs care because it sets the legacy name on purpose.
  Acceptance: a full `ctest` run leaves `%LOCALAPPDATA%` with no folder it didn't have before (compare a listing before and after), and every touched test still passes.
  Complexity: S

- [ ] P3 — Add "Stop all jobs" and "Unmount all" to the tray menu
  Why: when a remote stops answering, every running job and mount has to be stopped one card at a time from the main window, and a mount that can't reach its remote can make Windows Explorer very unresponsive.
  Evidence (Verified): the tray menu has only Show, Job History, Watch Folders and Quit (`src/main_window.cpp:1641-1661`); Rclone Manager v0.3.3 "Emergency Stop" ("immediately unmounts all remotes and halts all active serves"); forum.rclone.org topic 54067.
  Touches: `src/main_window.cpp`, `src/job_widget.cpp`, `src/mount_widget.cpp`, `src/stream_widget.cpp`, `src/remote_widget.cpp` (serves)
  Acceptance: both actions run at once with no dialog and finish with a tray message giving the count stopped; "Unmount all" skips any mount whose VFS cache still has uploads pending and names it in that message, so the existing dirty-cache warning is never bypassed; both entries are disabled when there's nothing to stop.
  Complexity: S

- [ ] P3 — Preview files in remote mode through `operations/getfile`
  Why: preview is local-only today, so a user browsing a remote daemon sees the action disabled, and rclone 1.76.0 added an rc call that returns a file's contents.
  Evidence (Verified): `src/remote_widget.cpp:1828-1836` (the local-only tooltip); rclone 1.76.0 rc docs for `operations/getfile`: `maxSize` "can only lower the built-in maximum" (default 2097152, 2 MiB) and `base64` returns the contents base64-encoded.
  Touches: `src/remote_daemon.{h,cpp}`, `src/remote_widget.cpp`, `tests/remote_daemon_test.cpp`
  Acceptance: against a daemon at 1.76.0 or newer, preview fetches files of 2 MiB or less with `base64` and shows them in the existing preview; a larger file says it's over the limit; an older daemon keeps the local-only tooltip; tests cover the version gate and base64 decoding.
  Complexity: S
