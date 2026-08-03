#!/usr/bin/env python3
"""Contract tests for the AppImage update-information verifier."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class AppImageUpdateTest(unittest.TestCase):
    def test_accepts_expected_gh_releases_zsync_marker(self) -> None:
        root = Path(__file__).resolve().parents[1]
        verifier = root / "scripts" / "verify_appimage_update.py"
        expected = (
            "gh-releases-zsync|SysAdminDoc|RcloneBrowserNG|latest|"
            "RcloneBrowserNG-*linux-x86_64.AppImage.zsync"
        )
        with tempfile.TemporaryDirectory(prefix="appimage-update-") as temp:
            artifact = Path(temp) / "RcloneBrowserNG.AppImage"
            artifact.write_bytes(b"ELF\x02\x01\x01\n" + expected.encode("utf-8"))
            result = subprocess.run(
                [
                    sys.executable,
                    str(verifier),
                    "--artifact",
                    str(artifact),
                    "--expected",
                    expected,
                ],
                cwd=root,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_rejects_missing_update_information(self) -> None:
        root = Path(__file__).resolve().parents[1]
        verifier = root / "scripts" / "verify_appimage_update.py"
        with tempfile.TemporaryDirectory(prefix="appimage-update-") as temp:
            artifact = Path(temp) / "RcloneBrowserNG.AppImage"
            artifact.write_bytes(b"ELF\x02\x01\x01\n")
            result = subprocess.run(
                [
                    sys.executable,
                    str(verifier),
                    "--artifact",
                    str(artifact),
                    "--expected",
                    "gh-releases-zsync|SysAdminDoc|RcloneBrowserNG|latest|update.zsync",
                ],
                cwd=root,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
