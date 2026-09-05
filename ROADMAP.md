# ROADMAP — RcloneBrowserNG

Actionable work only. Historical and completed roadmap material is archived in CHANGELOG.md; blocked work is kept in Roadmap_Blocked.md.

## Research-Driven Additions

Added 2026-09-04 from the research pass recorded in RESEARCH.md.

### P1

- [ ] P1 — Remove GUI-thread blocking from scheduling, Open/Edit and the RC engine
  Why: clicking Save in the Schedule dialog can freeze the UI for up to 15 seconds per shell-out, Open/Edit runs a 30-second nested event loop, and starting an RC-backed transfer spins on `msleep` with `processEvents`.
  Evidence: seventeen `waitForFinished(3000..15000)` calls in `src/schedule_manager.cpp` (including lines 270, 310, 360, 416, 459, 492, 561); `QEventLoop loop; ... loop.exec()` with a 30s timer at `src/remote_widget.cpp:210`; `waitForStarted(5000)` plus a `QThread::msleep(50)` + `processEvents` loop at `src/rclone_rc_engine.cpp:55-71`.
  Touches: `src/schedule_manager.{h,cpp}`, `src/schedule_dialog.cpp`, `src/remote_widget.cpp`, `src/rclone_rc_engine.{h,cpp}`, `src/main_window.cpp`
  Acceptance: no `waitForFinished`, `waitForStarted` or `QEventLoop::exec` remains on any path reachable from a user click; scheduling, Open/Edit and RC startup report progress through callbacks with a visible cancel; a test drives `ScheduleManager` against a stub program that sleeps 5s and asserts the call returns before the program exits.
  Complexity: L

- [ ] P1 — Report failures from post-transfer hooks and unmount instead of discarding them
  Why: both run through `QProcess::startDetached`, which captures no output and no exit code, so a broken user hook or a failed unmount is indistinguishable from success, including in job history.
  Evidence: `src/main_window.cpp:3270`, `:3273`, `:3737`, `:3740` (post-command hooks); `src/mount_widget.cpp:370` (`umount`), `:392` (`fusermount -u`).
  Touches: `src/main_window.cpp`, `src/mount_widget.cpp`, `src/job_history.{h,cpp}`
  Acceptance: hooks and unmount use a parented `QProcess` with merged channels; a non-zero exit records the exit code and the last stderr line in job history and raises a background error-queue entry; the mount widget shows "Unmount failed" with the reason rather than silently staying mounted.
  Complexity: M

- [ ] P1 — Write a rotating diagnostic log to disk
  Why: three independent upstream reports ask for it, and a scheduled overnight run that fails currently leaves no evidence at all; the in-session error panel and the redacted support bundle both die with the process.
  Evidence: `qInstallMessageHandler` appears nowhere in `src/`; `kapitainsky#134`, `kapitainsky#233`, `mmozeiko#148`. rclone gained `--log-file` rotation in v1.71 and can write to the same directory.
  Touches: new `src/app_log.{h,cpp}`, `src/main.cpp`, `src/preferences_dialog.{ui,cpp}`, `src/main_window.cpp`
  Acceptance: a `qInstallMessageHandler` writes timestamped lines to `<AppLocalDataLocation>/logs/rclonebrowser.log`, rotating at 5 MB with three generations kept; every job start, exit code and error-queue entry is logged; Preferences exposes the log level and an "Open log folder" action; the existing diagnostics redaction is applied to log lines; a crash handler flushes before exit.
  Complexity: M

- [ ] P1 — Settle and enforce the Qt branch policy before 2026-09-22
  Why: Qt 6.11 leaves open-source support on 2026-09-22 and `validate_qt_version.ps1` still accepts it with no upper bound; the CVE gate runs on Windows only; and CMake declares no Qt minimum at all despite the README badge claiming 6.4+.
  Evidence: endoflife.date/qt gives Qt 6.11 OSS end 2026-09-22, 6.10 ended 2026-04-07, 6.8 LTS commercial to 2029-10-08 with its OSS window closed 2025-04-02 and the highest public patch at 6.8.3, while the CVE-2026-6210 fix needs 6.8.8; Qt 6.12 LTS is targeted for 2026-09-22 (Beta 1 June 2026, wiki.qt.io/Qt_6.12_Release). `CMakeLists.txt:10` calls `find_package(QT NAMES Qt6 REQUIRED ...)` with no version; `scripts/release_windows.cmd:67` is the only caller of `scripts/validate_qt_version.ps1`; `scripts/release_AppImage.sh` and `scripts/release_macOS.sh` have no Qt check; the gate cites CVE-2026-6210 but not CVE-2026-9499, which shares the 6.8.8+/6.11.1+ boundary.
  Touches: `CMakeLists.txt`, `scripts/validate_qt_version.ps1`, new `scripts/validate_qt_version.sh`, `scripts/release_AppImage.sh`, `scripts/release_macOS.sh`, `scripts/release_check.py`, `README.md`, `SECURITY.md`
  Acceptance: `find_package` carries an explicit minimum that matches the README badge and the distro floor the project intends to support; one shared version-policy check runs in all three release scripts, names both CVEs, and additionally fails when the detected branch is past its documented open-source support date; `SECURITY.md` records the chosen branch, the date it must be revisited, and the intended move to 6.12 LTS; building below the minimum fails at configure time with a readable message.
  Complexity: S
  Note: settle the "is 6.8.8 obtainable without a commercial licence" open question in RESEARCH.md first; the answer decides whether the pinned branch is 6.8 or 6.11 until 6.12 ships.

- [ ] P1 — Parse `rclone listremotes --json` instead of splitting on `:`
  Why: the current parser requires exactly two colon-separated parts, but rclone emits `name: type description` when a remote has a description, which yields a garbage type, a failed icon lookup and a misleading tooltip.
  Evidence: `src/main_window.cpp:2802-2822`. Verified against rclone v1.75.0: `listremotes --long` printed `localdisk: alias My main disk: backup`; `listremotes --json` returned `{"name","type","source","description"}` per remote. Likely residue of `mmozeiko#83`.
  Touches: `src/main_window.cpp`, `src/rclone_capabilities.{h,cpp}`, `CMakeLists.txt`, new test fixture
  Acceptance: remotes are read from `listremotes --json` when the detected rclone supports it, falling back to `--long` otherwise; the remote description is shown in the tooltip and the type still drives the icon; a fixture with a description containing a colon lists the remote with the correct type.
  Complexity: S

- [ ] P1 — Show a pre-run summary before a Sync deletes destination files
  Why: `Sync` removes destination files that are absent at the source and today runs with no summary and no deletion count; a user on the upstream tracker lost already-uploaded data by combining a misunderstood exclude path with delete-excluded.
  Evidence: no confirmation, warning or summary path exists in `src/transfer_dialog.cpp` for `ui.rbSync`; `kapitainsky#252`. Cyberduck shows a per-item reconciliation preview with direction arrows before executing; FreeFileSync states the create/update/delete counts.
  Touches: `src/transfer_dialog.cpp`, `src/main_window.cpp`, new `src/sync_preview.{h,cpp}`
  Acceptance: choosing Sync (or enabling `--delete-excluded`) runs `rclone check --combined` or a `--dry-run` pass first and shows counts for files to add, update and delete plus the first twenty deletions, with Run and Cancel; this is a summary panel, not a yes/no confirmation dialog; the preview is skipped when the destination has nothing to delete.
  Complexity: M

- [ ] P1 — Publish a maintainer-owned Homebrew tap
  Why: Homebrew disabled unsigned casks in the official repository on 2026-09-01, so the inherited `rclone-browser` cask is gone, but third-party taps are unrestricted and need only the maintainer's existing GitHub account. This is the one macOS distribution route that is not blocked on Apple enrolment.
  Evidence: workbrew.com/blog/homebrew-5-0-0 and Homebrew discussion #6482 confirm the 2026-09-01 cutoff applies to the official tap only; `scripts/generate_package_manifests.py` already emits a validated Homebrew Cask file.
  Touches: `scripts/generate_package_manifests.py`, `README.md`, new repository `SysAdminDoc/homebrew-rclonebrowserng`
  Acceptance: `brew tap SysAdminDoc/rclonebrowserng && brew install --cask rclone-browser-ng` installs the published macOS artifact; the cask's `sha256` matches the release asset; the README documents the tap and states plainly that the build is unsigned.
  Complexity: S

### P2

- [ ] P2 — Run RC transfers through `sync/*` with `_async` and `_group` instead of `core/command`
  Why: the engine shells the CLI inside the daemon, so there are no per-job stats groups, which is the actual reason live bandwidth tuning, graceful stop and batched operations cannot be built; the three items filed in Roadmap_Blocked.md as "depends on rcd engine (not yet implemented)" are mis-diagnosed — the engine exists, the shim is the blocker.
  Evidence: `src/rclone_rc_engine.cpp:110` posts to `core/command`; the engine only ever calls `rc/noopauth`, `core/command`, `job/status`, `core/stats`, `job/stop`, `core/quit`. rclone.org/rc documents `sync/copy`, `sync/move`, `sync/sync`, `sync/bisync` with `_async` and `_group`, and `core/stats` filtered by group.
  Touches: `src/rclone_rc_engine.{h,cpp}`, `src/main_window.cpp`, `src/rc_job_widget.cpp`, `tests/rc_auth_regression_test.cpp`
  Acceptance: copy, move, sync and bisync jobs started through the engine use the matching `sync/*` endpoint with `_async: true` and a unique `_group`; `RcJobWidget` reads progress from `core/stats?group=<group>`; the existing RC auth gate still passes; falls back to `core/command` when the detected rclone lacks the endpoint.
  Complexity: L

- [ ] P2 — Add live bandwidth and concurrency tuning to running RC jobs
  Why: a running job can only be killed, not slowed; `core/bwlimit` changes it on the live daemon. Moved from Roadmap_Blocked.md — its stated blocker ("depends on rcd engine which hasn't landed") is void; the real prerequisite is the `sync/*` migration above.
  Evidence: rclone.org/rc `core/bwlimit`; `src/rc_job_widget.cpp` has no tuning controls; upstream request rclone#3898.
  Touches: `src/rc_job_widget.{h,cpp}`, `src/rclone_rc_engine.{h,cpp}`
  Acceptance: an RC job's panel offers a bandwidth field that posts `core/bwlimit` and reflects the value the daemon reports back; the change takes effect without restarting the job; the control is hidden for process-backed jobs.
  Complexity: M

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

- [ ] P2 — Offer server-side copy for remote-to-remote transfers
  Why: copying between two remotes on the same provider currently streams every byte through the local machine; `--server-side-across-configs` lets the provider do it, which is the single largest speed win available to this app's remote-to-remote feature.
  Evidence: `src/remote_widget.cpp` remote-to-remote transfer path (commit `b36f753`) passes no such flag; rclone.org/docs documents `--server-side-across-configs` for same-provider config pairs.
  Touches: `src/transfer_dialog.{ui,cpp}`, `src/job_options.{h,cpp}`, `src/rclone_capabilities.cpp`
  Acceptance: when source and destination remotes report the same backend type, the transfer dialog offers "Copy on the server where possible" and adds `--server-side-across-configs`; the option is hidden when the types differ; the job panel notes when a transfer completed server-side.
  Complexity: S

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

- [ ] P2 — Explain filter anchoring and warn on delete-excluded
  Why: rclone filter patterns are unanchored and users read them as paths; one upstream reporter deleted already-uploaded data by combining a wrong exclude with delete-excluded.
  Evidence: `kapitainsky#252` including the reporter's follow-up ("Checking the 'delete excluded' option also deleted the already uploaded folder"); the exclude builder in `src/transfer_dialog.cpp` offers quick-add buttons but no anchoring explanation and no warning on `--delete-excluded`.
  Touches: `src/transfer_dialog.{ui,cpp}`
  Acceptance: the exclude builder shows, for each entered pattern, whether it is anchored and one example of what it will and will not match; enabling delete-excluded turns the exclude list into a warning-styled panel naming what will be removed at the destination.
  Complexity: S

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

- [ ] P2 — Map rclone exit codes to outcomes instead of treating every non-zero as failure
  Why: rclone reserves specific exit codes for outcomes that are not errors, so a job stopped by a transfer or duration cap is reported to the user as a failure with no explanation.
  Evidence: rclone.org/docs#exit-code defines 0 success, 1 syntax error, 2 uncategorised, 3 directory not found, 4 file not found, 5 temporary error, 6 less serious error, 7 fatal error, 8 `--max-transfer` reached, 9 no files transferred (`--error-on-no-transfer`), 10 `--max-duration` reached. `src/job_widget.cpp` records `mExitCode` and `mSuccess` with no code-to-meaning mapping. The v1.69.0 changelog states usage errors moved from 1 to 2 and bisync critical abort from 2 to 7, which disagrees with the current docs table, so the mapping must be verified against the pinned rclone rather than trusted.
  Touches: `src/job_widget.{h,cpp}`, `src/job_history.{h,cpp}`, `src/rc_job_widget.cpp`, new `tests/exit_code_test.cpp`
  Acceptance: each documented exit code renders a named outcome and a one-line explanation in the job card and in history; codes 8, 9 and 10 render as a completed-with-limit state rather than an error; codes 3, 4 and 5 offer Retry; the mapping is verified empirically against the rclone version the release pins and the test asserts every code has a distinct message.
  Complexity: S

### P3

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

- [ ] P3 — Add an in-app update path for the application itself
  Why: the app checks GitHub and opens a browser, which is the least likely of the update routes to be followed through; rclone gets `selfupdate` but the GUI does not.
  Evidence: `src/main_window.cpp:4450` and `:4486` fetch the latest release and open the releases page; `kapitainsky#249`. The AppImage already embeds `gh-releases-zsync` metadata (`scripts/release_AppImage.sh`).
  Touches: `src/main_window.cpp`, `scripts/release_AppImage.sh`, `scripts/release_windows.cmd`
  Acceptance: on Linux the AppImage offers "Download and apply update" using the embedded zsync metadata and verifies the SHA256 before swapping; on Windows and macOS the app downloads the release asset, verifies its `SHA256SUMS` entry, and opens the verified installer; a failed hash aborts with a visible error.
  Complexity: M
  Note: depends on the P0 release-publishing item; there is nothing to update to until releases exist.

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

- [ ] P3 — Require Qt Test only when tests are being built
  Why: `find_package` demands the Test component unconditionally, so a Qt installation without QtTest cannot configure the application even with testing disabled.
  Evidence: `CMakeLists.txt:11` lists `Test` among required components outside any `BUILD_TESTING` guard.
  Touches: `CMakeLists.txt`
  Acceptance: configuring with `-DBUILD_TESTING=OFF` succeeds against a Qt install lacking QtTest and still builds the application; with testing on, a missing QtTest fails with a message naming the component.
  Complexity: S

- [ ] P3 — Give the CLI a `--help` and reject unknown arguments
  Why: the app accepts six command-line flags, prints none of them, and silently launches the GUI when given a typo, which contradicts the project's own rule that failures must not be silent.
  Evidence: `src/main.cpp:56` handles `--version`; `:296` `--run-task`; `:328` `--list-tasks`; `:337` `--send-to`; `:349-350` `--minimized` and `--tray`; no `--help` and no unknown-argument branch.
  Touches: `src/main.cpp`
  Acceptance: `--help` prints every supported flag with one line each and exits 0; an unrecognised argument beginning with `--` prints the same list to stderr and exits non-zero; a smoke test asserts both exit codes.
  Complexity: S

- [ ] P3 — Document the saved-task file migration in the README
  Why: a field report treated the move from `tasks.bin` to `tasks.json` as a compatibility break; the behaviour is intentional but undocumented, and downgrading loses any task created after the upgrade.
  Evidence: `src/list_of_job_options.cpp` reads `tasks.json`, falls back to `tasks.bin`, and thereafter writes only `tasks.json`, leaving the legacy file untouched; PR #15 third observation.
  Touches: `README.md`
  Acceptance: the "Portable vs standard mode" section names `tasks.json` as the current store, states that `tasks.bin` is read once and then left alone, and says plainly that reverting to an older build loses tasks created since the upgrade.
  Complexity: S
