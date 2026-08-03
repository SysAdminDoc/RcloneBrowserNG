#!/usr/bin/env python3
"""Contract tests for the dependency-free package manifest generator."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


class PackageManifestTest(unittest.TestCase):
    def test_generates_all_non_submitted_manifests_with_matching_hashes(self) -> None:
        root = Path(__file__).resolve().parents[1]
        script = root / "scripts" / "generate_package_manifests.py"
        with tempfile.TemporaryDirectory(prefix="rclone-manifests-") as temp:
            release = Path(temp) / "release"
            output = Path(temp) / "manifests"
            release.mkdir()
            files = {
                "windows-x64-installer": release / "RcloneBrowserNG-9.8.7-abc-windows-x64-setup.exe",
                "macos": release / "RcloneBrowserNG-9.8.7-abc-macos-arm64.dmg",
                "source": release / "RcloneBrowserNG-9.8.7-source.tar.gz",
            }
            for index, path in enumerate(files.values(), start=1):
                path.write_bytes(f"artifact-{index}".encode("ascii"))

            command = [
                sys.executable,
                str(script),
                "--version",
                "9.8.7",
                "--release-dir",
                str(release),
                "--output",
                str(output),
                "--base-url",
                "https://downloads.example.test/v{version}",
            ]
            for key, path in files.items():
                command.extend(["--artifact", f"{key}={path}"])
            result = subprocess.run(
                command,
                cwd=root,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout)

            index = json.loads((output / "manifest-index.json").read_text())
            self.assertEqual(index["packageVersion"], "9.8.7")
            self.assertFalse(index["submitted"])
            self.assertEqual(
                set(index["packages"]), {"chocolatey", "flatpak", "macos", "windows"}
            )
            for family, key in {
                "windows": "windows-x64-installer",
                "chocolatey": "windows-x64-installer",
                "macos": "macos",
                "flatpak": "source",
            }.items():
                expected_hash = hashlib.sha256(files[key].read_bytes()).hexdigest()
                self.assertEqual(index["artifacts"][family]["sha256"], expected_hash)
                for relative_path in index["packages"][family]:
                    self.assertTrue((output / relative_path).is_file())

            winget = output / "winget" / "SysAdminDoc.RcloneBrowserNG" / "9.8.7"
            installer_text = (winget / "SysAdminDoc.RcloneBrowserNG.installer.yaml").read_text()
            self.assertIn("InstallerType: inno", installer_text)
            self.assertIn("https://downloads.example.test/v9.8.7/", installer_text)

            nuspec = output / "chocolatey" / "rclone-browser-ng.nuspec"
            ET.parse(nuspec)
            self.assertIn("9.8.7", nuspec.read_text())
            self.assertIn(
                hashlib.sha256(files["windows-x64-installer"].read_bytes()).hexdigest(),
                (output / "chocolatey" / "tools" / "chocolateyinstall.ps1").read_text(),
            )

            cask = (output / "homebrew" / "rclone-browser-ng.rb").read_text()
            self.assertIn('version "9.8.7"', cask)
            self.assertIn(hashlib.sha256(files["macos"].read_bytes()).hexdigest(), cask)

            flatpak = (output / "flatpak" / "io.github.sysadmindoc.rclonebrowserng.yml").read_text()
            self.assertIn("app-id: io.github.sysadmindoc.rclonebrowserng", flatpak)
            self.assertIn(hashlib.sha256(files["source"].read_bytes()).hexdigest(), flatpak)


if __name__ == "__main__":
    unittest.main()
