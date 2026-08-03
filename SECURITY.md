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

## Security Measures

- Release builds are produced by the maintainer's local release scripts; this repository does not provide CI builds, checksums, provenance attestations, or CodeQL/Scorecard results.
- The local Windows release script validates the configured Qt version against the documented security floor.
- The app warns when the detected rclone version has known CVEs
