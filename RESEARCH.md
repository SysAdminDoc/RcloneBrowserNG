# Research — RcloneBrowserNG

## Executive Summary
Verified: RcloneBrowserNG is a lightweight native Qt 6 desktop GUI for rclone that now has unusually strong coverage for transfers, saved tasks, scheduling, mounts, RC-backed jobs, history, backup/restore, first-run repair, high-contrast polish, and security hardening for a volunteer desktop project. The highest-value direction is trust repair and reliability consolidation, not broad product expansion: current `master` removed `.github/` workflows in `5f722b9`, but public docs, scripts, changelog text, and AppStream metadata still claim GitHub Actions, CodeQL, attestations, workflow-managed releases, and CI-backed package artifacts. Top opportunities: 1. reconcile release/security docs with the local-build-only repo state; 2. add a local release verification harness with tests, smokes, checksums, metadata validation, and optional static checks; 3. prune completed/stale roadmap entries so agents stop re-planning shipped work; 4. add a version-2 staged-transfer schema for execution metadata; 5. replace the non-Windows `b64:` saved-task secret fallback or clearly warn about plaintext-equivalent storage; 6. finish the existing async helper, QTest, accessibility, and i18n roadmap work; 7. add safe mount presets and stale-mount recovery probes before attempting larger workflow-builder or web-dashboard features.

## Product Map
- Core workflows: browse/manage rclone remotes, copy/download/upload/sync/move/bisync with dry-run and saved tasks, queue/stage transfers, inspect job history, mount remotes, schedule recurring jobs, run remote health/config backup/first-run repair utilities.
- User personas: desktop backup operators, rclone power users who want GUI safety around complex flags, cross-platform users avoiding Electron/Tauri/browser shells, and maintainers packaging a small native tool.
- Platforms and distribution: C++17/Qt 6.4+ on Windows, macOS, Linux, and BSD-like source builds; local scripts target Windows zip/installer, macOS app/DMG, and Linux AppImage; `.github/` release/build workflows are absent at current HEAD.
- Key integrations and data flows: rclone CLI and optional RC endpoints, OS scheduling APIs, Windows Credential Manager for rclone password-command, WinFsp/macFUSE/fuse-t/nfsmount mount backends, AppStream/desktop metadata, saved-task JSON, job history JSON, staged-transfer JSON.

## Competitive Landscape
- rclone-ui: does broad Tauri distribution well with many package formats, signatures, and updater UX. Learn from its release artifact breadth and install/update surfaces. Avoid copying its web-runtime footprint because this repo's defensible niche is native Qt.
- RClone Manager: does aggressive multi-platform packaging, localization, update plumbing, watchers, and alert actions well. Learn from its visible mount-default and sleep/wake issue signals. Avoid its visual workflow-builder scope until core reliability and packaging truth are stable.
- H4R1B0/rclone-gui: does native macOS discipline well, especially release validation, safer parsing, persistence-error surfacing, and a large focused test suite. Learn from its test/release gate posture. Avoid macOS-only design assumptions.
- rem, yet-another-rclone-dashboard, and rclone-rc-web-gui: do lightweight web/RC dashboards and responsive layouts well. Learn from their remote-control monitoring surfaces. Avoid building a competing web UI unless the project intentionally leaves native-desktop scope.
- Official rclone GUI/RC: provides the protocol surface, security constraints, and command capabilities the app should track. Learn from new RC endpoints and security advisories. Avoid bypassing rclone semantics with parallel custom sync logic.
- GoodSync, WinSCP, Mountain Duck, and RcloneView: show table-stakes commercial expectations around durable queues, verification, sync profiles, mount/cache health, and config migration. Learn recovery and trust patterns. Avoid cloning full smart-sync/offline filesystem scope.

## Security, Privacy, and Reliability
- Verified: `README.md:6`, `SECURITY.md:35`, `SECURITY.md:36`, `SECURITY.md:37`, `CHANGELOG.md`, `scripts/release_windows.cmd:5`, `scripts/release_AppImage.sh:5`, `scripts/release_macOS.sh:5`, and `assets/io.github.sysadmindoc.rclonebrowserng.metainfo.xml:17` still describe workflows/CI/attestations/release automation that do not exist after `.github/` removal.
- Verified: `src/job_options_store.cpp:21` uses Windows DPAPI for protected task fields, but `src/job_options_store.cpp:29` falls back to reversible `b64:` on non-Windows for heartbeat URLs, shell hooks, and webhook URLs. Existing roadmap coverage is correct but should remain high priority.
- Verified: staged transfers are persisted atomically, but `src/main_window.cpp:2707` only saves message/source/dest/args/backup retention. `src/main_window.h:76`, `src/main_window.h:77`, `src/main_window.h:78`, `src/main_window.h:79`, and `src/main_window.h:127` show execution fields that are missing from the staged schema, including notification hooks, task label, and verification state.
- Verified: `src/schedule_manager.cpp`, `src/mount_backend.cpp`, and `src/mount_widget.cpp` still contain GUI-path `waitForFinished()` helpers; the existing roadmap item to remove helper-process waits remains valid.
- Verified: post-job shell hooks in `src/main_window.cpp:3273` and `src/main_window.cpp:3637` run via `QProcess::startDetached`, so exit status is not captured in history even though the trust gate now blocks unreviewed hooks.
- Verified: rclone RC security advisories make authenticated app-started RC regression tests important; this repo already adds random mount RC credentials, but future helper paths need automated gates.
- Likely: a local release harness should become the replacement control point for checksums, package smoke tests, AppStream validation, static checks, and SBOM/manifest generation if GitHub Actions intentionally stays removed.

## Architecture Assessment
- Verified: `src/main_window.cpp` remains the orchestration hub for staging, saved tasks, scheduling, mounts, RC jobs, history, health checks, and release-adjacent workflows. Future work should move persistence schemas and process-helper orchestration behind small tested helpers before adding more UI surface.
- Verified: `scripts/release_AppImage.sh:70` treats AppStream validation as best-effort with `|| true`, which is appropriate for exploratory local builds but weak for release artifacts.
- Verified: `src/remote_widget.cpp:1213` exposes VFS cache mode through a combo box and `src/preferences_dialog.cpp:63` exposes raw default mount flags. A mount-profile layer can make defaults clearer without removing expert control.
- Verified: most tests now use Qt Test, but `tests/parsing_regression_test.cpp` still uses the older custom harness. This is already covered by existing roadmap work and should not be re-added as a duplicate.
- Verified: `main.cpp` loads `QTranslator`, but the source scan found only two `tr()` call sites in `src/main_window.cpp`; i18n is scaffolding, not a complete translation-ready source tree. Existing blocked/roadmap i18n work remains correct.
- Verified: many controls have accessible names and high-contrast utilities, but there is not yet a broad keyboard/screen-reader smoke suite. Existing accessibility roadmap coverage remains correct.

## Rejected Ideas
- Web UI or mobile companion app: official rclone GUI, rclone-ui, yet-another-rclone-dashboard, and rclone-rc-web-gui already cover the web lane; it contradicts the native lightweight Qt niche.
- Visual workflow builder: RClone Manager issue traffic shows interest, but it is scope-heavy and should wait until release trust, persistence, and helper-process reliability are settled.
- Full smart sync/offline filesystem: Mountain Duck/GoodSync and sync research show the complexity; RcloneBrowserNG should expose rclone/bisync safely rather than inventing a sync engine.
- Plugin marketplace: rclone already owns backend extensibility; a GUI plugin system would add maintenance/security surface without clear current demand.
- Multi-user server/headless Docker mode: useful in adjacent tools, but already treated as blocked/out-of-scope for this native desktop repo.
- Rewriting in Tauri/Electron/Swift/Rust: competitors already occupy those stacks; a rewrite would discard the project's main differentiator.
- Store/package submissions and code signing as agent-ready work: still blocked by maintainer credentials and external account ownership, so it belongs in blocked planning rather than active roadmap additions.

## Sources
Competitors and analogous tools:
- https://github.com/rclone-ui/rclone-ui
- https://github.com/rclone-ui/rclone-ui/releases/tag/v3.6.0
- https://github.com/Zarestia-Dev/rclone-manager
- https://github.com/Zarestia-Dev/rclone-manager/releases/tag/v0.2.8
- https://github.com/Zarestia-Dev/rclone-manager/issues/225
- https://github.com/Zarestia-Dev/rclone-manager/issues/5
- https://github.com/H4R1B0/rclone-gui
- https://github.com/H4R1B0/rclone-gui/releases/tag/v1.6.0
- https://github.com/liriliri/rem
- https://github.com/outlook84/yet-another-rclone-dashboard
- https://github.com/retifrav/rclone-rc-web-gui
- https://awesome-rclone.com/

Commercial and adjacent products:
- https://rcloneview.com/support/blog/config-backup-restore-migrate-rcloneview
- https://www.goodsync.com/features
- https://winscp.net/eng/docs/transfer_queue
- https://docs.duck.sh/mountainduck/sync/

Core docs and security:
- https://rclone.org/changelog/
- https://rclone.org/rc/
- https://rclone.org/gui/
- https://rclone.org/commands/rclone_check/
- https://github.com/rclone/rclone/security/advisories/GHSA-qw24-gh76-8rvv
- https://github.com/rclone/rclone/security/advisories/GHSA-25qr-6mpr-f7qx
- https://github.com/advisories/GHSA-jfwf-28xr-xw6q
- https://doc.qt.io/qt-6/qaccessible.html
- https://doc.qt.io/qt-6/internationalization.html

Packaging and release engineering:
- https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations
- https://docs.appimage.org/packaging-guide/optional/updates.html
- https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines

Sync research:
- https://tonsky.me/blog/crdt-filesync/
- https://www.usenix.org/conference/2003-usenix-annual-technical-conference/place-rsync-file-synchronization-mobile-and

## Open Questions
- Should local-only release engineering remain the intended policy, or should GitHub Actions be restored? This choice changes whether the release harness is script-only or workflow-backed.
- Are new macOS/Linux secret-storage dependencies acceptable, or should non-Windows saved-task secrets use an explicit warned plaintext mode until a small vault abstraction is approved?
- Which signing/package-publication accounts are available to the maintainer? This determines whether blocked distribution work can move into the active roadmap.
