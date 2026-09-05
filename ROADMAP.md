# ROADMAP — RcloneBrowserNG

Actionable work only. Historical and completed roadmap material is archived in CHANGELOG.md; blocked work is kept in Roadmap_Blocked.md.

## Research-Driven Additions

Added 2026-09-04 from the research pass recorded in RESEARCH.md.

### P1

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
