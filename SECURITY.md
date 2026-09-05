# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 2.0.x   | Yes                |
| < 2.0   | No (upstream abandoned) |

## Reporting a Vulnerability

If you discover a security vulnerability in Rclone Browser NG, please report it responsibly.

**Preferred method:** [GitHub Security Advisories](https://github.com/SysAdminDoc/RcloneBrowserNG/security/advisories/new)

This allows private discussion and coordinated disclosure. You will receive an acknowledgement within 72 hours and a substantive response within 7 days.

**Alternative:** Email the maintainer directly (see the GitHub profile for contact info).

## What to Report

- Vulnerabilities in Rclone Browser NG code (C++/Qt)
- Unsafe handling of rclone output or user input
- Credential exposure or insecure storage
- Build and release tooling or artifact-integrity issues in this repository

## What Not to Report Here

- Vulnerabilities in rclone itself — report those to the [rclone project](https://github.com/rclone/rclone/security)
- Vulnerabilities in Qt — report those to [Qt](https://www.qt.io/security)
- Vulnerabilities in WinFsp or macFUSE — report to those projects directly

## Qt branch policy

Release builds ship **Qt 6.11.2**. Revisit this on **2026-09-22**.

Two things have to be true of the Qt a release is built with, and both are
enforced by `scripts/validate_qt_version.py`, which every release script
calls:

1. It carries the fixes for CVE-2026-6210 (QtSvg type confusion and heap
   overflow) and CVE-2026-9499 (`QTextCodec::codecForName` out-of-bounds
   read). Both landed in 6.8.8 and 6.11.1.
2. Its branch still receives open-source patches, so the next vulnerability
   will actually reach it.

Qt 6.8 is the current LTS and looks like the obvious choice, but it is not
usable here. Its open-source window closed on 2025-04-02, and the public
archive at download.qt.io carries 6.8 only up to **6.8.4** (checked
2026-09-05). The 6.8.8 that fixes the two CVEs above is available to
commercial licensees only. Building a release from open-source 6.8 means
shipping a known-vulnerable QtSvg.

That leaves 6.11, whose 6.11.2 is published and patched. Its open-source
support ends 2026-09-22, and Qt 6.12 — the next LTS — had not been released
as of 2026-09-05. When 6.12 ships, move to it and add it to the policy table
in `scripts/validate_qt_version.py`; the validator warns for the last 30 days
of a branch's window and fails once it has passed.

Source builds are a separate matter and floor at Qt 6.4, which is what the
README badge claims and what the oldest supported distribution ships. People
building for themselves are not distributing binaries; the stricter floor
applies to what gets published.

## Security Measures

- Release builds are produced by the maintainer's local release scripts; this repository does not provide CI builds, checksums, provenance attestations, or CodeQL/Scorecard results.
- The local Windows release script validates the configured Qt version against the documented security floor.
- The app warns when the detected rclone version has known CVEs
