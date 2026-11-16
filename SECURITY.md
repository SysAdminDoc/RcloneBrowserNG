# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 2.2.x   | Yes                |
| 2.1.x   | No (update to 2.2) |
| 2.0.x   | No (update to 2.2) |
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

- Vulnerabilities in rclone itself go to the [rclone project](https://github.com/rclone/rclone/security)
- Vulnerabilities in Qt go to [Qt](https://www.qt.io/security)
- Vulnerabilities in WinFsp or macFUSE go to those projects directly

## Qt branch policy

Release builds ship **Qt 6.12.0**, built from source with two security fixes that have no 6.12 binary yet. Revisit this on **2027-04-01**.

Two things have to be true of the Qt a release is built with, and both are enforced by `scripts/validate_qt_version.py`, which every release script calls:

1. It carries the fixes for the Qt vulnerabilities the app can reach. CVE-2026-6210 (QtSvg type confusion and heap overflow) and CVE-2026-9499 (`QTextCodec::codecForName` out-of-bounds read) landed in 6.8.8 and 6.11.1. CVE-2026-95620 and CVE-2026-96432 (`QNetworkAccessManager` over HTTP/2) landed in 6.11.3, and the app reaches them through the GitHub update checks and the remote daemon. None of the four affects 6.12.0. Two other recent Qt CVEs don't apply: CVE-2026-101201 is in `QBinaryJson`, part of Qt5Compat, which the app doesn't link, and CVE-2026-105924 needs CBOR input, which the app never reads.
2. Its branch still receives open-source patches, so the next vulnerability will actually reach it.

Qt 6.12 is the current LTS. It came out on 2026-09-30 and gets open-source patches until 2027-04-01 (endoflife.date, checked 2026-11-15). endoflife.date ends 6.11's support on the day 6.12 shipped. 6.11.3 still followed on 2026-10-26 as the branch's last release, and the validator refuses it with the rest of 6.11.

Two fixes don't have a binary yet. CVE-2026-116337 (memory exhaustion from an HTTP reply's status line or headers, reachable through the remote daemon and the GitHub update check) and CVE-2026-105943 (on Windows, NTLM single sign-on that doesn't pass the server's SPN) are fixed in 6.12.1. On 2026-11-15 download.qt.io had no 6.12.1 binaries, but it did publish the qtbase source diffs for 6.12, on 2026-11-09 and 2026-11-13.

`scripts/build_patched_qt.py` builds qtbase 6.12.0 from the release sources with those three diffs. It checks the source archive and each diff against a SHA-256 pinned in the script before using them, and it leaves `qt-security-patches.json` in the install listing what went in. The validator reads that file through qmake and refuses a 6.12.0 that doesn't have both fixes, so the stock 6.12.0 binaries can't ship. 6.12.1 passes as it is once it's out. The app links only qtbase modules (Core, Gui, Widgets and Network). The script also builds Qt SVG 6.12.0 from its unchanged release source, because the package ships Qt SVG's image and icon plugins so `.svg` files preview in the remote browser. The script runs on Linux and macOS too, with the compiler on PATH.

Qt 6.8 LTS was the obvious choice before 6.12, but it was never usable here. Its open-source window closed on 2025-04-02, and the public archive at download.qt.io carries 6.8 only up to **6.8.5** (checked 2026-11-15). The 6.8.8 that fixes the first two CVEs is available to commercial licensees only. Building a release from open-source 6.8 means shipping a known-vulnerable QtSvg.

When the next LTS ships, add it to the policy table in `scripts/validate_qt_version.py`. The validator warns for the last 30 days of a branch's window and fails once it has passed.

Source builds are a separate matter and floor at Qt 6.4, which is what the README badge claims and what the oldest supported distribution ships. People building for themselves aren't distributing binaries, so the stricter floor only applies to what gets published.

## Security Measures

- Release builds come from the maintainer's local release scripts. The exception is the macOS DMG, which a hand-started workflow builds on a GitHub-hosted Mac because there's no Mac on hand, and that workflow publishes nothing. This repository does not provide CI builds on push, provenance attestations, or published CodeQL and Scorecard results. Each release ships a `SHA256SUMS` file.
- `scripts/codeql_scan.ps1` runs CodeQL's security-extended queries over the app's C++, traced from a clean MSVC build, and over the Python release scripts. It runs on the maintainer's machine, and its results aren't published. The scan on 2026-11-15 found nothing.
- The Windows, AppImage and macOS release scripts validate the configured Qt against the documented security floor, including the source-patch record for 6.12.0.
- The app warns when the detected rclone version has known CVEs
