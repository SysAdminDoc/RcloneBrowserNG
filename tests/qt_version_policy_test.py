#!/usr/bin/env python3
"""Pin the Qt branch policy enforced by scripts/validate_qt_version.py.

The gate used to live only in the Windows lane, accepted "6.11.1 or newer"
with no upper bound, and knew nothing about a branch falling out of
open-source support. Both halves matter: a Qt can be unpatched today, or it
can be patched today and unpatchable tomorrow.
"""

from __future__ import annotations

import contextlib
import importlib.util
from datetime import date
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "scripts" / "validate_qt_version.py"

spec = importlib.util.spec_from_file_location("validate_qt_version", VALIDATOR)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)
builder = validator.patch_builder()

# A day comfortably inside the 6.11 open-source window, so the CVE half of
# the policy can be tested without the support-date half firing.
IN_SUPPORT = date(2026, 6, 1)

# The day 6.12 became the release branch. 6.11 is past its window by then.
MOVED_TO_612 = date(2026, 11, 15)

# What a qtbase 6.12.0 built by scripts/build_patched_qt.py carries. Written
# out rather than read from the table, so a CVE dropped from the table shows
# up here.
PATCHED_6120 = frozenset({"CVE-2026-116337", "CVE-2026-105943"})


class QtVersionPolicyTest(unittest.TestCase):
    def reasons(
        self,
        version: str,
        today: date = IN_SUPPORT,
        patched: frozenset[str] = frozenset(),
    ) -> list[str]:
        return validator.evaluate(validator.parse_version(version), today, patched)

    def test_accepts_the_recommended_build(self):
        # 6.12.0 is the recommended version, but only as built with the
        # qtbase source diffs. The stock binaries are refused below.
        self.assertEqual(self.reasons(validator.RECOMMENDED, patched=PATCHED_6120), [])

    def test_rejects_the_cve_vulnerable_range(self):
        # CVE-2026-6210 and CVE-2026-9499 are fixed in 6.11.1, the HTTP/2
        # ones in 6.11.3, so 6.11.0 and 6.11.2 are both in range.
        for version in ("6.11.0", "6.11.2"):
            with self.subTest(version=version):
                problems = self.reasons(version)
                self.assertTrue(problems)
                self.assertIn("vulnerable range", " ".join(problems))
        self.assertEqual(self.reasons("6.11.3"), [])

    def test_accepts_612_and_refuses_611_on_the_move_date(self):
        # The acceptance line of the move: every 6.12 passes (6.12.0 as
        # built with the source diffs) and every 6.11 is refused, the last
        # patched one included.
        self.assertEqual(self.reasons("6.12.0", MOVED_TO_612, PATCHED_6120), [])
        for version in ("6.12.1", "6.12.4"):
            with self.subTest(version=version):
                self.assertEqual(self.reasons(version, MOVED_TO_612), [])
        for version in ("6.11.0", "6.11.2", "6.11.3"):
            with self.subTest(version=version):
                self.assertTrue(self.reasons(version, MOVED_TO_612))

    def test_refuses_6120_without_both_source_diffs(self):
        # CVE-2026-116337 and CVE-2026-105943 have 6.12 source diffs but no
        # binary before 6.12.1, which download.qt.io did not carry on
        # 2026-11-15. The stock 6.12.0 used to pass with a warning. Now it
        # is refused unless it was built with the diffs for both.
        problems = " ".join(self.reasons("6.12.0", MOVED_TO_612))
        self.assertIn("CVE-2026-116337", problems)
        self.assertIn("CVE-2026-105943", problems)
        self.assertIn("build_patched_qt.py", problems)

        half = self.reasons("6.12.0", MOVED_TO_612, frozenset({"CVE-2026-116337"}))
        self.assertTrue(half)
        self.assertIn("lacks the fix for CVE-2026-105943.", " ".join(half))

        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(self.reasons("6.12.0", MOVED_TO_612, PATCHED_6120), [])
        self.assertIn("CVE-2026-116337", stderr.getvalue())
        self.assertIn("CVE-2026-105943", stderr.getvalue())

        # 6.12.1 carries both fixes as shipped and needs no record.
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(self.reasons("6.12.1", MOVED_TO_612), [])
        self.assertNotIn("CVE-2026-116337", stderr.getvalue())

    def test_the_patch_script_fixes_what_the_table_waits_on(self):
        # The validator asks for the record to cover the pending CVEs, and
        # the script records what it pins. If the two drift, a correctly
        # built Qt is refused or a partly patched one passes.
        pending = validator.BRANCHES[(6, 12)]["pending_fix"]
        self.assertEqual(set(pending["cves"]), PATCHED_6120)
        self.assertEqual({patch["cve"] for patch in builder.PATCHES}, PATCHED_6120)
        self.assertEqual(builder.QT_VERSION, validator.RECOMMENDED)
        self.assertLess(
            validator.parse_version(builder.QT_VERSION), pending["fixed_in"]
        )

    def write_record(self, prefix: Path, **changes) -> None:
        builder.write_record(prefix)
        path = prefix / builder.RECORD_NAME
        record = json.loads(path.read_text(encoding="utf-8"))
        record.update(changes)
        path.write_text(json.dumps(record), encoding="utf-8")

    def test_reads_the_record_the_patch_script_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp)
            self.assertEqual(validator.patched_cves(prefix, (6, 12, 0)), frozenset())
            builder.write_record(prefix)
            self.assertEqual(validator.patched_cves(prefix, (6, 12, 0)), PATCHED_6120)

    def test_a_record_that_does_not_match_covers_nothing(self):
        both_diffs = [dict(patch) for patch in builder.PATCHES]
        cases = {
            "another Qt version": {"qt_version": "6.12.1"},
            "another module": {"module": "qtnetworkauth"},
            "another source archive": {"source": {"file": "x", "sha256": "0" * 64}},
            "no patches": {"patches": []},
        }
        for label, changes in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                self.write_record(Path(tmp), **changes)
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(
                        validator.patched_cves(Path(tmp), (6, 12, 0)), frozenset()
                    )

        # CVE-2026-116337 needs both of its diffs. Listing one, or listing it
        # with a different hash, doesn't count.
        only_0001 = [p for p in both_diffs if not p["file"].endswith("-0002.diff")]
        tampered = [dict(p) for p in both_diffs]
        tampered[0]["sha256"] = "f" * 64
        for label, patches in (("one diff", only_0001), ("wrong hash", tampered)):
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                self.write_record(Path(tmp), patches=patches)
                self.assertEqual(
                    validator.patched_cves(Path(tmp), (6, 12, 0)),
                    frozenset({"CVE-2026-105943"}),
                )

    def test_a_damaged_record_is_an_error(self):
        for content in ("{not json", "[1, 2]"):
            with self.subTest(content=content), tempfile.TemporaryDirectory() as tmp:
                (Path(tmp) / builder.RECORD_NAME).write_text(content, encoding="utf-8")
                with self.assertRaises(ValueError):
                    validator.patched_cves(Path(tmp), (6, 12, 0))

    def test_rejects_qt_68_because_the_fix_is_commercial_only(self):
        # Checked against download.qt.io on 2026-11-15: the public archive
        # stops at 6.8.5, below the 6.8.8 that carries the fixes. The old
        # gate accepted "6.8.8+" and would have waved through a build nobody
        # running open-source Qt can actually produce.
        for version in ("6.8.0", "6.8.4", "6.8.5", "6.8.8"):
            with self.subTest(version=version):
                problems = self.reasons(version)
                self.assertTrue(problems, f"{version} must be refused")

    def test_rejects_branches_out_of_open_source_support(self):
        # 6.9 and 6.10 are both past their dates even on the early test date.
        for version in ("6.9.3", "6.10.3"):
            with self.subTest(version=version):
                problems = " ".join(self.reasons(version))
                self.assertIn("open-source", problems)

    def test_rejects_a_branch_once_its_window_closes(self):
        # A patched build on its branch's last supported day passes, and the
        # same build a day later is refused, so the release lane cannot
        # quietly keep shipping from a branch nobody patches any more.
        # endoflife.date ends 6.11 on 2026-09-30. 6.11.3 came out after that,
        # on 2026-10-26, as the branch's last release; the dates below test
        # the rule, not the release history.
        self.assertEqual(self.reasons("6.11.3", date(2026, 9, 30)), [])
        problems = " ".join(self.reasons("6.11.3", date(2026, 10, 1)))
        self.assertIn("open-source patches", problems)
        self.assertIn("SECURITY.md", problems)
        # The same rule refuses 6.12 after 2027-04-01, the date SECURITY.md
        # gives, and not a day sooner.
        self.assertEqual(self.reasons("6.12.1", date(2027, 4, 1)), [])
        self.assertTrue(self.reasons("6.12.1", date(2027, 4, 2)))

    def test_allows_a_branch_newer_than_the_table(self):
        # 6.12 is in the table now, so 6.13 stands in for the next branch.
        # The gate must not block it on silence; it notes it and asks for
        # the table to be updated. The note has to name the fixes still
        # waiting on a binary too, or a 6.13 build would never be checked
        # for them.
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(self.reasons("6.13.0", date(2027, 3, 1)), [])
        for cve in ("CVE-2026-6210", "CVE-2026-116337", "CVE-2026-105943"):
            with self.subTest(cve=cve):
                self.assertIn(cve, stderr.getvalue())

    def test_rejects_qt5(self):
        problems = " ".join(self.reasons("5.15.17"))
        self.assertIn("requires Qt 6", problems)

    def test_names_every_tracked_cve(self):
        for cve in (
            "CVE-2026-6210",
            "CVE-2026-9499",
            "CVE-2026-95620",
            "CVE-2026-96432",
        ):
            with self.subTest(cve=cve):
                self.assertIn(cve, validator.CVES)
        for cve in ("CVE-2026-116337", "CVE-2026-105943"):
            with self.subTest(cve=cve):
                self.assertIn(cve, validator.pending_cves())

    def test_parse_version_reads_a_qmake_style_string(self):
        self.assertEqual(validator.parse_version("6.11.2"), (6, 11, 2))
        self.assertEqual(validator.parse_version("  6.8.4\n"), (6, 8, 4))
        with self.assertRaises(ValueError):
            validator.parse_version("not a version")

    def test_command_line_exits_non_zero_for_a_refused_version(self):
        def run(*args: str) -> subprocess.CompletedProcess:
            return subprocess.run(
                [sys.executable, str(VALIDATOR), *args],
                capture_output=True,
                text=True,
            )

        with tempfile.TemporaryDirectory() as tmp:
            builder.write_record(Path(tmp))
            good = run("--version", validator.RECOMMENDED, "--qt-prefix", tmp)
        self.assertEqual(good.returncode, 0, good.stderr)
        self.assertEqual(run("--version", "6.12.1").returncode, 0)

        for version in ("6.8.8", "6.11.2", validator.RECOMMENDED):
            with self.subTest(version=version):
                bad = run("--version", version)
                self.assertEqual(bad.returncode, 1)
                self.assertIn("ERROR", bad.stderr)

    def test_revisit_date_matches_the_shipping_branch_window(self):
        # SECURITY.md quotes this date; it has to be the same one the gate
        # will start failing on.
        branch = validator.BRANCHES[(6, 12)]
        self.assertEqual(validator.REVISIT_ON, branch["oss_support_ends"])
        # endoflife.date, checked 2026-11-15. Pinned here so moving both
        # values together can't slip past.
        self.assertEqual(validator.REVISIT_ON, date(2027, 4, 1))
        self.assertEqual(
            validator.parse_version(validator.RECOMMENDED)[:2], (6, 12)
        )


if __name__ == "__main__":
    unittest.main()
