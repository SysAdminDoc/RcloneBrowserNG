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

## P4 — Platform & Compatibility

- [ ] **Code signing for releases** — Sign binaries and provide checksums.
  Blocked: Superseded by the P1/P2 signing work above and requires the same external signing identities and service access.
  _Sources: rclone forum_
