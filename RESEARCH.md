# Research — RcloneBrowserNG
Date: 2026-09-04 — replaces all prior research (previous pass 2026-06-26).

## Executive Summary

RcloneBrowserNG is a Qt 6 / C++17 desktop GUI that drives the `rclone` CLI. Measured against its lineage it is the most feature-complete member: three years of open requests on `kapitainsky/RcloneBrowser` and `mmozeiko/RcloneBrowser` have been implemented (bisync, scheduling, dual pane, queue, dedupe, serve, trash, tiles, search, compare, job history, staged transfers, DPAPI secret protection, mount presets). The problem is no longer feature coverage. It is that the delivery chain is broken at both ends.

At the UI end, four shipped features are unreachable in the running application because the code that inserts their controls depends on a `qobject_cast` to a layout class the `.ui` file does not use. This is the same defect class as issue #13, which was fixed only inside `preferences_dialog.cpp`; the other twelve cast sites were never audited, and four of them are provably dead (verified against the `.ui` XML, see Reported Issues). One of the dead features — the bandwidth timetable editor — is something neither Mountain Duck nor Cyberduck offers.

At the distribution end, `CHANGELOG.md` records releases 2.0.0, 2.0.1 and 2.0.2, but the repository has **zero** `2.x` git tags (local or remote) and **zero** GitHub Releases (`gh api repos/SysAdminDoc/RcloneBrowserNG/releases` returns `0`). Built Windows artifacts sit unpublished in `release/`. The only install path a user has is "build from source", which is why the repo has 5 stars while `rclone-ui` has 2.2k and `rclone-manager` has 1.1k, both shipping through six package managers each.

Top opportunities, in priority order:

1. Repair the four dead runtime layout casts and generalise `preferences_layout_test` so the class cannot recur.
2. Replace `--include <name>` multi-file selection with `--files-from`. Verified locally against rclone v1.75.0: selecting `b[1].txt` downloads `b1.txt` instead, and selecting `a.txt` also pulls `sub/a.txt`.
3. Make crypt-backing-remote hiding a preference. It is currently unconditional and permanent, and it is the confirmed cause of the "only 8 of 15 remotes displayed" field report.
4. Raise the in-app rclone CVE floor from `1.74.3` (itself vulnerable) to `1.75.1`, and make the constant reviewable rather than hardcoded.
5. Tag and publish the three existing releases with checksums, plus a maintainer-owned Homebrew tap.
6. Make `parsing_regression_test` test the real parser instead of a copy of it.
7. Remove the GUI-thread blocking in `schedule_manager.cpp`, `remote_widget.cpp` and `rclone_rc_engine.cpp`.
8. Add a rotating on-disk log (three independent upstream requests, still unmet).
9. Settle the Qt branch policy before 2026-09-22, when Qt 6.11 leaves open-source support.
10. Move the RC engine off `core/command` onto native `sync/*` with `_async` and `_group`, which unblocks three items currently parked in `Roadmap_Blocked.md`.

## Product Map

- **Core workflows.** Browse remotes in tabs (`remote_widget.cpp`); build and run transfers with full flag control (`transfer_dialog.cpp`); save, schedule and replay tasks (`list_of_job_options.cpp`, `schedule_manager.cpp`); mount remotes with presets and stale-mount recovery (`mount_widget.cpp`, `mount_options.cpp`); stream media to an external player (`stream_widget.cpp`).
- **Personas.** rclone power users who want a safety rail around destructive flags; desktop backup operators running scheduled syncs; people avoiding an Electron or Tauri runtime for a file manager.
- **Platforms and distribution.** Source builds on Windows, macOS, Linux and BSD. Local release scripts in `scripts/` produce a Windows zip plus Inno Setup installer, a macOS app/DMG, and a Linux AppImage with zsync metadata. Nothing is published. No `.github/` directory exists (removed in `5f722b9`), which is deliberate repo policy.
- **Integrations and data flows.** rclone CLI (primary), `rclone rcd` over authenticated localhost HTTP (hooks-only path), OS schedulers (`schtasks` / `crontab` / `launchd` / systemd timers), Windows DPAPI for saved-task secrets, Windows Credential Manager for the config password, WinFsp / macFUSE / fuse-t / `nfsmount` for mounts, `tasks.json` + `staged.json` + job history JSON on disk.

## Competitive Landscape

**rclone-ui/rclone-ui** (Tauri + React, Apache-2.0, 2.2k stars). Ships through Flathub, Homebrew, Scoop, Chocolatey, WinGet, NPM, the Apple App Store and Google Play, for Windows x64/ARM, macOS Intel/Apple Silicon, and Linux AppImage/deb/rpm including ARM. Notable features NG lacks: inline rclone flag documentation rendered in every operation window, a command palette, Discord/Slack/Telegram notification targets, and remote control of rclone instances running on servers or in Docker. *Learn*: the distribution breadth, and the inline-docs idea (buildable from `rclone config providers` JSON and `rclone help flags`). *Avoid*: the web runtime; native Qt is this project's only structural advantage.

**Zarestia-Dev/rclone-manager** (Angular 22 + Tauri, 1.1k stars). Ships via AUR, Flathub, Homebrew, WinGet, Chocolatey and Scoop. Localised into eight languages. Has an inline file viewer, serve management for WebDAV/SFTP/HTTP/FTP, a headless web-server mode for VPS/NAS, and an Android beta. *Learn*: that shipped i18n is table stakes now; NG has QTranslator wired up and exactly two `tr()` calls in hand-written code. *Avoid*: the headless/web mode, which contradicts the native-desktop niche.

**RcloneView** (commercial, freemium, Windows/macOS/Linux/Android). Free tier: mount as a drive, client-side encryption, browsing, drag and drop. Paid tier paywalls exactly what NG gives away: visual folder comparison, scheduling, transfer monitoring and logging. Its two paid features NG does *not* have are **1:N sync** (one source fanned out to several destinations) and **multi-window connection to external rclone daemons**. *Learn*: 1:N sync is cheap to build on the existing saved-task model and is demonstrably worth money. *Avoid*: nothing in particular; this is the closest direct competitor.

**Mountain Duck 5** (proprietary, iterate GmbH, $49/seat perpetual, macOS + Windows). Its Integrated mode registers as a Windows Cloud Files (CfAPI) sync root and a macOS File Provider extension, so hydration-on-open is handled by the OS and the mount appears as a regular folder rather than a network volume. Seven-state overlay badges in Finder and Explorer. Custom Versioning stores history in a `.duckversions` folder for protocols with no native versioning. A documented nine-case conflict taxonomy with checksum-first detection and timestamp-suffix quarantine. Per-bookmark cache quotas by size and by last-access age. *Learn*: the conflict taxonomy is the model for presenting `bisync` conflicts, which NG currently exposes only as a `--conflict-resolve` combo box. *Avoid*: CfAPI/File Provider integration — it cannot be built on top of `rclone mount`, and attempting it would mean writing a sync engine. Notably, **Mountain Duck has no bandwidth control at all** (verified absent from its docs), so NG's bandwidth timetable is a real differentiator once it is reachable.

**Cyberduck 9.5.4** (GPL-3.0, Java + IKVM, 4.7k stars, donationware). Bandwidth throttling per transfer and per bookmark. Versions tab with revert, and it can toggle S3 bucket versioning from the Info panel. A pre-flight synchronise preview with per-item checkboxes and direction arrows before anything executes. Localised into 30 languages via Transifex. Ships signed RPM and DEB repositories plus `brew`/`choco` for its CLI. *Learn*: the pre-flight preview with per-item accept/reject is a better model than NG's dry-run text diff, and it is the right home for a sync-deletion guard. *Avoid*: no continuous sync engine — Cyberduck's own docs redirect that ask to cron, which is the same position NG should hold.

**ExpanDrive 7** (proprietary, acquired by Files.com January 2024, **relaunched free for personal use April 2025**, current build 2026.08.25.883). macOS via Apple's File Provider extension with no kext, Windows via its own signed kernel filesystem driver with real drive letters and per-user mount isolation, Linux via system FUSE. A web management console pushes a shared connection set to a fleet and can lock users out of adding or removing drives. Server Edition adds headless mounting, Office file locking and SMB re-share. *Learn*: the price floor for "mount cloud storage as a drive" is now zero, so a mount manager is not by itself a reason to install anything. *Avoid*: nothing to copy directly; the fleet console and kernel driver are not reachable from a CLI wrapper. Worth noting it has **no bandwidth control and no conflict-resolution UI**, and states outright that it does no client-side encryption.

**CloudMounter 4.18 / 3.8** (Electronic Team, macOS $74 lifetime or $29/yr, Windows $29.99 one-time, also on Setapp). Its differentiator is AES-256 client-side encryption with optional filename encryption and Finder/Explorer context menus. macOS offers three selectable mount types (native File Provider, macFUSE, network drive) with only the native one supporting offline access; Windows uses Dokan and still has no offline mode. Finder and Explorer status-icon overlays on both. *Learn*: giving the user an explicit mount-backend choice with a stated tradeoff per option is a good pattern for the macOS macFUSE / fuse-t / nfsmount split this app already detects. *Avoid*: its encryption is a proprietary app-locked format with unrecoverable passwords; rclone `crypt` is the better answer and the app already exposes it. Like ExpanDrive: **no bandwidth throttling, no versioning UI, no conflict UI**.

**rclone built-in GUI** (`rclone gui`, added in v1.74, plus `rclone rcd --rc-web-gui`). The upstream now ships its own embedded web interface with a dark-mode file browser. *Learn*: this closes the "web UI" lane permanently; NG should not compete there. *Avoid*: any web front end.

**FreeFileSync / Grsync / luckyBackup** (local sync GUIs). The pattern worth taking is the explicit pre-run summary that states how many files will be created, updated and **deleted**, with the deletion count called out, before the run starts. NG's `Sync` mode deletes destination files with no summary and no extra friction beyond an optional dry run.

## Reported Issues

The repository has issues enabled, one closed issue (#13), no open issues, and no discussions. The real user signal is elsewhere: in the body of closed PR #15, and in the two abandoned upstream trackers this fork inherits.

**Open bugs worth fixing.**

- **Four dynamically created controls are never inserted into any layout.** Verified by resolving each widget's parent layout class from the `.ui` XML and comparing it to the cast target. `transfer_dialog.cpp:313` casts `modeGroup`'s layout to `QHBoxLayout`, but it is a `QGridLayout` — the **Bisync radio button** (`mRbBisync`) is never inserted, yet `mRbBisync->isChecked()` is read at `transfer_dialog.cpp:941`. `transfer_dialog.cpp:47` and `transfer_dialog.cpp:166` cast `tab3`'s layout to `QFormLayout`, but it is a `QGridLayout` — the **bandwidth timetable editor button** and the **performance preset combo** are never inserted. `remote_widget.cpp:427` casts `buttons`' layout to `QHBoxLayout`, but it is a `QGridLayout` — the **Google Drive trash / cleanup button** is never inserted. Each widget is constructed with the dialog as parent, so it exists and is reachable by `findChildren`, which is why `accessibility_smoke_test.cpp` passes over all of them. Same root cause as #13; only `preferences_dialog.cpp` was repaired. The remaining eight cast sites (`main_window.cpp:304,1163,1180,1438,1835`, `remote_widget.cpp:378,650`, `transfer_dialog.cpp:243`) do resolve correctly today. Confidence: **Verified** that each of the four casts evaluates to null, since `QGridLayout` derives from neither `QFormLayout` nor `QHBoxLayout`; **needs live validation** for exactly how each orphaned widget renders (issue #13's screenshot showed the same class of widget painting over the tab bar).
- **Multi-file download transfers the wrong files.** `remote_widget.cpp:1441` builds one `--include <filename>` pair per selected row. rclone filter patterns are globs and are not anchored. Reproduced against rclone v1.75.0 on a local path (filtering happens in rclone's filter layer, above the backend, so this is backend-independent): with `a.txt`, `sub/a.txt`, `b1.txt` and `b[1].txt` present, `--include "a.txt"` matched both `a.txt` and `sub/a.txt`, and `--include "b[1].txt"` matched `b1.txt` and **not** the selected file. `--files-from` matched exactly the named path. This is upstream `kapitainsky#130` ("`[ ] ( )` and others is not supported"), still unfixed. It also puts two arguments per selected file on one command line, which will hit the Windows 32,767-character `CreateProcess` limit on a large selection.
- **Crypt-backing remotes are hidden permanently with no way to show them.** `main_window.cpp:2905-2937` runs `rclone config dump` asynchronously after the list is populated, collects the backing remote of every `crypt` entry, and calls `setHidden(true)` plus `setData(Qt::UserRole + 1, true)`. The filter handler at `main_window.cpp:242` explicitly skips those items, so they can never be revealed. No preference exists. This is the mechanism that produces the symptom in the PR #15 field report ("Only 8 of 15 remotes are displayed in the remote tab; when pressing refresh all 15 remotes flash up for a second"): the flash is the synchronous list population, the disappearance is the async `config dump` callback arriving afterwards. Verified against a local config that a `crypt` remote over `localdisk:` yields `cryptBackends = {localdisk}` and hides it. Confidence: **Verified** for the code path and the flash-then-hide behaviour; **Likely** that this specific reporter's seven missing remotes were all crypt backends, which their config would confirm. The feature came from upstream `kapitainsky#178` and `#206`, both of which asked for it as an **option**.
- **The in-app rclone CVE floor is set to a vulnerable version.** `main_window.cpp:2025` compares against the literal `"1.74.3"`. GHSA-fqj9-69pf-6pjg (high) is patched in 1.74.4, so 1.74.3 itself is affected. Since then rclone published eleven further advisories on 2026-09-04, including two critical (GHSA-xwwr-4h3p-r22c, GHSA-p569-5gjg-9cmj) and three high, all patched in 1.75.1. GHSA-mfvx-7rcj-9m5g (patched 1.75.0) is directly relevant: the pprof debug handler bypasses the rc auth rule and exposes `/debug/pprof/cmdline`, i.e. the daemon's full argv including `--rc-user` and `--rc-pass`, which is exactly how `rclone_rc_engine.cpp` and the Windows mount path start rclone.
- **`parsing_regression_test` cannot detect a parser regression.** `CMakeLists.txt:127-133` builds the target from `tests/parsing_regression_test.cpp` and its header only, with no `src/` source. The test reimplements the balanced-brace `lsjson` scanner rather than exercising `ItemModel::load`'s `StreamParser`. Mutating `src/item_model.cpp` leaves it green.
- **GUI-thread blocking on user actions.** Corrected 2026-09-05 after checking the call graph rather than the call sites. Of the three places originally listed, only one was a real freeze, and it is fixed: `rclone_rc_engine.cpp` called `waitForStarted(5000)` and then spun on `QThread::msleep(50)` on the click that starts an RC-backed transfer, with no progress and nothing to cancel. The seventeen `waitForFinished` calls in `schedule_manager.cpp` are worker-thread implementations reached only through `installScheduleAsync`/`removeScheduleAsync`/`listSchedulesAsync`, which all three GUI call sites use; they never run on the window thread. The nested `QEventLoop` at `remote_widget.cpp:211` is window-modal with a working Cancel and a 30-second timeout, so the window stays responsive; it is tracked as its own narrower roadmap item rather than treated as a freeze.
- **Silent failure paths.** Post-transfer shell hooks run via `QProcess::startDetached` at `main_window.cpp:3270`, `3273`, `3737` and `3740`, and unmount runs via `startDetached` at `mount_widget.cpp:370` and `392`. None captures stderr, checks an exit code, or reports anything. A broken user hook or a failed `fusermount -u` produces no feedback at all.
- **Latent memory-safety issues.** `item_model.cpp:8-16` (`advanceSpinner`) computes `(int)((size_t)text.length() - 2)` with no length guard and indexes a four-element array with an index that can reach 5 if the character is not found. Not reachable from the current single call site, but unguarded. `job_widget.cpp:225` computes a percentage with no clamp, so a backend reporting `bytes > totalBytes` during a retry renders above 100. `remote_widget.cpp:1906-1916` constructs an unused second `QProcess` for every `serve` invocation and never deletes it.

**Feature requests with real demand.** Ranked by how many independent trackers carry them.

- A persistent, rotating error log file: `kapitainsky#134`, `kapitainsky#233`, `mmozeiko#148`. Three separate asks. The app has an in-session error panel and a redacted support bundle, but `qInstallMessageHandler` appears nowhere and nothing is written to disk, so an overnight scheduled run that fails leaves no trace.
- Mount as a Windows network drive with a chosen letter and volume label: `kapitainsky#210`. `--network-mode` appears only as placeholder text in `preferences_dialog.cpp:65` and `remote_widget.cpp:1243`; there is no control, and `--volname` is absent entirely.
- Multi-language support: `kapitainsky#138`, `kapitainsky#47`, `mmozeiko` localisation demand. Both live competitors ship it.
- In-app self-update: `kapitainsky#249`. NG checks GitHub for a newer version and opens the browser; `rclone selfupdate` is wired for rclone but nothing updates the app.
- Mount listing cache pre-warming: `mmozeiko#132`. The `vfs/refresh` rc endpoint exists and is unused.
- Filter semantics are not understood by users: `kapitainsky#252`, where a reporter combined a misunderstood exclude path with "delete excluded" and lost already-uploaded data. NG has a visual exclude builder but no explanation of anchoring and no warning on `--delete-excluded`.

**Reports judged stale, already fixed, or not worth acting on.**

- PR #15's first observation (issue #13 still reproducing) is not actionable as reported: the fix `5f19902` landed 2026-07-29 and the PR was opened 2026-08-05 from a fork whose base is unclear. The generalised layout audit above supersedes it either way.
- PR #15's third observation (`tasks.bin` not readable by older rclone-browser) is by design: `list_of_job_options.cpp` reads `tasks.json` first, falls back to `tasks.bin`, and thereafter writes only `tasks.json`. The legacy file is left in place, so downgrading loses only tasks created after the upgrade. Worth a one-line note in the README, not code.
- `mmozeiko#77` (export encoded in OS codec, not UTF-8) is resolved by Qt 6: `QTextStream` in `export_list_writer.cpp:76` defaults to UTF-8.
- `kapitainsky#94` (keyboard shortcuts, 22 comments) and `kapitainsky#144` ("are you sure" before Run) are both refused by standing project policy: shortcuts were deliberately removed in the 2026-06-15 pass, and confirmation dialogs are prohibited. Neither should be re-proposed.
- Build-failure issues across both upstreams (`#207`, `#219`, `#223`, `#248`, `mmozeiko#33`, `#96`) target Qt 5 era code that no longer exists after `bea1178`.
- PR #15 itself (add GitHub Actions CI) was correctly closed: the repository has a standing no-CI policy and `.github/` was removed on purpose in `5f722b9`.

## Security, Privacy, and Reliability

- The rclone version gate is hardcoded and wrong (`src/main_window.cpp:2025`, floor `1.74.3`); current safe floor is 1.75.1. Detail above.
- The Qt CVE gate runs on Windows only. `scripts/release_windows.cmd:67` invokes `scripts/validate_qt_version.ps1`; `scripts/release_AppImage.sh` and `scripts/release_macOS.sh` contain no Qt version check at all, so a Linux or macOS artifact can ship a QtSvg with CVE-2026-6210 unchecked. The gate also cites only CVE-2026-6210, while CVE-2026-9499 (`QTextCodec::codecForName` out-of-bounds read, affecting 4.0.0-6.8.7 and 6.9.0-6.11.0) has the same 6.8.8+/6.11.1+ fix boundary and is not mentioned.
- **Qt 6.11 leaves open-source support on 2026-09-22, and Qt 6.12 LTS is targeted for the same date** (Beta 1 shipped June 2026). Qt 6.10 already ended 2026-04-07 and 6.9 on 2025-10-07. Qt 6.8 is LTS with commercial support to 2029-10-08, but its open-source window closed 2025-04-02 and the highest publicly listed 6.8 patch is 6.8.3, while the CVE-2026-6210 fix requires 6.8.8. Reading those together: **6.8.8 is likely commercial-only, so 6.11.2 is currently the only patched Qt branch an open-source build can obtain** — which is what the maintainer already has installed, and which stops receiving patches in eighteen days. The practical plan is to stay on 6.11.2 now and move to 6.12 LTS as soon as it ships. Confidence: Verified for the dates, Likely for the 6.8.8 availability claim; needs live validation against download.qt.io.
- `validate_qt_version.ps1` accepts `6.11.1+` with no upper bound and has no notion of a branch going out of support. `CMakeLists.txt:10` declares no minimum Qt version at all, so a Qt 6.0 toolchain configures and then fails obscurely, despite the README badge claiming 6.4+. Distro reality sets the practical floor for source builds: Debian 13 trixie ships 6.8.2, Ubuntu 26.04 LTS ships 6.10.2, but Ubuntu 24.04 LTS is still on 6.4.2.
- Post-transfer shell hooks are trust-gated before execution but run detached with no captured exit status (`main_window.cpp:3270-3273`, `3737-3740`), so a hook that fails silently is indistinguishable from one that succeeded, including in job history.
- `utils.cpp:7-9` holds `gRclone`, `gRcloneConf` and `gRclonePassword` as unsynchronised file-static globals read from many call sites. Safe today only because every caller is on the GUI thread; nothing enforces that.
- MSVC builds compile with `/W4 /WX` but blanket-suppress `/wd4100 /wd4189 /wd4996` (unused parameter, unused local, deprecated API), while GCC/Clang use `-Wall -Wextra -Werror -Wno-deprecated-declarations`. Unused locals are fatal on Linux and invisible on Windows, so the two legs of the build disagree about what a warning is.
- No sanitizer configuration exists anywhere in the build (`-fsanitize` appears in neither `CMakeLists.txt` nor `src/CMakeLists.txt`), and the project has no fuzzing despite three text/JSON parsers on untrusted-ish input.
- No crash log and no persistent log file. `qInstallMessageHandler` is not used.
- Two hardening decisions to keep, not revisit: the app never enables `--rc-web-gui` (which would make rclone download a third-party bundle from GitHub at runtime), and it starts rcd with per-session random credentials on loopback rather than `--rc-no-auth`. The rc CVE cluster of 2026 (CVE-2026-41176, CVE-2026-41179, CVE-2026-49980, all CVSS 9.8, plus GHSA-p569-5gjg-9cmj at 9.1) is entirely against the rcd HTTP server; `librclone`, the cgo shared library, has none, and is the safer long-term integration path if the RC surface ever grows.
- `QProcess::terminate()` posts `WM_CLOSE` and cannot reach a console child on Windows, so every cancel path (`job_widget.cpp:538`, `mount_widget.cpp:399`, `remote_widget.cpp:1927`, `stream_widget.cpp:118-119`) waits out its full 5-second fallback before `kill()` does the actual work. On POSIX the `SIGTERM` is real and the wait is worth having.
- EU Cyber Resilience Act vulnerability-reporting obligations begin 2026-09-11. The Article 64(10) open-source steward carve-out means no CE marking, no conformity assessment and no fines for an unpaid project; what remains is a stated security policy and reporting of actively exploited vulnerabilities. `SECURITY.md` already satisfies the first.
- Recovery gaps: `staged.json` and `tasks.json` are written atomically via `QSaveFile` and a corrupt `tasks.bin` is quarantined with a `.corrupt` suffix, but there is no equivalent guard for `tasks.json` beyond fail-closed parsing, and no user-visible "restore previous tasks file" action.

## Architecture Assessment

- `src/main_window.cpp` is 4,947 lines and remains the orchestration hub for remotes, tasks, jobs, mounts, RC, scheduling, history, health and update checks. The cheapest useful split is the remotes-list lifecycle (`rcloneListRemotes` and the crypt-hide callback, roughly lines 2787-3000) into a `RemotesListController`, because that is where the crypt-hide and `listremotes` parsing fixes land anyway.
- `src/remote_widget.cpp` is 2,682 lines; the selection-to-arguments logic (download at ~1410-1460, upload, delete, move) is the natural extraction into a testable `SelectionArguments` helper, which is also the fix site for the `--files-from` change.
- The RC engine is real but shallow. `rclone_rc_engine.cpp` uses only `rc/noopauth`, `core/command`, `job/status`, `core/stats`, `job/stop` and `core/quit`, and it is entered only when a saved task carries a heartbeat, webhook or post-command hook (`main_window.cpp:3252-3256`). Because transfers go through `core/command` rather than `sync/copy` with `_async` and `_group`, per-job stats groups are unavailable, which is precisely what `core/bwlimit`, graceful stop and `job/batch` need. Three items in `Roadmap_Blocked.md` are filed as "depends on rcd engine (not yet implemented)" — the engine *is* implemented, so their real blocker is the `core/command` shim, and they are actionable.
- `main_window.cpp:2802-2822` parses `rclone listremotes --long` with `line.split(':')` and requires exactly two parts. rclone v1.75.0 emits `name: type description` when a remote has a `description` set, so `parts[1]` becomes `"alias My main disk: backup"`, the part count can exceed two, and the remote is either skipped or given a garbage type (which fails the `:/remotes/images/<type>.png` lookup and falls back to `unknown.png`). `rclone listremotes --json` returns `{name,type,source,description}` and removes the whole class of problem. This is the likely residue of `mmozeiko#83`.
- Test gaps. Twenty-five `src/*.cpp` files have no dedicated test. `main.cpp`, `pch.cpp` and `rclone_password_helper.cpp` appear in no test binary at all. `accessibility_smoke_test` links most of the UI but asserts only accessible names, focus chains and non-zero geometry, which is why it walks straight past four widgets that are not in any layout. `preferences_layout_test` has the right assertion (`verifyManaged`, lines 40-51) and it is applied to exactly one dialog.
- `CMakeLists.txt` requires the Qt Test component unconditionally at line 11, even when `BUILD_TESTING` is off, so a Qt install without QtTest cannot configure the application.
- The New Remote flow stops one step short of being a real wizard. `main_window.cpp:2330-2334` already fetches `config/providers` through `rclone rc --loopback`, and `showCreateRemoteDialog` uses it for a searchable provider list with descriptions, but then tells the user it "will open rclone's interactive setup in a terminal". The JSON it already has carries everything needed to build the form: `fs.Option` marshals `Type`, `DefaultStr` and `ValueStr` alongside `Required`, `Advanced`, `Exclusive`, `Sensitive`, `IsPassword` and `Examples`, and `MatchProvider` semantics (comma-separated list, leading `!` negates, blank matches all) drive conditional visibility for provider variants such as the S3 family. Backends needing OAuth or multi-step setup are not in that JSON (`Config` is `json:"-"`), but they are drivable through `rclone config create --non-interactive` followed by `rclone config update --continue --state <s> --result <r>` until the returned state is empty; rclone ships a reference implementation of that loop in `bin/config.py`.
- The transfer dialog's flag coverage stops at filtering, retries and timing. `src/job_options.cpp` builds arguments from a fixed set containing no `--track-renames` (with `--track-renames-strategy`), `--check-first`, `--order-by`, `--max-duration` with `--cutoff-mode`, `--max-transfer`, `--partial-suffix`, `--inplace`, `--metadata`, `--multi-thread-streams`/`--multi-thread-cutoff`, `--compare-dest`/`--copy-dest`, `--no-traverse` or `--server-side-across-configs`. The last is the biggest single omission for this app specifically, because it already offers remote-to-remote transfers and currently streams every byte through the local machine even when both ends are the same provider.
- Nothing maps rclone's exit codes. `src/job_widget.cpp` stores `mExitCode` and a boolean `mSuccess`, but rclone reserves 8 for `--max-transfer` reached, 9 for no files transferred under `--error-on-no-transfer`, and 10 for `--max-duration` reached, none of which are failures; 3, 4 and 5 (directory not found, file not found, temporary error) are retryable and distinguishable. The v1.69.0 changelog also moved usage errors from 1 to 2 and bisync critical abort from 2 to 7, which disagrees with the current docs table, so any mapping must be checked against the pinned rclone rather than the documentation alone.
- Two modernisation items are already done and should not be re-proposed: no deprecated high-DPI attributes are set anywhere in `src/`, and `main_window.cpp:187` already reads `QGuiApplication::styleHints()->colorScheme()` rather than probing the registry.
- i18n is scaffolding only: `main.cpp:43-54` loads a `QTranslator`, `src/CMakeLists.txt:31` finds `LinguistTools` as an optional component, and there are two `tr()` calls in hand-written code (`main_window.cpp:2709-2710`) and no `.ts` files, no `lupdate`/`lrelease` target and no `translations/` directory. `Roadmap_Blocked.md` blocks this on Weblate enrolment, but the `tr()` retrofit and the English template generation need no external service.
- The CLI accepts `--version`, `--run-task`, `--list-tasks`, `--send-to`, `--minimized` and `--tray` but has no `--help` and silently ignores unrecognised arguments (`main.cpp:56-350`).

## Rejected Ideas

- **OS-native placeholder files (Windows CfAPI sync root / macOS File Provider).** Source: Mountain Duck 5. Requires owning the sync engine and hydration protocol; unreachable on top of `rclone mount`.
- **Cryptomator vault interoperability.** Source: Cyberduck/Mountain Duck. rclone `crypt` is a different, non-interoperable format; reimplementing Cryptomator's vault format is a project in itself.
- **Web UI, headless server mode, Docker image.** Source: `Roadmap_Blocked.md` P5, rclone-manager headless mode. rclone shipped its own embedded GUI in v1.74; this lane is closed and it contradicts the native-desktop niche.
- **Continuous background sync daemon.** Source: RcloneView, Mountain Duck Smart Sync. Cyberduck's own docs redirect this to cron, and NG already has native OS scheduling; a daemon means owning conflict semantics rather than delegating to `bisync`.
- **Keyboard shortcuts.** Source: `kapitainsky#94` (22 comments). Standing project policy prohibits them; they were removed deliberately in the 2026-06-15 pass.
- **Confirmation dialog before running a saved task.** Source: `kapitainsky#144`. Standing project policy prohibits confirmation dialogs. The sync-deletion preview below is the compliant alternative because it is information, not a permission prompt.
- **Plugin system / extension marketplace.** rclone owns backend extensibility; a GUI plugin ABI adds maintenance and attack surface with no observed demand in either tracker.
- **A mobile companion.** Source: RcloneView (Android, iOS announced), rclone-manager (Android beta). This is a Qt Widgets desktop application; a touch UI would be a second product, and Round-Sync already serves Android rclone users.
- **Fleet management: a web console, pushed configuration profiles, or device policy locks.** Source: ExpanDrive's Web Management Console. It is the clearest capability gap against the commercial field and it is deliberately not worth chasing — it presumes a licensing server and an administrator persona that a single-user desktop tool does not have.
- **Client-side encryption of the app's own, or Cryptomator's, format.** Source: CloudMounter AES-256 with filename encryption. rclone `crypt` already does this with an open, documented, recoverable format and the app already browses crypt remotes; adding a second scheme would be strictly worse.
- **Rewrite in Tauri, Electron or Rust.** rclone-ui and rclone-manager already occupy that stack with more stars and better distribution; the native Qt build is the only axis on which this project is distinct.
- **ppc64le and FreeBSD headless CI, Snap, Flathub submission, and code signing.** All remain blocked on runners, external accounts or paid enrolment, so they stay in `Roadmap_Blocked.md`. Three of their stated premises are now out of date and should be corrected there rather than acted on here: the Homebrew unsigned-cask deadline (2026-09-01) has passed rather than approaching; Windows signing is no longer SignPath-or-nothing, since Azure Artifact Signing is $9.99/month, FIPS 140-3, and explicitly available to individuals in the US, Canada, EU and UK, while EV certificates no longer buy instant SmartScreen reputation and certificate lifetimes are capped at 460 days from 2026-03-01; and macOS has become harder rather than easier, because macOS Sequoia removed the Control-click "Open Anyway" Gatekeeper bypass, so an unnotarized DMG now needs a trip through System Settings.
- **Flatpak as the primary Linux channel.** A FUSE mount created inside a Flatpak sandbox is not visible to the host session, so the app would have to run rclone on the host through `flatpak-spawn --host`, which is an untested configuration for rclone. AppImage, which the project already builds with zsync metadata, is the better primary channel for a tool whose main job is mounting. Flatpak stays a second channel at best, and is separately complicated by Flathub's reported policy on AI-assisted submissions.

## Sources

Repository and lineage:
- https://github.com/SysAdminDoc/RcloneBrowserNG/pull/15
- https://github.com/SysAdminDoc/RcloneBrowserNG/issues/13
- https://github.com/kapitainsky/RcloneBrowser/issues
- https://github.com/mmozeiko/RcloneBrowser/issues

Competitors:
- https://github.com/rclone-ui/rclone-ui
- https://rcloneui.com/docs/ui
- https://github.com/Zarestia-Dev/rclone-manager
- https://rcloneview.com/
- https://mountainduck.io/
- https://docs.mountainduck.io/mountainduck/connect/
- https://docs.mountainduck.io/mountainduck/connect/sync/
- https://docs.mountainduck.io/mountainduck/versions/
- https://mountainduck.io/buy/
- https://cyberduck.io/download/
- https://docs.cyberduck.io/cyberduck/transfer/
- https://docs.cyberduck.io/cyberduck/sync/
- https://github.com/iterate-ch/cyberduck
- https://www.expandrive.com/pricing/
- https://www.expandrive.com/now-free
- https://docs.expandrive.com/web-management-console
- https://cloudmounter.net/
- https://help.electronic.us/support/solutions/articles/44002428087-what-types-of-drive-mouting-are-used-in-cloudmounter-
- https://alternativeto.net/software/rclone-gui

rclone:
- https://rclone.org/changelog/
- https://rclone.org/rc/
- https://rclone.org/filtering/
- https://rclone.org/commands/rclone_listremotes/
- https://rclone.org/commands/rclone_lsjson/
- https://rclone.org/commands/rclone_config_create/
- https://rclone.org/bisync/
- https://rclone.org/docs/#exit-code
- https://raw.githubusercontent.com/rclone/rclone/master/fs/registry.go
- https://github.com/rclone/rclone/security/advisories/GHSA-p569-5gjg-9cmj
- https://github.com/rclone/rclone/security/advisories/GHSA-xwwr-4h3p-r22c
- https://github.com/rclone/rclone/security/advisories/GHSA-mfvx-7rcj-9m5g
- https://github.com/rclone/rclone/security/advisories/GHSA-fqj9-69pf-6pjg

Qt:
- https://endoflife.date/qt
- https://doc.qt.io/qt-6/qt-releases.html
- https://wiki.qt.io/Qt_6.11_Release
- https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling
- https://www.qt.io/blog/security-advisory-out-of-bounds-read-vulnerability-in-qtextcodeccodecforname
- https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products

Distribution:
- https://workbrew.com/blog/homebrew-5-0-0
- https://github.com/orgs/Homebrew/discussions/6482
- https://docs.brew.sh/Taps
- https://docs.appimage.org/packaging-guide/optional/updates.html
- https://azure.microsoft.com/en-gb/pricing/details/trusted-signing/
- https://cabforum.org/uploads/Baseline-Requirements-for-the-Issuance-and-Management-of-Code-Signing.v3.7.pdf
- https://docs.flatpak.org/en/latest/sandbox-permissions.html
- https://openssf.org/public-policy/eu-cyber-resilience-act/

## Open Questions

- Is a CVE-2026-6210-patched Qt (6.8.8 or later on the 6.8 branch) actually downloadable without a commercial licence? The public 6.8 patch list stops at 6.8.3. If it is not, 6.11.2 is the only open-source route until 6.12 LTS ships, and the release gate has to say so. This is answerable by inspecting download.qt.io and nothing else, so it should be settled before the Qt-policy roadmap item is implemented.
- Is publishing unsigned GitHub Releases acceptable, given SmartScreen and Gatekeeper warnings? If yes, the release backlog clears immediately with checksums and a maintainer-owned Homebrew tap. If not, everything stays blocked on SignPath and Apple enrolment.
- Should crypt-backing remotes default to hidden or visible when the new preference lands? Upstream `#178` and `#206` asked for hiding; PR #15's reporter was confused by it. Defaulting to visible is the safer read but reverses the shipped behaviour for existing users.
