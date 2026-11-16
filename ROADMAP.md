# ROADMAP — RcloneBrowserNG

Actionable work only. Historical and completed roadmap material is archived in CHANGELOG.md; blocked work is kept in Roadmap_Blocked.md.

## Research-Driven Additions

Added 2026-09-04 and 2026-11-15 from the research passes. RESEARCH.md holds the 2026-11-15 pass.

### P2

- [ ] P2 — Let a later try in the same stats group delete what the CLI's would
  Why: when a sync's tries share one stats group (a `--max-delete-size` run after a delete, or a daemon that never answered options/get), the group still says it errored from the earlier try. deleteFiles then refuses with "not deleting files as there were IO errors" (fs/sync/sync.go:600-613, v1.76.0) even on a try that had no errors of its own. The CLI calls ResetErrors before each retry (cmd/cmd.go:287-289), so its later try deletes.
  Evidence: `src/rc_job_result.cpp` PlanNextAttempt keeps the group in that case; rc has no call that clears a group's errors and keeps its deletesSize (core/stats-reset clears both). Found by reading the source, not measured.
  Touches: `src/rc_job_widget.cpp`, `src/rc_job_result.{h,cpp}`, `tests/rc_job_widget_test.cpp`
  Acceptance: measured first on the CLI and the daemon with `sync --max-delete-size 1G` and a file locked on try 1 only; if the daemon's try 2 skips deletes the CLI makes, the card gets them made (a new group carrying the counted bytes in _config, say) and a real-rclone row in rc_job_widget_test shows the same files left as the CLI; if it doesn't, the entry is deleted with what was measured.
  Complexity: M

- [ ] P2 — Preview a Copy or Move that carries a delete cap
  Why: the deletion preview runs only for Sync or `--delete-excluded`, yet rclone v1.76.0 counts a Move's moved files against `--max-delete` when one replaces a destination file or crosses remotes, and a Copy with `--backup-dir` counts replaced files' backup moves. A Copy or Move whose extra options carry `--max-delete` can stop partway with no warning. So can a Copy or Move with `--backup-dir`, `--suffix` or `--fix-case` and a cap but `--delete-excluded` off, since nothing else starts the check (the verifier of c8f916a measured the Move case). The dialog's cap control is Sync-only and its tooltip says "Only Sync deletes at the destination", which reads as "Copy and Move can't hit a cap".
  Evidence: `src/transfer_dialog.cpp` acceptWithDeletionPreview gate; measured move_overwrite (local `rclone move` replacing dst/a.txt fails at `--max-delete 0`) and mem_move_excluded (local to memory, 2 files, fails at 0 and 1).
  Touches: `src/transfer_dialog.cpp`, `src/sync_preview.{h,cpp}`, `tests/accessibility_smoke_test.cpp`
  Acceptance: a Copy or Move whose options hold `--max-delete` or `--max-delete-size` gets the dry-run preview, and its summary opens at a cap the measured runs fail at; the tooltip no longer implies Copy and Move are never counted; a smoke test drives a Move over an existing file at cap 0.
  Complexity: M

- [ ] P2 — Read `global.` options from a remote's config section
  Why: rclone v1.76.0 applies a `global.<option>` from a remote's section in rclone.conf when it opens that remote, the same as one in a connection string (fs/newfs.go:98-145). EffectiveArguments reads the connection string's only, so a section carrying `global.fix_case` or `global.backup_dir` reaches the run without the summary knowing. A section's `global.max_delete` also stops the deletion check's own dry run partway, since the dry run renames that option only where a path spells it out. The summary then quotes rclone's error instead of listing every file. The review of c305041 measured that with the cap inside an alias's value (`remote = :local,global.max_delete=1:D`): the dry run exits 7 after listing 1 of 5 deletes. A path held in an `RCLONE_` variable does the same to the dry run, though the summary now reads its cap. `RCLONE_COMPARE_DEST` set to `":local,global.max_delete=1:C"` stopped a `--dry-run` sync after 1 of 5 deletes (v1.76.0, 2026-11-16), and DryRunEnvironment only drops `RCLONE_MAX_DELETE` and `RCLONE_MAX_DELETE_SIZE`.
  Evidence: `src/sync_preview.cpp` EffectiveArguments and ConnectionOptions; `src/job_options.cpp` DryRunArguments; read in fs/newfs.go, not measured for the config section.
  Touches: `src/sync_preview.{h,cpp}`, `src/transfer_dialog.cpp`, `tests/sync_preview_test.cpp`
  Acceptance: measured first with a section holding `global.fix_case = true` on v1.76.0; the summary reads the section's `global.` options (through `config/get` or `rclone config dump` on the run's own config, password included), and a section's `global.max_delete` or `global.max_delete_size` doesn't stop the check's dry run, nor does one in a path an `RCLONE_` variable holds, with real-rclone rows in sync_preview_test for both.
  Complexity: M

- [ ] P2 — Keep a dry run dry when rclone.conf or the environment sets `global.dry_run`
  Why: a dry run renames `global.dry_run` in its arguments (JobOptions::KeepDryRunOn), but rclone also reads it from the opened remote's config section and from `RCLONE_CONFIG_<REMOTE>_GLOBAL.DRY_RUN`. Measured on v1.76.0 (2026-11-16): `sync --dry-run src dd:` deleted for real with `global.dry_run = false` in `[dd]`, through an alias pointing at that section too, and with `RCLONE_CONFIG_DD_GLOBAL.DRY_RUN=false` set. A CLI preview, the task list's and history's Dry Run and the dialog's Dry Run button all go through rclone's own config. rc jobs (sync/*) skip `global.` options (IsRCRequest), core/command children don't. Three more forms went real in the same measurements. A plain local path reads a `[local]` section (no `type` needed) and `RCLONE_CONFIG_LOCAL_GLOBAL.DRY_RUN`. A `--config` in a task's extra options or the default options replaces the app's own, since the last one wins. An environment variable that holds a path, like `RCLONE_BACKUP_DIR` or `RCLONE_ALIAS_REMOTE` set to `:local,global.dry_run=false:D`, carries the option past the rename, which only reads arguments.
  Evidence: `src/job_options.cpp` KeepDryRunOn; each case reproduced with a throwaway rclone.conf and a local destination of three files; addConfigToContext in fs/newfs.go reads config.Get, which takes the section and env.
  Touches: `src/job_options.{h,cpp}`, `src/sync_preview.{h,cpp}`, `src/main_window.cpp`, `tests/dryrun_contract_test.cpp`
  Acceptance: before any dry run, the app reads the sections of every remote its paths name (nested ones too, and `local` for a plain path, through `config dump` on the run's own config, which is the last `--config` in its arguments) and its environment, path-valued `RCLONE_` variables included, and refuses the dry run with a message naming the remote when one turns `global.dry_run` off (or passes `--config` with the section's key renamed, if that's measured to work); real-rclone rows cover the section, the alias, `[local]`, an extra-options `--config` and the env forms.
  Complexity: M

- [ ] P2 — Word a Dropbox or Koofr `--suffix` replace as a delete
  Why: on a remote that can't set a file's time without deleting it (backend/dropbox and backend/koofr return ErrorCantSetModTimeWithoutDelete), equal() removes the old file when `--backup-dir` is empty (fs/operations/operations.go:338-348, v1.76.0) instead of renaming it to the `--suffix` name, and that remove isn't counted against the caps. The summary says the old copy moves into the backup and warns about a cap it can't reach.
  Evidence: `src/sync_preview.cpp` NoDeleteDetail and CapWarnings treat every re-upload under a backup the same way; read in the source, not measured (no Dropbox or Koofr account).
  Touches: `src/sync_preview.{h,cpp}`, `tests/sync_preview_test.cpp`
  Acceptance: measured first on a Dropbox or Koofr remote with `--suffix .old` and a file whose time differs; the summary says the old copy is removed, not backed up, and gives no cap warning for it, as a fixture row.
  Complexity: S

- [ ] P2 — Word `rclone move`'s source deletes as source deletes in the preview
  Why: under `rclone move`, a source file that already matches the destination is deleted from the source, and the dry run logs that as "delete". The summary words every "delete" as a destination delete ("Move will delete N files"), which is wrong about where the files go missing.
  Evidence: `src/sync_preview.cpp` Parse counts every "delete" into toDelete and DeletionTitle says "will delete"; rclone `fs/sync` move path deletes matched source objects.
  Touches: `src/sync_preview.{h,cpp}`, `src/transfer_dialog.cpp`, `tests/sync_preview_test.cpp`
  Acceptance: a measured `rclone move --dry-run` log with a matching source file yields a summary that says the file leaves the source, not the destination; the count still feeds the cap warning; a fixture test covers it.
  Complexity: S

- [ ] P2 — Skip the deletion preview for `--delete-excluded` under Copy and Move
  Why: rclone v1.76.0 deletes nothing at the destination for `--delete-excluded` under `copy` or `move` (measured: no delete lines in the dry run), yet the option alone makes the dialog run a dry-run preview first, which costs a full listing for nothing.
  Evidence: `src/transfer_dialog.cpp` acceptWithDeletionPreview treats `--delete-excluded` as a delete trigger for every mode; measured move_overwrite dry run with `--exclude *.log --delete-excluded` logs only the move.
  Touches: `src/transfer_dialog.cpp`, `tests/accessibility_smoke_test.cpp`
  Acceptance: Copy or Move with only `--delete-excluded` runs without a preview (unless the cap item above applies); Sync keeps it; a smoke test checks both.
  Complexity: S

- [ ] P2 — Keep live progress for a job with its own speed limit
  Why: the daemon has one bandwidth and TPS limit for every job and drops a job's own, so a task with `--bwlimit`, `--tpslimit` or `--tpslimit-burst` (and every transfer while the status bar Limit is set) now runs on core/command, where its stats never reach the card until it ends.
  Evidence: `src/rc_sync_request.cpp` `sharedLimitReason`; `src/rclone_rc_engine.cpp` startTransfer routes those to startCommand with a route note; `src/rc_job_widget.cpp` setStartedJob prints "Live progress isn't shown for this run".
  Touches: `src/main_window.cpp` route choice, `src/rclone_rc_engine.{h,cpp}`, `src/job_widget.cpp`
  Acceptance: a local Copy with `--bwlimit 1M` shows live bytes, speed and ETA on its card while it runs and still honors the limit; a remote-daemon job keeps core/command with the route note; a test drives the limited job and sees at least one progress update before the end.
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

- [ ] P3 — Close the last gaps between the card's retry loop and the CLI's
  Why: a run that stays in one stats group (`--max-delete-size` after a delete, or a daemon that never answered options/get) keeps an earlier try's retry flag, since rc has no call that clears a group's errors and keeps its deletes. The card then judges the try by the error its job returned, and knows only rclone's own no-retry errors (`immutable file modified`, `context deadline exceeded`). A backend's own no-retry error in that mode gets one try more than the CLI gives it. Separately, a try that hits a soft `--max-transfer` stop along with a retryable error hasn't been measured against the CLI on the daemon route. And a run that stays in its group with no size left gets MaxDeleteSize "0b" in that group's _config (PlanNextAttempt), where deletesSize is already above 0, so Stats.DeleteFile refuses even a 0-byte delete (fs/accounting/stats.go:677-678). A new group starts at 0 and lets those through. No v1.76.0 input reaches the same-group case today, since options/get gives every cap or none.
  Evidence: `src/rc_job_widget.cpp` retryAfterFailure (`mRetryFlagCarried`) and noteLimitStop; `src/rc_job_result.cpp` IsNoRetryError; rclone v1.76.0 `fs/accounting/stats.go` ResetErrors runs only from cmd.Run, and core/stats-reset also clears deletesSize.
  Touches: `src/rc_job_widget.cpp`, `src/rc_job_result.{h,cpp}`, `tests/rc_job_widget_test.cpp`
  Acceptance: the CLI and the card print the same attempt lines and exit code for (a) `sync --max-delete-size 1G` where try 2 hits only a backend no-retry error, and (b) `copy --max-transfer 1k --cutoff-mode soft` with a locked file in the same try; both are rows in rc_job_widget_test against real rclone or a fake daemon fed the measured stats. (c) A same-group run with no size left lets a 0-byte delete through, in a fake-daemon row that reaches PlanNextAttempt's same-group path.
  Complexity: M

- [ ] P3 — Read a core/command child's exit code when the task logs to its own file
  Why: a task whose extra options carry `--log-file` sends the child rclone's whole log to that file, so core/command hands back an empty result and the card falls back to exit 1. Measured on v1.76.0 (2026-11-16): `copy <missing> <dst> --retries 1 --use-json-log -v --log-file child.log` exits 3 on the command line, and core/command with the same arguments returned an empty result with its error flag set.
  Evidence: `src/rc_job_result.cpp` ExitCodeFromLog reads output.result only; `src/job_options.cpp` appends the extra options as typed, and nothing in `src/` strips `--log-file`.
  Touches: `src/rc_job_widget.cpp`, `src/rc_job_result.{h,cpp}`, `tests/rc_route_test.cpp`
  Acceptance: on a local daemon the card reads the task's log file from the size it had before the run and records 3 for that copy, in a real-rcd row. On a remote daemon, whose files can't be read, the card says the code came from a run with no log.
  Complexity: S

- [ ] P3 — Measure a bisync that returns success after counting errors
  Why: when a command returns nil after the stats counted an error, cmd.Run exits on the stats' last error (cmd/cmd.go:257 and 326 on v1.76.0). sync/bisync reports success in that case, so the card would record 0 where the command line exits non-zero. No bisync input known today does it.
  Evidence: rclone v1.76.0 `cmd/cmd.go` Run; `src/rc_job_widget.cpp` retryAfterFailure's bisync branch reads only the job's own error.
  Touches: `src/rc_job_widget.cpp`, `tests/rc_job_widget_test.cpp`
  Acceptance: a bisync input that counts an error and still returns nil is found and measured on v1.76.0, and the card then reads the group's last error on a successful bisync and records the command line's code, in a real-rclone row. If no such input turns up in cmd/bisync, this item goes.
  Complexity: S

- [ ] P3 — Tell a prefix `--name-transform` backup move from a rename
  Why: `sync --name-transform file,prefix=x_ --backup-dir bak --track-renames` with an older dst/x_a.txt logs "x_a.txt move to x_x_a.txt" beside "a.txt copy", and the real run puts x_x_a.txt into bak (measured on v1.76.0 by the verifier of c8f916a). The summary calls it a rename ("Sync will rename 1 file") and warns at caps 0 and 1, which the real local run never hits. It over-warns only.
  Evidence: `src/sync_preview.cpp` LooksLikeBackupName tries splits at dots only, so a prefix inserted before the name isn't recognized, and Parse's non-Move branch counts it as renamed.
  Touches: `src/sync_preview.{h,cpp}`, `tests/sync_preview_test.cpp`
  Acceptance: the measured log gives "move old copies of 1 file into the backup" under `--track-renames`, the same as without it, as a fixture row; a real `--track-renames` rename still reads as one.
  Complexity: S

- [ ] P3 — Read flag values in FlagsIn the way pflag does
  Why: FlagsIn skips the token after `--exclude`, `--include` or `--filter` even when that token is the value of an earlier string flag, and it doesn't skip the values of other string flags. With `--user-agent --exclude --fix-case`, rclone takes `--exclude` as the user agent and turns `--fix-case` on, while FlagsIn reads it off (measured on the CLI by the verifier of c8f916a). It needs a nonsense argument list, but it under-warns when it happens.
  Evidence: `src/sync_preview.cpp` FlagsIn kPatternFlags skip.
  Touches: `src/sync_preview.{h,cpp}`, `tests/sync_preview_test.cpp`
  Acceptance: FlagsIn knows which rclone flags take a value (from `rclone help flags` on v1.76.0, or a list checked against it) and skips exactly those values; the measured case reads `--fix-case` on, as a data row. EffectiveArguments makes the same guess about which tokens are paths and should use the same list.
  Complexity: S

- [ ] P3 — Keep the Move check's one-file-at-a-time rule when the user's filters hold `!`
  Why: DryRunExtras adds `--filter "- /.rclone-browser-ng-dry-run-matches-nothing"` so a Move's dry run lists each file instead of one server-side folder move. A `!` rule clears every rule before it (fs/filter/rules.go:296), and `--filter` rules come before `--filter-from` ones, so a `!` in the user's filter file drops the added rule. The check then shows a folder move where the real run, into an existing folder, moves file by file. A `--max-size` near 4 EiB keeps the filter active without a rule (MaxSize ≥ 0 makes InActive false, and a file of unknown size stays in), but a command-line `--max-size` overrides an `RCLONE_MAX_SIZE` variable, so the check would list files the real run leaves alone.
  Evidence: `src/sync_preview.cpp` DryRunExtras; read in fs/filter, not measured.
  Touches: `src/sync_preview.{h,cpp}`, `tests/dryrun_contract_test.cpp`
  Acceptance: measured first on v1.76.0 with a filter file holding `!` and a Move into a missing and an existing folder; the check lists each file in both, and a `--max-size` from the task, the environment or a path still applies to it, as real-rclone rows.
  Complexity: S

- [ ] P3 — Drop the cap warning for a Move rclone does as one folder move
  Why: a Move into a destination folder that doesn't exist yet, with no filters, is one server-side DirMove (fs/sync/sync.go:1282 when the filter is inactive), which counts no deletes. The check still lists it file by file and warns that a cap could stop the run.
  Evidence: `src/sync_preview.cpp` DryRunExtras and CapWarnings; read in fs/sync, not measured against a capped run.
  Touches: `src/sync_preview.{h,cpp}`, `src/transfer_dialog.cpp`, `tests/sync_preview_test.cpp`
  Acceptance: measured first with `move --max-delete 0` into a missing folder on a local and a memory remote; where the real run passes, the summary gives no cap warning, as a data row.
  Complexity: S

- [ ] P3 — Read a bisync's conflict renames in the check
  Why: bisync renames a conflict's two copies (to `..path1` and `..path2` unless `--conflict-suffix` says otherwise), and its dry run logs those as renames that Parse skips, so the summary doesn't mention them.
  Evidence: `src/sync_preview.cpp` Parse; not measured.
  Touches: `src/sync_preview.{h,cpp}`, `tests/sync_preview_test.cpp`
  Acceptance: measured first with a bisync dry run over a file changed on both sides; the summary says how many files it renames, from a captured log as a fixture row.
  Complexity: S

- [ ] P3 — Check pane moves and delete batches for `global.` options in rclone.conf
  Why: transfers now read the daemon's `config/dump` and run as a separate rclone when a section, a section's variable or a path in an `RCLONE_` variable sets a `global.` option, because a job the daemon runs itself skips it. Two other rc calls go straight to the daemon. A drag between panes on a remote daemon posts sync/move or operations/movefile, and a multi-item delete posts job/batch with operations/deletefile and operations/purge. A section's `global.backup_dir` or `global.max_delete` would then do nothing there, while the CLI form of the same action applies it.
  Evidence: `src/remote_daemon.cpp` builds sync/move and operations/movefile; `src/rc_delete_batch.cpp` builds the batch; neither reads the config. Not measured.
  Touches: `src/remote_daemon.cpp`, `src/rc_delete_batch.cpp`, `src/rclone_rc_engine.cpp`, `tests/rc_route_test.cpp`
  Acceptance: measured first on v1.76.0 with `[bk] global.max_delete = 1` and a two-item delete; if the CLI stops where the batch doesn't, both calls take the core/command route on the same check RcSyncRequest::ConfigGlobalsReason makes, with a real-rcd row.
  Complexity: S

- [ ] P3 — Cover a retry and a remote daemon's environment in the `global.` check
  Why: a transfer reads the daemon's config/dump once, in startTransfer, and the card's retries post the same sync/* call again. A section that gains a `global.` option between tries (rclone.conf edited, or Preferences pointed at another config mid-run) is skipped on the next try. On a remote daemon, a `global.` option its host sets through `RCLONE_CONFIG_<REMOTE>_GLOBAL.<OPTION>` never shows in config/dump. Measured on v1.76.0 (2026-11-16) with an rcd started under `RCLONE_CONFIG_BK_GLOBAL.BACKUP_DIR=bk:bak`: config/dump listed only `remote` and `type` for `[bk]`, sync/sync deleted the four destination files, and the CLI and core/command moved them into `bak`.
  Evidence: `src/rclone_rc_engine.cpp` startTransfer (the environment is empty when isRemote()) and resubmit; the retry case read in source, not run.
  Touches: `src/rclone_rc_engine.cpp`, `src/rc_job_widget.cpp`, `tests/rc_route_test.cpp`
  Acceptance: a retry reads config/dump again and runs on core/command when a section gained a `global.` option, with a real-rcd row that edits rclone.conf between tries. For a remote daemon, its settings offer a switch (off by default, with the reason beside it) that runs its transfers as separate rclone processes, unless a measured way to read the host's environment over rc turns up first.
  Complexity: M

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
