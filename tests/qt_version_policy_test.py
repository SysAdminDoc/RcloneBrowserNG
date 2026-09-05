#!/usr/bin/env python3
"""Pin the Qt branch policy enforced by scripts/validate_qt_version.py.

The gate used to live only in the Windows lane, accepted "6.11.1 or newer"
with no upper bound, and knew nothing about a branch falling out of
open-source support. Both halves matter: a Qt can be unpatched today, or it
can be patched today and unpatchable tomorrow.
"""

from __future__ import annotations

import importlib.util
from datetime import date
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


class QtVersionPolicyTest(unittest.TestCase):
    def reasons(self, version: str, today: date = IN_SUPPORT) -> list[str]:
        return validator.evaluate(validator.parse_version(version), today)

    def test_accepts_the_recommended_build(self):
        self.assertEqual(self.reasons(validator.RECOMMENDED), [])

    def test_rejects_the_cve_vulnerable_range(self):
        # Fixed in 6.11.1, so 6.11.0 is in range.
        problems = self.reasons("6.11.0")
        self.assertTrue(problems)
        self.assertIn("vulnerable range", " ".join(problems))

    def test_rejects_qt_68_because_the_fix_is_commercial_only(self):
        # Checked against download.qt.io on 2026-09-05: the public archive
        # stops at 6.8.4, below the 6.8.8 that carries the fixes. The old
        # gate accepted "6.8.8+" and would have waved through a build nobody
        # running open-source Qt can actually produce.
        for version in ("6.8.0", "6.8.4", "6.8.8"):
            with self.subTest(version=version):
                problems = self.reasons(version)
                self.assertTrue(problems, f"{version} must be refused")

    def test_rejects_branches_out_of_open_source_support(self):
        # 6.9 and 6.10 are both past their dates even on the early test date.
        for version in ("6.9.3", "6.10.3"):
            with self.subTest(version=version):
                problems = " ".join(self.reasons(version))
                self.assertIn("open-source", problems)

    def test_rejects_the_shipping_branch_once_its_window_closes(self):
        # The point of the item: 6.11.2 is fine today and must be refused
        # after 2026-09-22, so the release lane cannot quietly keep shipping
        # from a branch nobody patches any more.
        self.assertEqual(self.reasons("6.11.2", date(2026, 9, 22)), [])
        problems = " ".join(self.reasons("6.11.2", date(2026, 9, 23)))
        self.assertIn("open-source patches", problems)
        self.assertIn("SECURITY.md", problems)

    def test_allows_a_branch_newer_than_the_table(self):
        # 6.12 LTS is not out yet. When it lands the gate must not block it
        # on silence; it warns and asks for the table to be updated.
        self.assertEqual(self.reasons("6.12.0", date(2026, 12, 1)), [])

    def test_rejects_qt5(self):
        problems = " ".join(self.reasons("5.15.17"))
        self.assertIn("requires Qt 6", problems)

    def test_names_both_cves(self):
        self.assertIn("CVE-2026-6210", validator.CVES)
        self.assertIn("CVE-2026-9499", validator.CVES)

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

        bad = subprocess.run(
            [sys.executable, str(VALIDATOR), "--version", "6.8.8"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(bad.returncode, 1)
        self.assertIn("ERROR", bad.stderr)

    def test_revisit_date_matches_the_shipping_branch_window(self):
        # SECURITY.md quotes this date; it has to be the same one the gate
        # will start failing on.
        branch = validator.BRANCHES[(6, 11)]
        self.assertEqual(validator.REVISIT_ON, branch["oss_support_ends"])
        self.assertEqual(
            validator.parse_version(validator.RECOMMENDED)[:2], (6, 11)
        )


if __name__ == "__main__":
    unittest.main()
