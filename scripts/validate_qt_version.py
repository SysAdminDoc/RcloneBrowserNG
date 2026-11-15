#!/usr/bin/env python3
"""Check the Qt used for release packaging against the project's branch policy.

Two separate things can be wrong with a Qt build, and the release lane has to
refuse both:

1. It carries a known vulnerability the app can reach. CVE-2026-6210 (QtSvg
   type confusion and heap overflow) and CVE-2026-9499
   (QTextCodec::codecForName out-of-bounds read) are fixed in 6.8.8 and
   6.11.1. CVE-2026-95620 and CVE-2026-96432 (QNetworkAccessManager over
   HTTP/2) and CVE-2026-101201 (JSON parsing out-of-bounds read) are fixed in
   6.11.3. None of the five affects 6.12.0.
2. Its branch has stopped receiving open-source patches, so the next
   vulnerability will never reach it.

Checked against download.qt.io on 2026-09-05: the public open-source archive
carries 6.8 only up to 6.8.4, so the 6.8.8 fix is commercial-only and 6.8 LTS
is not a usable release branch for this project.

Checked 2026-11-15: 6.11's open-source window closed on 2026-09-30, and 6.12
LTS (released 2026-09-30, open-source patches until 2027-04-01) is the
branch to ship from. CVE-2026-116337 (Qt Network HTTP response memory
exhaustion) is fixed only in 6.12.1, which download.qt.io doesn't carry yet,
so 6.12.0 passes with a warning until it does.

Run standalone or import BRANCHES/evaluate from a test.
"""

from __future__ import annotations

import argparse
from datetime import date
import re
import subprocess
import sys


# Per-branch policy. `patched_from` is the first patch on that branch carrying
# the fix for every CVE in CVES, or None when no such patch was ever published
# openly. `oss_support_ends` is when open-source patches stop, from
# endoflife.date. `pending_fix`, when present, is a (version, CVE) pair for a
# vulnerability whose fix hasn't been published openly yet: builds below that
# version pass with a warning, since there is nothing newer to move to.
BRANCHES = {
    (6, 5): {"patched_from": None, "oss_support_ends": date(2023, 10, 9)},
    (6, 6): {"patched_from": None, "oss_support_ends": date(2024, 4, 2)},
    (6, 7): {"patched_from": None, "oss_support_ends": date(2024, 10, 7)},
    # LTS, but its open-source window closed and the archive stops at 6.8.4,
    # below the 6.8.8 that carries the fixes.
    (6, 8): {"patched_from": None, "oss_support_ends": date(2025, 4, 2)},
    (6, 9): {"patched_from": None, "oss_support_ends": date(2025, 10, 7)},
    (6, 10): {"patched_from": None, "oss_support_ends": date(2026, 4, 7)},
    (6, 11): {"patched_from": (6, 11, 3), "oss_support_ends": date(2026, 9, 30)},
    (6, 12): {
        "patched_from": (6, 12, 0),
        "oss_support_ends": date(2027, 4, 1),
        "pending_fix": (
            (6, 12, 1),
            "CVE-2026-116337 (Qt Network HTTP response memory exhaustion)",
        ),
    },
}

# The branch the project ships from today, and when that choice must be
# revisited. Keep in step with SECURITY.md.
RECOMMENDED = "6.12.0"
REVISIT_ON = date(2027, 4, 1)

CVES = (
    "CVE-2026-6210, CVE-2026-9499, CVE-2026-95620, CVE-2026-96432, "
    "CVE-2026-101201"
)


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
            f"Qt {major}.{minor}.{patch} is in the vulnerable range of at "
            f"least one of {CVES}. Use {wanted} or newer on this branch."
        )

    pending = branch.get("pending_fix")
    if pending is not None and version < pending[0]:
        fixed_in = ".".join(str(part) for part in pending[0])
        print(
            f"WARNING: Qt {major}.{minor}.{patch} is still affected by "
            f"{pending[1]}. The fix is in {fixed_in}, which wasn't published "
            "openly when this table was last checked. Rebuild with it as "
            "soon as it is, and update scripts/validate_qt_version.py.",
            file=sys.stderr,
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
