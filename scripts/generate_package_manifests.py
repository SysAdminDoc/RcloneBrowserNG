#!/usr/bin/env python3
"""Generate local, non-submitted package-manager manifests.

The release scripts produce platform artifacts but do not publish them. This
tool turns those artifacts into reviewable Winget, Homebrew Cask, Chocolatey,
and Flatpak manifest files while carrying the exact artifact URL and SHA256.
It deliberately has no third-party Python dependencies so it can run in the
same minimal environment as the local release readiness checks.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


PACKAGE_ID = "SysAdminDoc.RcloneBrowserNG"
PACKAGE_NAME = "Rclone Browser NG"
HOMEBREW_CASK = "rclone-browser-ng"
FLATPAK_ID = "io.github.sysadmindoc.rclonebrowserng"
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


@dataclass(frozen=True)
class Artifact:
    key: str
    path: Path | None
    url: str
    sha256: str

    @property
    def filename(self) -> str:
        if self.path is not None:
            return self.path.name
        return self.url.rsplit("/", 1)[-1]

    @property
    def suffix(self) -> str:
        return Path(self.filename).suffix.lower()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def yaml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def artifact_url(base_url: str, version: str, filename: str) -> str:
    if "{" in base_url:
        formatted = base_url.format(version=version, filename=filename).rstrip("/")
        if "{filename}" not in base_url:
            return formatted + "/" + filename
        return formatted
    return base_url.rstrip("/") + "/" + filename


def parse_artifact_specs(
    specs: list[str], root: Path, base_url: str, version: str
) -> dict[str, Artifact]:
    artifacts: dict[str, Artifact] = {}
    aliases = {
        "windows": "windows-x64",
        "win-x64": "windows-x64",
        "windows-installer": "windows-x64-installer",
        "macos": "macos",
        "darwin": "macos",
        "linux": "linux-x86_64",
        "source-archive": "source",
    }
    for raw in specs:
        key, separator, raw_path = raw.partition("=")
        if not separator or not key or not raw_path:
            raise ValueError(
                f"--artifact must use PLATFORM=PATH, got {raw!r}"
            )
        key = aliases.get(key.lower(), key.lower())
        if key not in {
            "windows-x64",
            "windows-x64-installer",
            "macos",
            "linux-x86_64",
            "linux-aarch64",
            "source",
        }:
            raise ValueError(f"unsupported artifact platform {key!r}")
        path = Path(raw_path)
        if not path.is_absolute():
            path = (root / path).resolve()
        if not path.is_file():
            raise ValueError(f"artifact does not exist: {path}")
        artifacts[key] = Artifact(
            key=key,
            path=path,
            url=artifact_url(base_url, version, path.name),
            sha256=sha256_file(path),
        )
    return artifacts


def discover_artifacts(
    release_dir: Path, base_url: str, version: str
) -> dict[str, Artifact]:
    patterns = {
        "windows-x64-installer": "*windows-x64-setup.exe",
        "windows-x64": "*windows-x64.zip",
        "macos": "*macos-*.dmg",
        "linux-x86_64": "*linux-x86_64.AppImage",
        "linux-aarch64": "*linux-aarch64.AppImage",
        "source": "*source.tar.gz",
    }
    artifacts: dict[str, Artifact] = {}
    for key, pattern in patterns.items():
        matches = sorted(release_dir.glob(pattern))
        if not matches:
            continue
        path = matches[-1]
        artifacts[key] = Artifact(
            key=key,
            path=path,
            url=artifact_url(base_url, version, path.name),
            sha256=sha256_file(path),
        )
    return artifacts


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.rstrip() + "\n", encoding="utf-8", newline="\n")


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def generate_winget(output: Path, version: str, artifact: Artifact) -> list[Path]:
    directory = output / "winget" / PACKAGE_ID / version
    installer_type = "inno" if artifact.suffix == ".exe" else "zip"
    installer_lines = [
        f"PackageIdentifier: {PACKAGE_ID}",
        f"PackageVersion: {version}",
        "Installers:",
        f"- Architecture: x64",
        f"  InstallerType: {installer_type}",
        f"  InstallerUrl: {yaml_string(artifact.url)}",
        f"  InstallerSha256: {artifact.sha256.upper()}",
    ]
    if installer_type == "inno":
        installer_lines.extend(
            [
                "  InstallerSwitches:",
                '    Silent: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"',
                '    SilentWithProgress: "/SILENT /SUPPRESSMSGBOXES /NORESTART"',
            ]
        )
    else:
        installer_lines.extend(
            [
                "  NestedInstallerType: portable",
                "  NestedInstallerFiles:",
                "  - RelativeFilePath: RcloneBrowser.exe",
                "    PortableCommandAlias: rclone-browser",
            ]
        )
    installer_lines.extend(["ManifestType: installer", "ManifestVersion: 1.6.0"])
    locale = f"""PackageIdentifier: {PACKAGE_ID}
PackageVersion: {version}
PackageLocale: en-US
Publisher: SysAdminDoc
PublisherUrl: https://github.com/SysAdminDoc
PackageName: {PACKAGE_NAME}
PackageUrl: https://github.com/SysAdminDoc/RcloneBrowserNG
License: MIT
LicenseUrl: https://github.com/SysAdminDoc/RcloneBrowserNG/blob/main/LICENSE
ShortDescription: Native Qt desktop browser for rclone remotes
Description: Rclone Browser NG is a native Qt desktop GUI for browsing, transferring, mounting, and scheduling rclone remotes.
Tags:
- rclone
- cloud
- backup
- file-manager
ManifestType: defaultLocale
ManifestVersion: 1.6.0
"""
    default = f"""PackageIdentifier: {PACKAGE_ID}
PackageVersion: {version}
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
"""
    paths = [
        directory / f"{PACKAGE_ID}.installer.yaml",
        directory / f"{PACKAGE_ID}.locale.en-US.yaml",
        directory / f"{PACKAGE_ID}.yaml",
    ]
    write_text(paths[0], "\n".join(installer_lines))
    write_text(paths[1], locale)
    write_text(paths[2], default)
    return paths


def generate_homebrew(output: Path, version: str, artifact: Artifact) -> list[Path]:
    path = output / "homebrew" / f"{HOMEBREW_CASK}.rb"
    content = f'''cask "{HOMEBREW_CASK}" do
  version "{version}"
  sha256 "{artifact.sha256}"

  url "{artifact.url}", verified: "github.com/SysAdminDoc/RcloneBrowserNG/"
  name "{PACKAGE_NAME}"
  desc "Native Qt desktop browser for rclone remotes"
  homepage "https://github.com/SysAdminDoc/RcloneBrowserNG"

  app "rclone-browser.app"
end
'''
    write_text(path, content)
    return [path]


def generate_chocolatey(output: Path, version: str, artifact: Artifact) -> list[Path]:
    directory = output / "chocolatey"
    nuspec = directory / f"{HOMEBREW_CASK}.nuspec"
    install = directory / "tools" / "chocolateyinstall.ps1"
    package_url = "https://github.com/SysAdminDoc/RcloneBrowserNG"
    nuspec_content = f'''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd">
  <metadata>
    <id>{HOMEBREW_CASK}</id>
    <version>{version}</version>
    <title>{PACKAGE_NAME}</title>
    <authors>SysAdminDoc</authors>
    <owners>SysAdminDoc</owners>
    <licenseUrl>{package_url}/blob/main/LICENSE</licenseUrl>
    <projectUrl>{package_url}</projectUrl>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <description>Native Qt desktop browser for rclone remotes.</description>
    <tags>rclone cloud backup file-manager</tags>
  </metadata>
  <files>
    <file src="tools\\**" target="tools" />
  </files>
</package>
'''
    write_text(nuspec, nuspec_content)
    if artifact.suffix == ".exe":
        install_content = f'''$ErrorActionPreference = 'Stop'
$packageName = '{HOMEBREW_CASK}'
$url64 = '{artifact.url}'
$checksum64 = '{artifact.sha256}'

Install-ChocolateyPackage `
  -PackageName $packageName `
  -FileType 'exe' `
  -SilentArgs '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' `
  -Url64bit $url64 `
  -Checksum64 $checksum64 `
  -ChecksumType64 'sha256'
'''
    else:
        install_content = f'''$ErrorActionPreference = 'Stop'
$packageName = '{HOMEBREW_CASK}'
$url64 = '{artifact.url}'
$checksum64 = '{artifact.sha256}'

Install-ChocolateyZipPackage `
  -PackageName $packageName `
  -Url64bit $url64 `
  -UnzipLocation $env:ChocolateyToolsLocation `
  -Checksum64 $checksum64 `
  -ChecksumType64 'sha256'
'''
    write_text(install, install_content)
    return [nuspec, install]


def generate_flatpak(output: Path, version: str, artifact: Artifact) -> list[Path]:
    path = output / "flatpak" / f"{FLATPAK_ID}.yml"
    content = f'''app-id: {FLATPAK_ID}
runtime: org.kde.Platform
runtime-version: "6.8"
sdk: org.kde.Sdk
command: rclone-browser
finish-args:
  - --share=ipc
  - --socket=fallback-x11
  - --socket=wayland
  - --filesystem=home
modules:
  - name: rclone-browser-ng
    buildsystem: cmake-ninja
    config-opts:
      - -DCMAKE_BUILD_TYPE=Release
      - -DCMAKE_INSTALL_PREFIX=/app
    sources:
      - type: archive
        url: {yaml_string(artifact.url)}
        sha256: {artifact.sha256}
'''
    write_text(path, content)
    return [path]


def validate_yaml_fields(path: Path, fields: list[str]) -> None:
    content = path.read_text(encoding="utf-8")
    for field in fields:
        if not re.search(rf"(?m)^{re.escape(field)}:", content):
            raise ValueError(f"{path.name} is missing YAML field {field}")


def validate_outputs(
    output: Path,
    version: str,
    emitted: dict[str, list[Path]],
    artifacts: dict[str, Artifact],
) -> None:
    if "windows" in emitted:
        winget = emitted["windows"]
        validate_yaml_fields(winget[0], ["PackageIdentifier", "PackageVersion", "Installers"])
        validate_yaml_fields(winget[1], ["PackageLocale", "ShortDescription"])
        installer_text = winget[0].read_text(encoding="utf-8")
        if artifacts["windows"].sha256.upper() not in installer_text:
            raise ValueError("Winget installer SHA256 does not match the artifact")
        if version not in installer_text:
            raise ValueError("Winget installer version does not match VERSION")
    if "macos" in emitted:
        cask_text = emitted["macos"][0].read_text(encoding="utf-8")
        if f'version "{version}"' not in cask_text:
            raise ValueError("Homebrew cask version does not match VERSION")
        if artifacts["macos"].sha256 not in cask_text:
            raise ValueError("Homebrew cask SHA256 does not match the artifact")
    if "chocolatey" in emitted:
        root = ET.parse(emitted["chocolatey"][0]).getroot()
        namespace = "{http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd}"
        version_node = root.find(f"{namespace}metadata/{namespace}version")
        if version_node is None or version_node.text != version:
            raise ValueError("Chocolatey nuspec version does not match VERSION")
        install_text = emitted["chocolatey"][1].read_text(encoding="utf-8")
        if artifacts["chocolatey"].sha256 not in install_text:
            raise ValueError("Chocolatey checksum does not match the artifact")
    if "flatpak" in emitted:
        flatpak_text = emitted["flatpak"][0].read_text(encoding="utf-8")
        for field in ("app-id:", "runtime:", "sources:", "sha256:"):
            if field not in flatpak_text:
                raise ValueError(f"Flatpak manifest is missing {field}")
        if artifacts["flatpak"].sha256 not in flatpak_text:
            raise ValueError("Flatpak source SHA256 does not match the artifact")


def generate(
    version: str,
    output: Path,
    artifacts: dict[str, Artifact],
    only: set[str],
) -> dict[str, list[Path]]:
    emitted: dict[str, list[Path]] = {}
    if "windows" in only:
        windows = artifacts.get("windows-x64-installer") or artifacts.get("windows-x64")
        if windows:
            artifacts["windows"] = windows
            artifacts["chocolatey"] = windows
            emitted["windows"] = generate_winget(output, version, windows)
            emitted["chocolatey"] = generate_chocolatey(output, version, windows)
        else:
            print("[SKIP] Windows manifests (no windows-x64 artifact)")
    if "macos" in only:
        macos = artifacts.get("macos")
        if macos:
            emitted["macos"] = generate_homebrew(output, version, macos)
        else:
            print("[SKIP] Homebrew cask (no macOS artifact)")
    if "flatpak" in only:
        source = artifacts.get("source")
        if source:
            artifacts["flatpak"] = source
            emitted["flatpak"] = generate_flatpak(output, version, source)
        else:
            print("[SKIP] Flatpak manifest (a source archive is required)")
    validate_outputs(output, version, emitted, artifacts)
    return emitted


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate and validate local package-manager manifests."
    )
    parser.add_argument("--release-dir", type=Path, default=Path("release"))
    parser.add_argument("--output", type=Path, default=Path("release/package-manifests"))
    parser.add_argument("--version", help="numeric package version; defaults to VERSION")
    parser.add_argument(
        "--base-url",
        default="https://github.com/SysAdminDoc/RcloneBrowserNG/releases/download/v{version}",
        help="artifact base URL; may contain {version} and {filename}",
    )
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        metavar="PLATFORM=PATH",
        help="override discovery; repeat for windows-x64, macos, or source",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=["windows", "macos", "flatpak"],
        help="generate only the selected package family (repeatable)",
    )
    parser.add_argument(
        "--require",
        action="append",
        choices=["windows", "macos", "flatpak"],
        help="fail if the selected package family has no source artifact",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    version = args.version or (root / "VERSION").read_text(encoding="utf-8").strip()
    if not VERSION_RE.fullmatch(version):
        print(f"ERROR: package version must be MAJOR.MINOR.PATCH, got {version!r}", file=sys.stderr)
        return 2
    release_dir = args.release_dir
    if not release_dir.is_absolute():
        release_dir = (root / release_dir).resolve()
    output = args.output
    if not output.is_absolute():
        output = (root / output).resolve()

    try:
        artifacts = discover_artifacts(release_dir, args.base_url, version)
        artifacts.update(
            parse_artifact_specs(args.artifact, root, args.base_url, version)
        )
        only = set(args.only or ["windows", "macos", "flatpak"])
        emitted = generate(version, output, artifacts, only)
        required = set(args.require or [])
        missing = sorted(required - set(emitted))
        if missing:
            raise ValueError("required package families were not generated: " + ", ".join(missing))
        if not emitted:
            raise ValueError("no package manifests were generated")
    except (OSError, ValueError, ET.ParseError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    index = {
        "packageVersion": version,
        "generatedBy": "scripts/generate_package_manifests.py",
        "submitted": False,
        "packages": {
            family: [str(path.relative_to(output)).replace("\\", "/") for path in paths]
            for family, paths in sorted(emitted.items())
        },
        "artifacts": {
            family: {
                "filename": artifact.filename,
                "url": artifact.url,
                "sha256": artifact.sha256,
            }
            for family, artifact in sorted(
                {
                    "windows": artifacts.get("windows-x64-installer")
                    or artifacts.get("windows-x64"),
                    "chocolatey": artifacts.get("chocolatey"),
                    "macos": artifacts.get("macos"),
                    "flatpak": artifacts.get("source"),
                }.items()
            )
            if artifact is not None and family in emitted
        },
    }
    write_json(output / "manifest-index.json", index)
    for family, paths in sorted(emitted.items()):
        print(f"[PASS] {family}: {len(paths)} manifest file(s)")
    print(f"Generated non-submitted manifests under {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
