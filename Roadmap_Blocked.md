# Blocked Roadmap — RcloneBrowserNG

Items in this file are blocked on maintainer identity, credentials, paid enrollments, external application submission, or another dependency that cannot be completed from the repo alone. Keep `ROADMAP.md` limited to actionable work. Move an item back to `ROADMAP.md` only when the blocker is cleared.

---

## P1 — Distribution

- [ ] P1 — Code signing before Homebrew September 2026 deadline
  Blocked: Requires SignPath approval/service access plus Apple Developer enrollment/notarization credentials.
  Why: Homebrew will remove unsigned casks after September 1, 2026; missing this deadline kills the Homebrew distribution channel. SignPath Foundation provides free OV code signing for OSS (private key on HSM, CI-integrated). Apple notarization requires $99/yr developer account.
  Evidence: workbrew.com/blog/homebrew-5-0-0; signpath.io/solutions/open-source-community; Homebrew discussion #6482
  Touches: .github/workflows/release.yml, code-signing policy, README
  Acceptance: Windows binaries Authenticode-signed via SignPath; macOS DMG notarized and stapled; Homebrew cask installable after September 2026.
  Complexity: L
  Note: Supersedes/elevates the existing P2 code-signing item with a hard deadline.

---

## P2 — Distribution

- [ ] P2 — Publish to winget, Scoop, and a -ng AUR package
  Blocked: Requires external package-registry submissions and maintainer/registry approval outside the repo before acceptance can be completed.
  Why: inherited Repology entries point at the dead upstream; peers (Syncthing Tray, Cyberduck) ship these channels.
  Evidence: repology.org/project/rclone-browser/packages; microsoft/winget-pkgs
  Touches: packaging manifests, CI (wingetcreate)
  Acceptance: winget/scoop/AUR all install the current NG release; version bumps automated in CI.
  Complexity: M

- [ ] P2 — Prepare SignPath Foundation application materials
  Blocked: Requires external SignPath application submission and maintainer-controlled policy decisions before implementation can be accepted as complete.
  Why: SignPath Foundation approval takes 2-4 weeks. Requires CODE_OF_CONDUCT, a published code signing policy, and a public CI workflow. These prerequisites must be in place before the signing infrastructure can be integrated. Blocks the existing P1 code signing deadline item.
  Evidence: signpath.org/terms.html; zenn.dev/shm_7ec/articles/signpath-oss-code-signing (real applicant timeline)
  Touches: CODE_OF_CONDUCT.md (new), code-signing-policy.md (new), README, .github/workflows/release.yml
  Acceptance: CODE_OF_CONDUCT and code signing policy published; SignPath application submitted with required documentation.
  Complexity: S

- [ ] P2 — Code signing (SignPath Foundation Windows + Apple notarization)
  Blocked: Requires maintainer identity, SignPath approval/service access, and Apple Developer enrollment/notarization credentials.
  Why: SmartScreen/Gatekeeper warnings deter installs; Homebrew purges unsigned casks Sept 2026, killing the inherited cask without notarization.
  Evidence: signpath.org/terms; developer.apple.com/developer-id; github.com/Homebrew/brew/issues/20755
  Touches: .github/workflows, code-signing policy, README
  Acceptance: Windows binaries Authenticode-signed via SignPath; macOS dmg notarized (blocked on maintainer identity + $99 Apple fee — see RESEARCH Open Questions).
  Complexity: M

---

## P1 — CI-dependent analysis

- [ ] P1 — Strengthen CodeQL to traced build and SHA-pin its actions
  Blocked: Requires restoring maintainer-owned GitHub Actions/CodeQL infrastructure; the repository intentionally has no `.github` workflows after `5f722b9`, so the CI/Security-tab acceptance cannot be completed locally.
  Why: `build-mode: none` performs extractionless C++ analysis that misses interprocedural dataflow, taint tracking, and most vulnerability classes. The CodeQL init and analyze actions use unpinned `@v4` tags while every other workflow action is SHA-pinned, violating the project's own supply-chain security policy.
  Evidence: `.github/workflows/codeql.yml` lines 21/28 use `@v4` not SHA pins; `build-mode: none` per CodeQL docs provides "limited coverage for compiled languages"; all other workflows (build.yml, release.yml, scorecard.yml) SHA-pin every action.
  Touches: `.github/workflows/codeql.yml`, `.github/workflows/build.yml` (add CodeQL build step sharing the existing Linux cmake/make invocation).
  Acceptance: CodeQL init and analyze actions are SHA-pinned to a specific v4 release; CodeQL runs with `build-mode: manual` using the existing Linux cmake build; C++ query results improve (visible in Security tab SARIF output); no unpinned action tags remain in any workflow file.
  Complexity: S

- [ ] P2 — Add clang-tidy static analysis to Linux CI
  Blocked: Acceptance requires a Linux CI runner and workflow, which are not present in the local-build-only repository; `clang-tidy` is also unavailable on this workstation.
  Why: No static analysis tool beyond CodeQL runs on the codebase. clang-tidy catches use-after-free patterns, uninitialized variables, modernization opportunities, and Qt-specific issues that extractionless CodeQL and MSVC /W4 miss.
  Evidence: No `.clang-tidy` or `CMAKE_CXX_CLANG_TIDY` in the repo; `src/main_window.cpp` is 4517 lines of complex control flow; OpenSSF recommends multiple overlapping analyzers; Qt moc-generated files need suppression.
  Touches: `.clang-tidy` (new config file), `CMakeLists.txt` or `.github/workflows/build.yml` (enable in Linux CI job).
  Acceptance: clang-tidy runs on the Linux CI build with `bugprone-*`, `cppcoreguidelines-*`, `performance-*`, and `modernize-*` checks enabled; moc/uic generated files are excluded via header-filter; findings are zero or explicitly suppressed with rationale; CI fails on new clang-tidy warnings.
  Complexity: S

---

## P3 — Visual design

- [ ] P3 — Add remote-type icons for post-v1.68 rclone backends
  Blocked: Requires maintainer-approved visual/icon design decisions before new provider artwork can be accepted.
  Why: Remote provider icons in `src/images/` cover only 18 services. Backends added since rclone v1.69 (iCloud Drive, iCloud Photos, Archive, Filen, Internxt, Huawei Drive, Cloudinary, FileLu, DOI, Shade, Drime) all display as `unknown.png`, making the remotes list visually unhelpful for users of newer providers.
  Evidence: `src/images/` contains 18 service icon pairs (normal + _inv); rclone v1.69-v1.74 changelogs list 11+ new backends; `src/resources.qrc` embeds the icon files.
  Touches: `src/images/` (new PNG pairs), `src/resources.qrc`, `src/main_window.cpp` (icon-name-to-type mapping if not already dynamic).
  Acceptance: At minimum iCloud Drive, Proton Drive, pCloud, Box, Nextcloud/WebDAV, Backblaze B2 (already has), and Filen have dedicated icons with `_inv` dark variants; icons are palette-aware; `resources.qrc` includes all new files.
  Complexity: S

---

## P4 — Platform & Compatibility

- [ ] **Code signing for releases** — Sign binaries and provide checksums.
  Blocked: Superseded by the P1/P2 signing work above and requires the same external signing identities and service access.
  _Sources: rclone forum_

- [ ] **Flatpak packaging** — Distribute via Flathub.
  Blocked: Flathub AI policy (May 29, 2026) prohibits AI-generated code in submissions. Project has AI-assisted commits. Needs maintainer decision on whether the "mature, well-maintained projects" exception applies. Also requires external Flathub submission review.
  _Sources: mmozeiko #112; linuxiac.com/flathub-now-rejects-ai-assisted-apps-and-submissions_

- [ ] **Snap packaging** — Distribute via Snap Store.
  Blocked: Requires Snap Store account registration and external submission/review.

- [ ] **Homebrew formula fix** — Current install script 404s (needs `.sh` suffix). Unmerged PR: kapitainsky#232.
  Blocked: Requires external PR to Homebrew repository + maintainer-owned Homebrew cask.
  _Sources: kapitainsky #232_

- [ ] P2 — Add screenshots to AppStream metainfo
  Blocked: Requires actual screenshots captured from the running app on a display. Cannot be automated without a running GUI environment.
  Why: `<screenshots>` element is required by Flathub for app publication. The metainfo file has none.
  Evidence: `assets/io.github.sysadmindoc.rclonebrowserng.metainfo.xml` has no `<screenshots>` element; docs.flathub.org requirements
  Touches: `assets/io.github.sysadmindoc.rclonebrowserng.metainfo.xml`, screenshot image files (hosted on GitHub)

- [ ] P3 — AppImage GPG signing
  Blocked: Requires a GPG key owned by the maintainer. Cannot be created or used without maintainer credentials.
  Why: AppImages support embedded GPG signatures via `appimagetool --sign`. Currently release AppImages are unsigned.
  Evidence: docs.appimage.org/packaging-guide/optional/signatures.html; release.yml produces unsigned AppImages
  Touches: `.github/workflows/release.yml` (sign step after build)

---

## P3 — Depends on rcd engine (not yet implemented)

- [ ] P3 — Graceful stop: finish in-flight files, then stop
  Blocked: Depends on rcd engine item which hasn't landed yet. The graceful stop requires rc job semantics to finish current files.
  Why: the only stop today is kill; finishing current files avoids partial-transfer waste.
  Evidence: github.com/rclone/rclone/issues/966
  Touches: job_widget.cpp, engine (depends on rcd engine item)
  Acceptance: jobs offer Stop (immediate) and Finish current files; the latter completes active transfers and skips queued ones.
  Complexity: M

- [ ] P3 — Live tuning of running jobs (bwlimit / transfers / checkers)
  Blocked: Depends on rcd engine item. rc exposes `core/bwlimit` on running daemons; natural once the rcd engine lands.
  Why: no GUI surfaces live tuning of running jobs.
  Evidence: github.com/rclone/rclone/issues/3898; rclone.org/rc
  Touches: job_widget.cpp, engine (depends on rcd engine item)
  Acceptance: a running job's panel lets the user change bandwidth limit and transfer concurrency without restarting.
  Complexity: M

- [ ] P3 — Leverage job/batch RC endpoint for multi-command operations
  Blocked: Depends on rcd engine maturity. rclone v1.72 added `job/batch` for concurrent RC command batches.
  Why: multi-file operations (delete multiple, move multiple) could use a single round-trip instead of sequential spawns.
  Evidence: rclone.org/rc (job/batch endpoint, v1.72); existing rcd engine roadmap
  Touches: `src/rclone_rc_engine.cpp` (depends on rcd engine maturity)
  Acceptance: batch-capable operations use job/batch when available; fallback to sequential for older rclone.
  Complexity: M

---

## P3 — Depends on other unimplemented items

- [ ] P3 — Condition-gated schedules: unmetered network / AC power
  Blocked: Depends on scheduled-tasks item + platform power/network probes not yet implemented.
  Why: Vorta/Round-Sync gate runs on power/network conditions; laptops syncing over hotspots is the failure mode.
  Evidence: github.com/borgbase/vorta; newhinton/Round-Sync#96
  Touches: scheduler (depends on scheduled-tasks item), platform power/network probes
  Acceptance: a scheduled task can require AC power and/or non-metered network; skipped runs are logged with reason.
  Complexity: M

- [ ] P3 — Adaptive bandwidth throttling based on user network activity
  Blocked: Depends on existing bandwidth limit items + platform-specific network activity detection not yet implemented.
  Why: FDM's traffic usage modes auto-throttle when user is browsing, full speed when idle.
  Evidence: freedownloadmanager.org; rclone.org/docs (--bwlimit); existing bandwidth roadmap items
  Touches: rclone rc `core/bwlimit`, network activity detection (platform-specific)
  Acceptance: "Adaptive" mode monitors outbound traffic; throttles when other apps use bandwidth; user configures floor/ceiling.
  Complexity: L

- [ ] P3 — Fuzz the three parsers with ClusterFuzzLite
  Blocked: Depends on parsing-regression-tests item for harness extraction (test infrastructure must exist first).
  Why: listing parser, stats/json-log parser, and tasks.bin loader are historically-broken surfaces.
  Evidence: google.github.io/clusterfuzzlite; item_model.cpp/job_widget.cpp/list_of_job_options.cpp
  Touches: new fuzz targets dir, CMake option, .clusterfuzzlite/ config
  Acceptance: three fuzz targets build and run in PR CI; seed corpora from recorded rclone outputs.
  Complexity: M

---

## P3 — i18n (external service enrollment)

- [ ] P3 — i18n retrofit (wrap strings in tr(), Weblate integration)
  Blocked: Requires Weblate account registration (external service) and translator community. Zero tr() calls today.
  Why: prerequisite for i18n support; Weblate natively supports Qt .ts, free for OSS.
  Evidence: RESEARCH.md i18n; docs.weblate.org Qt format
  Touches: all UI strings, CMake (lupdate/lrelease), Weblate config
  Acceptance: UI strings translatable; a .ts workflow exists; English template generated in CI.
  Complexity: L

- [ ] P3 — i18n / multi-language support
  Blocked: Depends on i18n retrofit above + needs translation contributors.
  _Sources: kapitainsky #138, #47; rclone forum (Portuguese, others)_

---

## P3 — Platform (complex system integration)

- [ ] P3 — VSS snapshot option for locked files (Windows)
  Blocked: Requires elevated vshadow helper and complex Windows platform integration that cannot be safely tested without live Volume Shadow Copy setup.
  Why: copying open files (Outlook PSTs, VMs) fails without VSS; FreeFileSync ships it.
  Evidence: github.com/rclone/rclone/issues/990; freefilesync.org
  Touches: transfer_dialog.cpp (option), elevated helper for vshadow
  Acceptance: local-source task can opt into "snapshot locked files"; requires elevation; gracefully explains when unavailable.
  Complexity: L

---

## P4 — Platform & Compatibility (CI infrastructure)

- [ ] P4 — ppc64le build support
  Blocked: Requires ppc64le CI runners or cross-compilation infrastructure not available.
  _Sources: kapitainsky #152_

- [ ] P4 — FreeBSD headless compilation
  Blocked: Requires FreeBSD CI runners or cross-compilation setup not available.
  _Sources: kapitainsky #247_

---

## P5 — Stretch Goals (major architecture work)

- [ ] P5 — Web UI mode — Serve the GUI over HTTP for headless/NAS/Docker deployments.
  Blocked: Requires significant architecture work (full web frontend). Stretch goal.
  _Sources: mmozeiko #51; rclone forum_

- [ ] P5 — Remote rclone server mode — Connect to an rclone `rcd` instance running on another machine.
  Blocked: Requires significant architecture work. Stretch goal.
  _Sources: mmozeiko #38; kapitainsky remote control mentions_

- [ ] P5 — Docker container with web access — Pre-built container image.
  Blocked: Depends on Web UI mode above.
  _Sources: docker repo, 86 stars_
