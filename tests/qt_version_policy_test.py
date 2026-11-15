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
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "scripts" / "validate_qt_version.py"

spec = importlib.util.spec_from_file_location("validate_qt_version", VALIDATOR)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

# A day comfortably inside the 6.11 open-source window, so the CVE half of
# the policy can be tested without the support-date half firing.
IN_SUPPORT = date(2026, 6, 1)

# The day 6.12 became the release branch. 6.11 is past its window by then.
MOVED_TO_612 = date(2026, 11, 15)


class QtVersionPolicyTest(unittest.TestCase):
    def reasons(self, version: str, today: date = IN_SUPPORT) -> list[str]:
        return validator.evaluate(validator.parse_version(version), today)

    def test_accepts_the_recommended_build(self):
        self.assertEqual(self.reasons(validator.RECOMMENDED), [])

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
        # The acceptance line of the move: every 6.12 passes and every 6.11
        # is refused, the last patched one included.
        for version in ("6.12.0", "6.12.1", "6.12.4"):
            with self.subTest(version=version):
                self.assertEqual(self.reasons(version, MOVED_TO_612), [])
        for version in ("6.11.0", "6.11.2", "6.11.3"):
            with self.subTest(version=version):
                self.assertTrue(self.reasons(version, MOVED_TO_612))

    def test_warns_about_a_fix_that_is_not_published_yet(self):
        # CVE-2026-116337 and CVE-2026-105943 have 6.12 source diffs but no
        # binary before 6.12.1, which download.qt.io did not carry on
        # 2026-11-15. 6.12.0 passes, but the release lane says what it is
        # shipping with, and stops saying it once 6.12.1 is used.
        for version, warns in (("6.12.0", True), ("6.12.1", False)):
            with self.subTest(version=version):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    problems = self.reasons(version, MOVED_TO_612)
                self.assertEqual(problems, [])
                self.assertEqual("CVE-2026-116337" in stderr.getvalue(), warns)
                self.assertEqual("CVE-2026-105943" in stderr.getvalue(), warns)

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
        good = subprocess.run(
            [sys.executable, str(VALIDATOR), "--version", validator.RECOMMENDED],
            capture_output=True,
            text=True,
        )
        self.assertEqual(good.returncode, 0, good.stderr)

        for version in ("6.8.8", "6.11.2"):
            with self.subTest(version=version):
                bad = subprocess.run(
                    [sys.executable, str(VALIDATOR), "--version", version],
                    capture_output=True,
                    text=True,
                )
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
