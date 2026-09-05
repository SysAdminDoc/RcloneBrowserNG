# Blocked Roadmap — RcloneBrowserNG

Items in this file are blocked on maintainer identity, credentials, paid enrollments, external application submission, or another dependency that cannot be completed from the repo alone. Keep `ROADMAP.md` limited to actionable work. Move an item back to `ROADMAP.md` only when the blocker is cleared.

---

## P1 — Distribution

- [ ] P1 — Code signing (Windows Authenticode + Apple notarization)
  Blocked: Requires an external signing identity — SignPath Foundation approval, an Azure Artifact Signing subscription, or a Certum OV certificate — plus Apple Developer enrollment for notarization.
  Why (revised 2026-09-04): the Homebrew cutoff has PASSED, not approaching. Homebrew disabled unsigned casks in the official repository on 2026-09-01, so the inherited cask is already gone; a maintainer-owned third-party tap is the unblocked interim route and is tracked in ROADMAP.md. macOS got harder in the same window: macOS Sequoia removed the Control-click "Open Anyway" Gatekeeper bypass, so an unnotarized DMG needs a trip through System Settings. Windows got cheaper and less exclusive: Azure Artifact Signing is $9.99/month, FIPS 140-3, and explicitly available to individuals in the US, Canada, EU and UK, so SignPath is no longer the only option; note also that EV certificates no longer grant instant SmartScreen reputation and certificate lifetimes are capped at 460 days from 2026-03-01.
  Evidence: workbrew.com/blog/homebrew-5-0-0; Homebrew discussion #6482; azure.microsoft.com/pricing/details/trusted-signing; CA/B Forum Code Signing BRs v3.7; signpath.io/solutions/open-source-community
  Touches: local release scripts, code-signing policy, README
  Acceptance: Windows artifacts are Authenticode-signed with a timestamp; the macOS DMG is notarized and stapled; both are produced by the local release scripts, since the repository has no CI.
  Complexity: L
  Note: Supersedes the two P2 code-signing entries below.

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
  Blocked: Only the CI enforcement remains blocked. The `.clang-tidy` config landed at the repo root in `24798bb` and the workflow that ran it was removed with `.github/` in `5f722b9`; there is no Linux runner to re-enable it on, and `clang-tidy` is not installed on this workstation.
  Why: No static analysis tool runs on the codebase today. clang-tidy catches use-after-free patterns, uninitialized variables, modernization opportunities, and Qt-specific issues that MSVC /W4 misses.
  Evidence: `.clang-tidy` exists but no `CMAKE_CXX_CLANG_TIDY` wires it into the build; `src/main_window.cpp` is 4517 lines of complex control flow; OpenSSF recommends multiple overlapping analyzers; Qt moc-generated files need suppression.
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

## P3 — Depends on the RC engine leaving the core/command shim

Correction (2026-09-04): the rcd engine IS implemented (`src/rclone_rc_engine.cpp`, wired at `src/main_window.cpp:3256`). What these items actually depend on is the migration off `core/command` onto native `sync/*` calls with `_async` and `_group`, which is now tracked as an actionable item in ROADMAP.md. The other two entries that were parked here (live bandwidth tuning, graceful stop) have been moved back to ROADMAP.md.

- [ ] P3 — Leverage job/batch RC endpoint for multi-command operations
  Blocked: Depends on the `sync/*` migration in ROADMAP.md landing first. rclone v1.72 added `job/batch` for concurrent RC command batches.
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

---

## P3 — i18n (external service enrollment)

- [ ] P3 — Weblate enrollment and translator onboarding
  Blocked: Requires a Weblate account (external service) and a translator community. Split 2026-09-04 — the `tr()` retrofit and English `.ts` generation need no external service and moved to ROADMAP.md; only the hosting and contributor half is blocked.
  Why: Weblate natively supports Qt `.ts` and is free for open source, but it needs an account and people willing to translate.
  Evidence: docs.weblate.org Qt format; kapitainsky#138, kapitainsky#47
  Touches: Weblate project config, `translations/`, CONTRIBUTING
  Acceptance: the Weblate project is live against `translations/rclonebrowser_en.ts`, at least one non-English locale reaches usable coverage, and the generated `.qm` files ship in the release artifacts.
  Complexity: M

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
