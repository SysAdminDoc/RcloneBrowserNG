#!/usr/bin/env python3
"""Check the Qt used for release packaging against the project's branch policy.

Two separate things can be wrong with a Qt build, and the release lane has to
refuse both:

1. It carries a known vulnerability. CVE-2026-6210 (QtSvg type confusion and
   heap overflow) and CVE-2026-9499 (QTextCodec::codecForName out-of-bounds
   read) are both fixed in 6.8.8 and 6.11.1.
2. Its branch has stopped receiving open-source patches, so the next
   vulnerability will never reach it.

Checked against download.qt.io on 2026-09-05: the public open-source archive
carries 6.8 only up to 6.8.4, so the 6.8.8 fix is commercial-only and 6.8 LTS
is not a usable release branch for this project. 6.11.2 is published and
carries both fixes, which makes 6.11 the branch to ship from until 6.12 LTS
arrives.

Run standalone or import BRANCHES/evaluate from a test.
"""

from __future__ import annotations

import argparse
from datetime import date
import re
import subprocess
import sys


# Per-branch policy. `patched_from` is the first patch on that branch carrying
# both CVE fixes, or None when no such patch was ever published openly.
# `oss_support_ends` is when open-source patches stop, from endoflife.date.
BRANCHES = {
    (6, 5): {"patched_from": None, "oss_support_ends": date(2023, 10, 9)},
    (6, 6): {"patched_from": None, "oss_support_ends": date(2024, 4, 2)},
    (6, 7): {"patched_from": None, "oss_support_ends": date(2024, 10, 7)},
    # LTS, but its open-source window closed and the archive stops at 6.8.4,
    # below the 6.8.8 that carries the fixes.
    (6, 8): {"patched_from": None, "oss_support_ends": date(2025, 4, 2)},
    (6, 9): {"patched_from": None, "oss_support_ends": date(2025, 10, 7)},
    (6, 10): {"patched_from": None, "oss_support_ends": date(2026, 4, 7)},
    (6, 11): {"patched_from": (6, 11, 1), "oss_support_ends": date(2026, 9, 22)},
}

# The branch the project ships from today, and when that choice must be
# revisited. Keep in step with SECURITY.md.
RECOMMENDED = "6.11.2"
REVISIT_ON = date(2026, 9, 22)

CVES = "CVE-2026-6210, CVE-2026-9499"


def parse_version(text: str) -> tuple[int, int, int]:
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", text.strip())
    if not match:
        raise ValueError(f"could not read a Qt version from {text!r}")
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)))


def evaluate(version: tuple[int, int, int], today: date) -> list[str]:
    """Return the reasons this Qt must not be used for a release, if any."""
    major, minor, patch = version
    problems: list[str] = []

    if major < 6:
        return [
            f"Qt {major}.{minor}.{patch} is not supported; this project "
            f"requires Qt 6. Use {RECOMMENDED}."
        ]

    branch = BRANCHES.get((major, minor))
    if branch is None:
        # A branch newer than the table. Unknown rather than bad: say so
        # instead of guessing, so a 6.12 LTS build is not blocked by silence.
        if (major, minor) > max(BRANCHES):
            print(
                f"NOTE: Qt {major}.{minor} is newer than this policy table. "
                f"Confirm it carries the fixes for {CVES}, then add it to "
                "scripts/validate_qt_version.py.",
                file=sys.stderr,
            )
            return []
        return [f"Qt {major}.{minor} is not a recognised branch."]

    patched_from = branch["patched_from"]
    if patched_from is None:
        problems.append(
            f"Qt {major}.{minor} never received a public open-source patch "
            f"for {CVES}. Use {RECOMMENDED}."
        )
    elif version < patched_from:
        wanted = ".".join(str(part) for part in patched_from)
        problems.append(
            f"Qt {major}.{minor}.{patch} is in the {CVES} vulnerable range. "
            f"Use {wanted} or newer on this branch."
        )

    ends = branch["oss_support_ends"]
    if today > ends:
        problems.append(
            f"Qt {major}.{minor} stopped receiving open-source patches on "
            f"{ends.isoformat()}, so the next vulnerability will not reach "
            f"it. Move to the current branch (see SECURITY.md)."
        )
    elif (ends - today).days <= 30:
        print(
            f"WARNING: Qt {major}.{minor} open-source support ends "
            f"{ends.isoformat()}, in {(ends - today).days} day(s). Plan the "
            "move now; see SECURITY.md.",
            file=sys.stderr,
        )

    return problems


def qt_version_from_qmake(qmake: str) -> str:
    result = subprocess.run(
        [qmake, "-query", "QT_VERSION"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{qmake} -query QT_VERSION failed: {result.stderr.strip()}"
        )
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmake", help="path to qmake/qmake6")
    parser.add_argument("--version", help="Qt version string, instead of --qmake")
    args = parser.parse_args()

    if not args.qmake and not args.version:
        parser.error("one of --qmake or --version is required")

    try:
        raw = args.version if args.version else qt_version_from_qmake(args.qmake)
        version = parse_version(raw)
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Qt version: {'.'.join(str(part) for part in version)}")
    problems = evaluate(version, date.today())
    if not problems:
        return 0

    for problem in problems:
        print(f"ERROR: {problem}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
