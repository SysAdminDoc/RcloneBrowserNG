#!/usr/bin/env python3
"""Check the Qt used for release packaging against the project's branch policy.

Two separate things can be wrong with a Qt build, and the release lane has to
refuse both:

1. It carries a known vulnerability the app can reach. CVE-2026-6210 (QtSvg
   type confusion and heap overflow) and CVE-2026-9499
   (QTextCodec::codecForName out-of-bounds read) are fixed in 6.8.8 and
   6.11.1. CVE-2026-95620 and CVE-2026-96432 (QNetworkAccessManager over
   HTTP/2) are fixed in 6.11.3. None of the four affects 6.12.0.
2. Its branch has stopped receiving open-source patches, so the next
   vulnerability will never reach it.

Checked against download.qt.io on 2026-11-15: the public open-source archive
carries 6.8 only up to 6.8.5, so the 6.8.8 fix is commercial-only and 6.8 LTS
is not a usable release branch for this project.

Checked 2026-11-15: endoflife.date ends 6.11's support on 2026-09-30, and
6.11.3 (2026-10-26) was its last release. 6.12 LTS (released 2026-09-30,
open-source patches until 2027-04-01) is the branch to ship from. Two qtbase
fixes the app reaches have no 6.12 binary yet: CVE-2026-116337 (memory
exhaustion from an HTTP reply's status line or headers) and CVE-2026-105943
(NTLM single sign-on on Windows without the server's SPN). They ship in
6.12.1, and download.qt.io publishes both as 6.12 source diffs until then.
A 6.12.0 passes only when scripts/build_patched_qt.py built it with those
diffs, which it records in qt-security-patches.json at the Qt prefix.

Not tracked, because the app can't reach them: CVE-2026-101201 (QBinaryJson
in Qt5Compat, which the app doesn't link) and CVE-2026-105924 (CBOR to JSON
conversion; the app reads no CBOR).

Run standalone or import BRANCHES/evaluate from a test.
"""

from __future__ import annotations

import argparse
from datetime import date
import importlib.util
from pathlib import Path
import re
import subprocess
import sys


# Per-branch policy. `patched_from` is the first patch on that branch carrying
# the fix for every CVE in CVES, or None when no such patch was ever published
# openly. `oss_support_ends` is when open-source patches stop, from
# endoflife.date. `pending_fix`, when present, covers vulnerabilities whose
# fixed binary release isn't out yet: builds below `fixed_in` are refused
# unless their Qt prefix records a build with the source diffs for every CVE
# in `cves`.
BRANCHES = {
    (6, 5): {"patched_from": None, "oss_support_ends": date(2023, 10, 9)},
    (6, 6): {"patched_from": None, "oss_support_ends": date(2024, 4, 2)},
    (6, 7): {"patched_from": None, "oss_support_ends": date(2024, 10, 7)},
    # LTS, but its open-source window closed and the archive stops at 6.8.5,
    # below the 6.8.8 that carries the fixes.
    (6, 8): {"patched_from": None, "oss_support_ends": date(2025, 4, 2)},
    (6, 9): {"patched_from": None, "oss_support_ends": date(2025, 10, 7)},
    (6, 10): {"patched_from": None, "oss_support_ends": date(2026, 4, 7)},
    (6, 11): {"patched_from": (6, 11, 3), "oss_support_ends": date(2026, 9, 30)},
    (6, 12): {
        "patched_from": (6, 12, 0),
        "oss_support_ends": date(2027, 4, 1),
        "pending_fix": {
            "fixed_in": (6, 12, 1),
            "cves": ("CVE-2026-116337", "CVE-2026-105943"),
            "summary": (
                "CVE-2026-116337 (memory exhaustion from an HTTP reply's "
                "status line or headers) and CVE-2026-105943 (NTLM single "
                "sign-on on Windows without the server's SPN)"
            ),
        },
    },
}

# The branch the project ships from today, and when that choice must be
# revisited. Keep in step with SECURITY.md. Until 6.12.1 ships, this version
# passes only as built by scripts/build_patched_qt.py.
RECOMMENDED = "6.12.0"
REVISIT_ON = date(2027, 4, 1)

CVES = "CVE-2026-6210, CVE-2026-9499, CVE-2026-95620, CVE-2026-96432"

PATCH_BUILDER = Path(__file__).resolve().parent / "build_patched_qt.py"


def pending_cves() -> str:
    """Every vulnerability the table still waits on a binary release for."""
    return "; ".join(
        branch["pending_fix"]["summary"]
        for branch in BRANCHES.values()
        if "pending_fix" in branch
    )


def patch_builder():
    """scripts/build_patched_qt.py, which owns the record format."""
    spec = importlib.util.spec_from_file_location("build_patched_qt", PATCH_BUILDER)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"can't load {PATCH_BUILDER}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def patched_cves(prefix: Path, version: tuple[int, int, int]) -> frozenset[str]:
    """The CVEs the record in a Qt prefix shows were patched in.

    Raises ValueError for a record that is there but damaged.
    """
    builder = patch_builder()
    record = builder.read_record(prefix)
    if record is None:
        return frozenset()
    text = ".".join(str(part) for part in version)
    covered = frozenset(builder.covered_cves(record, text))
    if not covered:
        print(
            f"NOTE: {prefix / builder.RECORD_NAME} lists none of the diffs "
            f"scripts/build_patched_qt.py pins for Qt {text}, so it is ignored.",
            file=sys.stderr,
        )
    return covered


def parse_version(text: str) -> tuple[int, int, int]:
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", text.strip())
    if not match:
        raise ValueError(f"could not read a Qt version from {text!r}")
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)))


def evaluate(
    version: tuple[int, int, int],
    today: date,
    patched: frozenset[str] = frozenset(),
) -> list[str]:
    """Return the reasons this Qt must not be used for a release, if any.

    `patched` is the set of CVEs a qt-security-patches.json record shows the
    build was patched for (see patched_cves).
    """
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
            pending = pending_cves()
            also = f", and for {pending}" if pending else ""
            print(
                f"NOTE: Qt {major}.{minor} is newer than this policy table. "
                f"Confirm it carries the fixes for {CVES}{also}, then add it "
                "to scripts/validate_qt_version.py.",
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
    if pending is not None and version < pending["fixed_in"]:
        fixed_in = ".".join(str(part) for part in pending["fixed_in"])
        missing = [cve for cve in pending["cves"] if cve not in patched]
        if missing:
            problems.append(
                f"Qt {major}.{minor}.{patch} is still affected by "
                f"{pending['summary']}. This build lacks the fix for "
                f"{', '.join(missing)}. Use {fixed_in} or newer, or a qtbase "
                "built with the published source diffs by "
                "scripts/build_patched_qt.py."
            )
        else:
            print(
                f"NOTE: Qt {major}.{minor}.{patch} was built with the qtbase "
                f"source diffs for {', '.join(pending['cves'])}. Move to "
                f"{fixed_in} once it ships.",
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


def qmake_query(qmake: str, name: str) -> str:
    result = subprocess.run(
        [qmake, "-query", name],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{qmake} -query {name} failed: {result.stderr.strip()}"
        )
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmake", help="path to qmake/qmake6")
    parser.add_argument("--version", help="Qt version string, instead of --qmake")
    parser.add_argument(
        "--qt-prefix",
        help="Qt install prefix to read qt-security-patches.json from "
        "(defaults to qmake's QT_INSTALL_PREFIX)",
    )
    args = parser.parse_args()

    if not args.qmake and not args.version:
        parser.error("one of --qmake or --version is required")

    try:
        raw = args.version if args.version else qmake_query(args.qmake, "QT_VERSION")
        version = parse_version(raw)
        prefix = args.qt_prefix
        if prefix is None and args.qmake:
            prefix = qmake_query(args.qmake, "QT_INSTALL_PREFIX")
        patched = patched_cves(Path(prefix), version) if prefix else frozenset()
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"Qt version: {'.'.join(str(part) for part in version)}")
    if patched:
        print(f"Patched with source diffs for: {', '.join(sorted(patched))}")
    problems = evaluate(version, date.today(), patched)
    if not problems:
        return 0

    for problem in problems:
        print(f"ERROR: {problem}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
