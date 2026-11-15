# Research — RcloneBrowserNG
Date: 2026-11-15 — replaces all prior research.

## Executive Summary

Most of what the 2026-09-04 pass found is fixed in the Unreleased section of `CHANGELOG.md`: the orphaned controls, the unanchored `--include` download bug, permanent crypt-backend hiding, exit-code mapping, an on-disk log, native `sync/*` remote-control calls and a deletion preview before Sync. v2.0.2 went out as the first published release on 2026-09-05. The ground moved under it since then. rclone published 19 security advisories on 2026-10-09 and 2026-11-06, and 17 of them list 1.75.1, the version the app still treats as safe (`src/rclone_security_floor.cpp:10`), in their affected range. One of them (GHSA-w33h-3qgq-8mv8, high) is in `rclone selfupdate`, which the app offers as a one-click button for any older rclone (`src/main_window.cpp:5021-5059`). The WinFsp warning recommends "2026 Beta1 or newer", but Beta1 predates the CVE-2026-7162 fix and six later vulnerability fixes. The pinned Qt 6.11.2 is affected by three Qt Network and JSON advisories that don't affect 6.11.3 or 6.12.0. The new remote-daemon mode sends Basic auth over plain HTTP by default and can't reach a daemon with a self-signed certificate. rclone 1.76.0 also changed glob character classes and `--bwlimit` parsing in ways the filter explainer and bandwidth timetable don't follow. On features, competitors shipped safety rails NG doesn't have: a delete cap that pauses schedules (Airclone), mass-deletion protection (Alto), sleep blocking during jobs (Vorta, RcloneExplorer), an emergency stop and a per-run file report (Rclone Manager). Users in four separate trackers ask for start-at-login with auto-mount, which NG is one setting away from.

Top opportunities:

1. Raise the rclone floor to 1.76.0 and name the advisories that reach this app (serve webdav and s3, selfupdate, rc JSON). P0.
2. Stop offering `rclone selfupdate` from rclone 1.75.1 and older, where the updater trusts an unsigned hash. P0.
3. Move the WinFsp warning to 2026 Beta5 (2.2.26286), the first build carrying every published fix. P1.
4. Warn on plain-HTTP remote daemons and make a TLS daemon usable. P1.
5. Cap deletions on saved and scheduled syncs with `--max-delete`. P1.
6. Move release builds to Qt 6.12 (existing P1 item, now with two more reachable Qt advisories behind it), then patch its qtbase with the published 6.12 diffs until 6.12.1 ships. P1.
7. Start the app at login so auto-mount works after a reboot. P2.
8. Keep the computer awake while jobs run. P2.
9. Announce job start, progress and finish to screen readers. P2.
10. Show what a finished sync, copy or move changed, from rclone's `--combined` report. P2.

## Product Map

- **Core workflows.** Browse remotes in tabs or a dual pane, build transfers with full flag control and a dry-run deletion preview, save and schedule tasks through the OS scheduler (with plugged-in and unmetered conditions), mount with backend detection (WinFsp, macFUSE, fuse-t, `nfsmount`), serve a remote over http, webdav, ftp, dlna, s3 or nfs (`src/remote_widget.cpp:2006`), stream to a player, compare folders, combine remotes, and drive an rclone on another machine through `rclone rcd`.
- **Personas.** Classic Rclone Browser users moving to a maintained build (kapitainsky/RcloneBrowser has 2,966 stars and its tracker now carries a successor announcement, #253). Operators running scheduled, unattended syncs. NAS and home-server owners using remote-daemon mode. Screen-reader users, who say no current GUI serves them (Reddit 1v14e3v). Confidence: Verified for the sources, Likely for the persona split.
- **Platforms and distribution.** v2.0.2 is published with a Windows installer, a portable zip, a source archive and `SHA256SUMS` (CHANGELOG Unreleased, Distribution). Local scripts build the AppImage and a macOS DMG (the DMG on a hand-started hosted-Mac workflow that publishes nothing). A `Dockerfile` runs the desktop app on a virtual display shown in a browser on port 6080. Release builds pin Qt 6.11.2, source builds floor at Qt 6.4 (`CMakeLists.txt:17`). Nothing is code-signed (`Roadmap_Blocked.md`, P1 Code signing).
- **Integrations and data flows.** rclone CLI processes. A local `rclone rcd` on `localhost` with per-session `--rc-user`/`--rc-pass` and `--rc-job-expire-duration 1h` (`src/rclone_rc_engine.cpp:201-202`). Native `sync/copy`, `sync/move`, `sync/sync` and `sync/bisync` calls whose `_config` and `_filter` come from the daemon's own `options/info` (`src/rc_sync_request.h`). A remote `rclone rcd` over HTTP Basic auth (`src/remote_daemon.cpp`). The GitHub releases API for the rclone update check (`src/main_window.cpp:5005-5035`). OS schedulers, webhooks and heartbeats on saved tasks, VSS snapshots on Windows, Credential Manager and DPAPI for secrets.

## Competitive Landscape

**rclone/rclone-ui** (TypeScript, Apache-2.0, 2,408 stars, v4.0.0-beta.3 on 2026-11-11). Does well: reach, and an S3-first pitch ("The cross-platform GUI for rclone & S3"). Its tracker shows what a webview shell costs: #251 "failed to create webview" on Windows 11, #137 no window or tray on GNOME, #247 a tiny UI on HiDPI, #91 Settings too tall at 150%. Two schedule bugs sit beside them: #113 (sync schedules never run) and #141 (schedule saving doesn't work). Learn: schedules are where GUIs in this space break first, and NG fixed three scheduled-run bugs of its own in Unreleased. Avoid: the paid Lifetime License its v3 announcement promotes (forum 52568).

**Zarestia-Dev/rclone-manager** (TypeScript, GPL-3.0, 1,230 stars, v0.3.6 on 2026-11-13). Does well: v0.3.6 added a Job Monitor with a per-run File Report (new, different, missing, matching, failed), resolution of Check and BiSync conflicts, and an RC Console. v0.3.3 added power management and an "Emergency Stop" that "immediately unmounts all remotes and halts all active serves". Learn: the file report and the emergency stop both map onto NG items below. Avoid: the visual workflow canvas and raw RC console, and the WebKitGTK dependency behind #316. A forum report of an all-black window on Zorin OS 18.1 Core (forum 53736) gives no cause. #312 (overlapping workflows stop transfers) shows the canvas has its own failure class.

**RcloneView** (commercial). Does well: a polished mount and scheduling product whose pricing page lists "Job Scheduling" and "Auto re-mount" as features. A Hacker News commenter called "$9.90/year" poor value "for auto-remount on login" and added that "RcloneBrowser does everything the free RcloneView does" (HN 46075442). The Mt. Sync author "got close to buying rcloneview for its paywalled features" (forum 54072). Learn: mounting at login is something people are asked to pay for, and NG has auto-mount on launch but no login entry. Avoid: paywalling basics.

**Airclone** (GigaionLLC/Airclone, Dart, AGPL-3.0, 57 stars, v0.31.2 on 2026-11-08). A desktop and mobile rclone interface. Its README: "A repeating Sync always carries a delete cap, and tripping it pauses every schedule until you look." Learn: a scheduled sync needs a deletion ceiling, because nobody is watching the preview. Avoid: the mobile scope.

**Alto** (hwittenborn/alto, TypeScript, GPL-3.0, 1,631 stars, v0.12.0 on 2026-11-14). A cloud storage client. v0.9.0 added "Confirmation before propagating mass deletions, and deleted local files go to the trash", conflict resolution and an `.altoignore` file. Learn: mass-deletion protection is now expected of a sync tool. Avoid: the confirmation-dialog form, which NG's no-dialog policy rules out. NG's version is a cap with an inline result.

**RcloneExplorer** (Jackpison/RcloneExplorer, C++, MIT, 29 stars, v5.4.3 on 2026-11-14). A native, portable rclone GUI, so it competes on NG's own axis. Its 5.4.3 notes say "the computer stays awake during transfers through the desktop portal". Learn: sleep blocking during transfers. Avoid: nothing specific to copy beyond that.

**Vorta** (borgbase/vorta, Python, GPL-3.0, 2,530 stars, v0.12.2 on 2026-11-15). A desktop GUI over a CLI engine (Borg), the same shape as NG. v0.12.0 shipped "Block system sleep while a backup is running (#2410)", a Jobs page, and "Show the actual Borg error in the status bar instead of the full traceback (#2572)". Learn: sleep blocking is a default in a mature tool of this shape. Avoid: Borg's snapshot-repository model, which rclone doesn't have.

**rclone's own GUI** (`rclone gui`, rclone/rclone-web 1.2.1 on 2026-11-03). rclone 1.76.0 took the GUI "out of beta" and deprecated `--rc-web-gui`. Users still trip over the old one (Reddit 1x0ox50) or don't know it exists ("I had no idea rclone had a gui", forum 53911). Learn: the upstream web GUI is now the default answer for "rclone with a GUI", so NG's case rests on what a browser tab can't do: mounts at login, OS schedules, tray, sleep control, screen-reader support. Avoid: becoming a web UI.

**The classic Rclone Browser lineage.** kapitainsky/RcloneBrowser (C++, 2,966 stars) is unmaintained. Its tracker asks how to autostart mounts (#250, 2026-04-19) and for auto-update (#249, 2026-03-29). #253 (2026-10-20) announces Rclone Browser Next (jason840709/RcloneBrowserNext, Swift, 2 stars, v0.11.0 on 2026-11-14). Alkl58/RcloneBrowser (128 stars) last made a stable release, "Rclone Browser 2.0.0 Beta", on 2023-03-17. It now publishes autobuild prereleases per commit (the latest on 2026-08-11), its Qt 6 port landed on 2026-07-05 among commits labeled "(AI)", and it fixed its #21 on 2026-08-10. The rclone 1.75.2 release thread lists `kapitainsky.RcloneBrowser` 1.8.0 in winget beside RClone Manager 0.3.3 and Rclone UI 3.7.3 (forum 54338). Learn: this is the largest pool of users NG can serve, and they keep asking for autostart and updates. Alkl58 #21 (the Checks field stays empty once rclone 1.74.4 adds a Listed count) doesn't affect NG, which reads `totalChecks` (`src/job_stats.cpp:32`). Avoid: sharing settings with the classic app, which NG #17 hit and Unreleased fixes.

**Smaller rclone GUIs from 2026.** Monti (forum 54504, Linux, MIT, drives `rclone rcd`, written after the author's "VFS cache that had grown to 40 GB"). Mt. Sync (forum 54072, GNOME). RcloneShuttle (pieterdd, Rust, GTK4, 157 stars). LazyMount-Mac (yuanweize, 143 stars, v2.4.0 on 2026-09-23, "launchd supervision, and network state recovery"). UltraExplorer (Sudo-Rahman, v1.4.2 on 2026-11-15). rem (liriliri, 657 stars, v1.5.0 on 2026-10-08). A "New Windows rclone GUI" post on r/windowsapps (1xcqkmo). Learn: cache size visibility (Monti) and mount supervision after network changes (LazyMount). Avoid: chasing each newcomer's feature list.

**OpenCfDrive** (forum 54483). Presents an rclone remote as a files-on-demand drive through the Windows Cloud Files API. Learn: there's demand for placeholder files. Avoid: building it, see Rejected Ideas.

**dataraven** (HN 48096464). A managed rclone service selling notifications, logs and an audit trail. Learn: those are the parts people pay for when they can't watch a job. NG has webhooks, heartbeats and job history already. Avoid: a hosted service.

## Reported Issues

**This repository** (checked with `gh` on 2026-11-15). Four open issues, each with a fix in CHANGELOG Unreleased and a maintainer reply on 2026-11-14.

- #16 "Not all remotes are shown" (2026-09-05). hrpv, who saw the same bug, confirmed on 2026-11-15 that they have crypt remotes on pCloud and Google Drive, which matches the cause found in the last pass: crypt-backing remotes were hidden with no way back. Hiding is opt-in in Unreleased. Verified.
- #17 "Conflicts between RcloneBrowserNG and classic RCloneBrowser Portable" (2026-09-05). NG now keeps its own `rclone-browser-ng` settings and data and migrates the old ones. Verified in CHANGELOG.
- #18 "Mounting with combined backends?" (2026-09-11). Combine remotes shipped in Unreleased.
- #20 "can not list local on right pane" (2026-10-18). Fixed by using rclone's `:local` backend for the dual pane's local side.
- PR #19 by giflw (merged 2026-11-14) fixed builds that treat warnings as errors, which backs the existing P3 MSVC/GCC warning-policy item. PR #14 by Dwgthomson (merged 2026-08-24) made start-minimized depend on always-show-in-tray, which the start-at-login item has to respect.

None of these fixes reach users until a release after v2.0.2.

**Demand from other trackers and communities.**

- *Start at login, mount at login.* kapitainsky #250, Reddit 1wayift ("mount my Mega on startup", 2026-09-08), rclone #9245 ("Autostart rclone minimised to tray", 2026-03-09), Zarestia #91 (start minimised), HN 46075442. NG has an "Auto-mount on launch" setting (`src/main_window.cpp:350-363`) and accepts `--minimized` (`src/main.cpp:449-453`), but nothing registers it to start at login. LaunchAgents appear only for schedules (`src/schedule_manager.cpp:459`). Verified.
- *Unattended failures nobody sees.* rclone #9899 (2026-09-10): a mount retrying forever with no alert when its OAuth token has expired. Forum 54134 asks for backup monitoring and alerts. Forum 54283 proposes Telegram notifications, and the rclone maintainer prefers a general hook mechanism.
- *Deletion safety.* Bisync Hub (forum 54406) builds a dashboard around `TOO_MANY_DELETES`. Airclone and Alto both ship deletion ceilings. NG builds no `--max-delete` anywhere (`src/job_options.cpp`). Verified.
- *What did this run change.* Forum 53728 asks for dry runs that tell a new file from an overwrite. Rclone Manager v0.3.6 ships a per-run file report.
- *Accessibility.* Reddit 1v14e3v (2026-07-19): a blind user asks for a native Windows app that works with a screen reader, handles B2 with crypt and reports progress accurately.
- *Mount hangs.* Forum 54067: Windows Explorer becomes very unresponsive when a mount can't reach its remote.
- *Cache growth.* Forum 54504: a VFS cache grew to 40 GB unnoticed. Backs the existing P2 VFS cache item.
- *Already met.* HN 47402570 says users want a GUI to find an existing rclone. NG does (`QStandardPaths::findExecutable("rclone")`, `src/main_window.cpp:1936`). kapitainsky #134 asked for an error log file, which shipped as the rotating log.
- *Not actionable.* Reddit 1vh18d6 wants stats for syncs started from a terminal. A CLI rclone started without `--rc` exposes nothing to read.

**Bugs in sibling projects, checked against NG.**

- Zarestia #322: Quick Run Delete calls `operations/purge` and ignores Include filters. NG's batch delete also sends folders to `operations/purge` (`src/rc_delete_batch.cpp:134`), but NG deletes an explicit selection, where removing the whole selected folder is the intended result. No action.
- rclone #9908 "MacFUSE + FSKit + mount - Finder cannot copy files" (closed). NG picks macFUSE's FSKit-capable path on macFUSE 5.2 and later (`src/mount_backend.cpp:150-167`). Needs live validation on a Mac with macFUSE 5.5.0.

## Security, Privacy, and Reliability

- **rclone floor is two releases behind.** rclone 1.75.2 (2026-10-09) and 1.76.0 (2026-11-06) fixed 18 advisories, and 17 of them list 1.75.1 as affected. The 18th, GHSA-gpw7-77cq-9f72 (b2 token leak on redirect), lists `<= v1.75.0` but names 1.75.2 as the fix. Three are rated high: GHSA-44pm-2q5p-fm63 (serve webdav `MKCOL` with `..` writes above the served root without authentication), GHSA-w33h-3qgq-8mv8 (selfupdate accepts an archive hash from an unsigned clearsign prefix, `>=1.55.0, <=1.75.1`) and GHSA-66qv-66mg-9h74 (serve sftp auth-proxy key replay). The app's Serve menu runs `serve webdav` and `serve s3` (`src/remote_widget.cpp:2006-2020`), which reach GHSA-44pm, GHSA-gj73-fh6v-92fj (webdav skips auth for `OPTIONS`), GHSA-w3h7-jxxq-vfrp and GHSA-qjw8-8hmp-c3m6 (serve s3). Remote-daemon mode exposes an rcd to the network, which reaches GHSA-f8p4-8g4j-q3m2 (oversized RC JSON exhausts memory before authorization). The Serve menu has no sftp, so GHSA-66qv doesn't reach it. A 19th advisory published the same day, GHSA-xqmq-v4wj-rw5m, was fixed in 1.74.2 and is already below the floor. Verified against each advisory's own range.
- **One-click selfupdate runs the vulnerable updater.** The update prompt offers "Run selfupdate" for any older rclone (`src/main_window.cpp:5030`). On 1.75.1 and older, the binary doing the update is the one GHSA-w33h describes, so the safe path for those versions is a manual download. Verified.
- **WinFsp warning names the wrong fixed build.** `src/main_window.cpp:5165-5178` warns only at 2.1.25156 and older and recommends "2026 Beta1 or newer". Beta1 (2.2.26112, 2026-04-22) lists no CVE-2026-7162 fix. Beta2 (2.2.26183, 2026-07-02) is the first that does, Beta4 (2.2.26215, 2026-08-03) adds fixes for two more reported vulnerabilities, and Beta5 (2.2.26286, 2026-10-13) adds four more. The latest stable is still 2.1 ("WinFsp 2025"). Verified from the release notes.
- **Remote daemon over plain HTTP.** `NormalizeUrl` prepends `http://`, every request carries a Basic auth header, and nothing handles `sslErrors`, so a self-signed `https` daemon fails to connect. The README tells users to run `rclone rcd --rc-addr :5572`, which binds all interfaces, and suggests `http://nas.local:5572`. rclone's own docs say "Access to the rc API is equivalent to shell access as the user running rclone" and "Do not bind the rc to a network address you do not control", and recommend `--rc-cert`/`--rc-key`. `--rc-min-tls-version` defaults to tls1.0. Verified.
- **Qt.** Release builds pin 6.11.2. The Qt advisory list gives CVE-2026-95620 (QNetworkAccessManager HTTP/2 use-after-free, 6.9.0 to 6.11.2), CVE-2026-96432 (QNetworkAccessManager null dereference over HTTP/2, up to 6.11.2), both fixed in 6.11.3, and neither lists 6.12.0 as affected. The app uses QNetworkAccessManager for the GitHub API (`src/main_window.cpp:4911-5079`) and the remote daemon (`src/remote_daemon.cpp:11-14`), so both are reachable from a malicious or compromised server. Likely reachable over HTTP/2 to api.github.com. CVE-2026-101201, fixed in 6.11.3 as well, is in QBinaryJson in Qt5Compat, which the app doesn't link. CVE-2026-116337 (HTTP reply status line and headers read without a size limit) and CVE-2026-105943 (QAuthenticator NTLM on Windows without the SPN) are fixed in qtbase source diffs published for 6.12 on 2026-11-09 and 2026-11-13, and in 6.12.1 and 6.8.10, but on 2026-11-15 download.qt.io lists only 6.12.0 binaries for the 6.12 branch. CVE-2026-105924 (QCborValue to JSON) isn't reachable because the app has no QCbor code. Verified.
- **Unsigned builds.** Microsoft's Smart App Control FAQ says an unsigned app is treated as untrusted and blocked, and that recent updates let Smart App Control be turned on without a clean install. That raises the cost of the signing item already in `Roadmap_Blocked.md`. Verified for the FAQ, Likely for the effect on NG's installer.
- **Deletion with nobody watching.** The dry-run deletion preview only helps when someone is at the screen. A scheduled sync whose source comes back empty (an unmounted drive, an expired token) deletes the destination with no ceiling. rclone's `--max-delete` stops the run with a fatal error (exit code 7) once the count is passed. rclone 1.76.0 fixed "rc ignoring global `--dry-run` and `--max-delete` settings", so the native rc route needs its own test on older daemons. Verified for the flag and the changelog, needs live validation for per-call `_config` on 1.75.x.
- **Sleep.** No sleep inhibitor exists in `src/` (no `SetThreadExecutionState`, IOKit power assertion or inhibit call). A laptop that sleeps mid-sync stops the job, and a schedule that fires during sleep doesn't run. Verified for the code, Likely for the effect.
- **macOS mount backends.** The fuse-t FSKit path adds `-o backend=fskit` on macOS 26 and later whenever the user hasn't chosen a backend (`src/mount_backend.cpp:160-167`). fuse-t 1.1.0 calls FSKit support "Experimental" and says the user has to run fuse-t.app and "manually enable the FSKit extension". Needs live validation for what a mount does when the extension is off.

## Architecture Assessment

- `src/main_window.cpp` is 5,597 lines, up from 4,947 at the last pass. Security-relevant version decisions live inline there: the WinFsp threshold (5165-5178) and the selfupdate offer (5021-5059). Only the rclone floor has its own unit and test (`src/rclone_security_floor.cpp`, `tests/rclone_security_floor_test.cpp`). Moving the other two into the same kind of unit is what makes the P0 and P1 fixes testable.
- `src/rc_sync_request.cpp` builds the native call's `_config` and `_filter` from the daemon's `options/info` and refuses any flag it can't map, because rclone accepts an unknown `_config` key with HTTP 200 and ignores it (`src/rc_sync_request.h:13-20`). New CLI flags such as `--max-delete` therefore reach the native route with no hand-kept table. The 1.76.0 rc fix above is the one place where the daemon version still changes the outcome.
- Version-dependent behavior is spread over `src/rclone_security_floor.cpp`, `src/mount_backend.cpp` (macFUSE 5.2, macOS 26), the WinFsp check in `src/main_window.cpp` and per-feature checks. rclone 1.76.0 adds two more version-dependent capabilities the app could use (`operations/getfile` and report parameters on `sync/*`), and it rejects unauthorized and unknown rc calls before reading the request (the GHSA-f8p4-8g4j-q3m2 fix).
- Remote-daemon mode disables six browse actions (`src/remote_widget.cpp:1003-1010`) and ten context-menu actions (`src/remote_widget.cpp:1828-1836`). 1.76.0's `operations/getfile` returns file contents over rc (`maxSize` defaults to 2 MiB, optional `base64`), which can lift Preview for small files.
- `src/filter_pattern.cpp:89-107` explains a character class by replacing it with its first character. Under 1.76.0, `[!...]` is a negated class and `[^...]` excludes `/`, so the explainer describes both wrongly.
- The bandwidth timetable editor (`src/transfer_dialog.cpp:42-90`) accepts free text and has no test. rclone 1.76.0 rejects times without a colon or with signed digits, so a saved timetable can now fail at start.
- No code classifies a remote-daemon URL as loopback, LAN or TLS, so nothing warns and nothing tests it.
- The app reads no CBOR, which keeps CVE-2026-105924 out of reach.
- Two areas get no new item. Translation stays with the existing P2 `tr()` retrofit in ROADMAP.md and the Weblate entry in Roadmap_Blocked.md. No issue, forum topic or competitor release read in this pass asks for multi-user or shared setups. Plugins and a mobile app are under Rejected Ideas.

## Rejected Ideas

- **RC console.** Source: Rclone Manager v0.3.6. A raw rc prompt hands users an API that rclone's docs call "equivalent to shell access". NG's typed UI is the safer surface.
- **Visual workflow canvas.** Source: Rclone Manager v0.3.3, and its #312 (overlapping workflows stop transfers). Saved tasks, schedules and pre/post hooks already chain jobs.
- **Windows Cloud Files placeholders.** Source: OpenCfDrive (forum 54483). It needs a sync engine of its own and can't sit on `rclone mount`. Unchanged from the 2026-09-04 pass.
- **A mobile app.** Source: Reddit 1qunkq2 (Rclone UI "Mobile app is launching soon"), Airclone. NG is a Qt Widgets desktop app.
- **Paid tiers or licenses.** Source: RcloneView pricing, Rclone UI's lifetime license (forum 52568). The free, complete app is NG's position.
- **Telegram-specific notifications.** Source: forum 54283. The generic webhook covers any service, and the rclone maintainer prefers a general hook there too.
- **Directory listing pages.** Source: rclone 1.76.0's `index` command, Rclone Manager v0.3.6. rclone already does this from the command line, and no NG user has asked.
- **Embedding librclone.** Source: the librclone README, which calls it experimental.
- **A rewrite in Tauri, Electron or Rust.** Source: rclone-ui, Rclone Manager, RcloneShuttle. Native Qt is NG's distinct axis.
- **A web UI.** Source: rclone 1.76.0 took its own GUI out of beta. The 2026-09-04 pass grouped "Docker image" with this rejection, but the `Dockerfile` in Unreleased runs the native app on a virtual display, which is a distribution channel, not a web UI.
- **Stats for syncs started in a terminal.** Source: Reddit 1vh18d6. There's nothing to read without `--rc`.
- **Keyboard shortcuts and confirmation dialogs.** Source: Alto's mass-deletion confirmation, kapitainsky #94 and #144 from the last pass. Project policy rules out both, so deletion safety comes as a cap and an inline result.
- **Plugin system, fleet management, Cryptomator interop, a continuous sync daemon, Flatpak as the primary Linux channel.** Carried from the 2026-09-04 pass with the same reasons. No new source changes them.

## Sources

### Repository and tracker
- https://github.com/SysAdminDoc/RcloneBrowserNG/issues/16
- https://github.com/SysAdminDoc/RcloneBrowserNG/issues/17
- https://github.com/SysAdminDoc/RcloneBrowserNG/issues/18
- https://github.com/SysAdminDoc/RcloneBrowserNG/issues/20
- https://github.com/SysAdminDoc/RcloneBrowserNG/pull/19
- https://github.com/SysAdminDoc/RcloneBrowserNG/pull/14
- https://github.com/SysAdminDoc/RcloneBrowserNG/releases/tag/v2.0.2

### OSS competitors
- https://github.com/rclone/rclone-ui
- https://github.com/rclone/rclone-ui/issues/251
- https://github.com/rclone/rclone-ui/issues/137
- https://github.com/rclone/rclone-ui/issues/247
- https://github.com/rclone/rclone-ui/issues/91
- https://github.com/rclone/rclone-ui/issues/113
- https://github.com/rclone/rclone-ui/issues/141
- https://github.com/Zarestia-Dev/rclone-manager
- https://github.com/Zarestia-Dev/rclone-manager/releases/tag/v0.3.6
- https://github.com/Zarestia-Dev/rclone-manager/releases/tag/v0.3.3
- https://github.com/Zarestia-Dev/rclone-manager/issues/91
- https://github.com/Zarestia-Dev/rclone-manager/issues/312
- https://github.com/Zarestia-Dev/rclone-manager/issues/316
- https://github.com/Zarestia-Dev/rclone-manager/issues/322
- https://github.com/GigaionLLC/Airclone
- https://github.com/hwittenborn/alto
- https://github.com/hwittenborn/alto/releases/tag/v0.9.0
- https://github.com/Jackpison/RcloneExplorer
- https://github.com/Jackpison/RcloneExplorer/releases/tag/v5.4.3
- https://github.com/kapitainsky/RcloneBrowser
- https://github.com/kapitainsky/RcloneBrowser/issues/134
- https://github.com/kapitainsky/RcloneBrowser/issues/249
- https://github.com/kapitainsky/RcloneBrowser/issues/250
- https://github.com/kapitainsky/RcloneBrowser/issues/253
- https://github.com/Alkl58/RcloneBrowser
- https://github.com/Alkl58/RcloneBrowser/issues/21
- https://github.com/jason840709/RcloneBrowserNext
- https://github.com/pieterdd/RcloneShuttle
- https://github.com/yuanweize/LazyMount-Mac
- https://github.com/Sudo-Rahman/UltraExplorer-Releases
- https://github.com/liriliri/rem
- https://github.com/rclone/rclone-web

### Commercial
- https://rcloneview.com/src/pricing
- https://rcloneview.com/support/release-notes/v1.6/
- https://ultra-explorer.app/
- https://www.raidrive.com/pricing
- https://www.airexplorer.net/en/
- https://cyberduck.io/changelog/
- https://mountainduck.io/changelog/
- https://cloudmounter.net/faq/cloudmounter-version-history-win/

### Adjacent tools
- https://github.com/borgbase/vorta
- https://github.com/borgbase/vorta/releases/tag/v0.12.0
- https://github.com/duplicati/duplicati
- https://github.com/kopia/kopia
- https://github.com/garethgeorge/backrest
- https://freefilesync.org/archive.php
- https://extensions.gnome.org/extension/615/appindicator-support/

### Awesome lists and directories
- https://github.com/awesome-rclone/awesome-rclone
- https://awesome-selfhosted.net/tags/file-transfer--synchronization.html
- https://rclone.org/gui/

### Community
- https://forum.rclone.org/t/make-rclone-more-user-friendly-with-a-little-effort/53911
- https://forum.rclone.org/t/backup-monitoring/54134
- https://forum.rclone.org/t/proposal-opt-in-telegram-notifications-for-rclone-copy/54283
- https://forum.rclone.org/t/bisync-hub-safety-first-operational-dashboard-conflict-engine-for-rclone-bisync/54406
- https://forum.rclone.org/t/monti-a-desktop-gui-for-rclone-on-linux-mit/54504
- https://forum.rclone.org/t/opencfdrive-an-rclone-remote-as-a-files-on-demand-drive-in-windows-explorer/54483
- https://forum.rclone.org/t/rclone-manager-does-not-work-in-zorin-os-18-1-core/53736
- https://forum.rclone.org/t/windows-unresponsiveness-with-rclone-mount-on-failing-to-connect-to-remote-expected-behavior-or-fixable/54067
- https://forum.rclone.org/t/rclone-release-1-75-2/54338
- https://forum.rclone.org/t/rclone-ui-v3-slim-but-mighty/52568
- https://forum.rclone.org/t/let-dry-run-differentiate-between-creating-a-new-file-and-overwriting-an-existing-one/53728
- https://forum.rclone.org/t/introducing-mt-sync-a-gnome-app-for-mounting-and-syncing-your-storage/54072
- https://news.ycombinator.com/item?id=46075442
- https://news.ycombinator.com/item?id=48096464
- https://news.ycombinator.com/item?id=47402570
- https://www.reddit.com/r/rclone/comments/1vh18d6/rclone_guis_dont_show_transfer_stats/
- https://www.reddit.com/r/rclone/comments/1qunkq2/full_fledged_rclone_browser/
- https://www.reddit.com/r/rclone/comments/1v14e3v/winscp_or_app_with_similar_interface_for_rclone/
- https://www.reddit.com/r/rclone/comments/1wayift/please_help_me_mount_my_mega_on_startup/
- https://www.reddit.com/r/rclone/comments/1x0ox50/despite_of_v1751_i_still_get_old_web_gui/
- https://www.reddit.com/r/windowsapps/comments/1xcqkmo/new_windows_rclone_gui/
- https://github.com/rclone/rclone/issues/9245
- https://github.com/rclone/rclone/issues/9899
- https://github.com/rclone/rclone/issues/9908

### Standards and platform docs
- https://rclone.org/rc/
- https://raw.githubusercontent.com/rclone/rclone/v1.76.0/docs/content/rc.md
- https://raw.githubusercontent.com/rclone/rclone/v1.76.0/docs/content/commands/rclone_sync.md
- https://raw.githubusercontent.com/rclone/rclone/v1.76.0/docs/content/flags.md
- https://raw.githubusercontent.com/rclone/rclone/v1.76.0/docs/content/commands/rclone_mount.md
- https://raw.githubusercontent.com/rclone/rclone/master/docs/content/docs.md
- https://raw.githubusercontent.com/rclone/rclone/master/librclone/README.md
- https://doc.qt.io/qt-6/qaccessibleannouncementevent.html
- https://support.microsoft.com/en-us/windows/security/threat-malware-protection/smart-app-control-frequently-asked-questions
- https://github.com/macfuse/macfuse/wiki/FUSE-Backends
- https://raw.githubusercontent.com/macos-fuse-t/fuse-t/main/README.md

### Dependency changelogs and engineering blogs
- https://raw.githubusercontent.com/rclone/rclone/master/docs/content/changelog.md
- https://github.com/rclone/rclone/releases/tag/v1.76.0
- https://github.com/rclone/rclone/releases/tag/v1.75.2
- https://github.com/winfsp/winfsp/releases
- https://github.com/macos-fuse-t/fuse-t/releases
- https://macfuse.github.io/2026/10/17/macfuse-5.5.0.html
- https://www.qt.io/blog/qt-6.11.3-released
- https://www.qt.io/blog/qt-6.12-released
- https://download.qt.io/official_releases/qt/6.11/
- https://download.qt.io/official_releases/qt/6.12/

### Security advisories
- https://github.com/rclone/rclone/security/advisories/GHSA-44pm-2q5p-fm63
- https://github.com/rclone/rclone/security/advisories/GHSA-w33h-3qgq-8mv8
- https://github.com/rclone/rclone/security/advisories/GHSA-66qv-66mg-9h74
- https://github.com/rclone/rclone/security/advisories/GHSA-gj73-fh6v-92fj
- https://github.com/rclone/rclone/security/advisories/GHSA-w3h7-jxxq-vfrp
- https://github.com/rclone/rclone/security/advisories/GHSA-qjw8-8hmp-c3m6
- https://github.com/rclone/rclone/security/advisories/GHSA-35g6-9fjv-rmx5
- https://github.com/rclone/rclone/security/advisories/GHSA-f8p4-8g4j-q3m2
- https://github.com/rclone/rclone/security/advisories/GHSA-gpw7-77cq-9f72
- https://github.com/rclone/rclone/security/advisories/GHSA-55qp-jrwr-x827
- https://github.com/rclone/rclone/security/advisories/GHSA-hvpr-p4pv-4f46
- https://github.com/rclone/rclone/security/advisories/GHSA-xqmq-v4wj-rw5m
- https://github.com/rclone/rclone/security/advisories/GHSA-rrwf-2qr6-p832
- https://github.com/rclone/rclone/security/advisories/GHSA-3rqf-7h69-gg5v
- https://github.com/rclone/rclone/security/advisories/GHSA-hwj7-wp9g-wrv9
- https://github.com/rclone/rclone/security/advisories/GHSA-3xpx-hjg2-vrjh
- https://github.com/rclone/rclone/security/advisories/GHSA-vmgx-g9c5-669p
- https://github.com/rclone/rclone/security/advisories/GHSA-m97j-7x65-wh93
- https://github.com/rclone/rclone/security/advisories/GHSA-r5wv-vqqh-2r9c
- https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products

## Open Questions

1. Answered: the WinFsp warning recommends the prerelease (2026 Beta5) and says it's a beta, because the latest stable, 2.1 from 2025-06-05, carries none of the 2026 fixes. It links GitHub's release list, since winfsp.dev/rel and winget only offer that stable build. Revisit when a stable 2026 build ships (the release check fails once the floor's review date is 180 days old).
2. What deletion cap should a new scheduled sync get by default: off, a fixed count, or a share of the destination's file count?
3. For remote-daemon mode over TLS, is trusting a self-signed certificate by its SHA-256 fingerprint acceptable, or should the app require a CA-valid certificate and document a reverse proxy?
4. With fuse-t 1.2.9 on macOS 26, does `-o backend=fskit` fail or fall back when the FSKit extension hasn't been enabled? This needs a Mac.
