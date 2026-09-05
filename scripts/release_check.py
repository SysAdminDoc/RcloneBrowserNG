#!/usr/bin/env python3
"""Run the release checks that are available in the local-only release lane."""

from __future__ import annotations

import argparse
import hashlib
from datetime import date
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


class Report:
    def __init__(self) -> None:
        self.failed = 0
        self.skipped = 0

    def passed(self, label: str) -> None:
        print(f"[PASS] {label}")

    def failed_check(self, label: str, detail: str = "") -> None:
        self.failed += 1
        print(f"[FAIL] {label}")
        if detail:
            print(detail)

    def skipped_check(self, label: str, detail: str = "") -> None:
        self.skipped += 1
        suffix = f" ({detail})" if detail else ""
        print(f"[SKIP] {label}{suffix}")


def command_text(command: list[str]) -> str:
    return subprocess.list2cmdline(command)


def run_checked(
    report: Report,
    label: str,
    command: list[str],
    root: Path,
    env: dict[str, str] | None = None,
    timeout: int = 900,
) -> bool:
    try:
        result = subprocess.run(
            command,
            cwd=root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError:
        report.failed_check(label, f"missing executable: {command[0]}")
        return False
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or "")
        report.failed_check(
            label,
            f"timed out after {timeout}s\n{output[-4000:]}",
        )
        return False

    if result.returncode != 0:
        output = result.stdout or ""
        report.failed_check(
            label,
            f"exit {result.returncode}: {command_text(command)}\n"
            f"{output[-4000:]}",
        )
        return False

    report.passed(label)
    return True


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def check_source_contract(report: Report, root: Path) -> str | None:
    version_path = root / "VERSION"
    if not version_path.is_file():
        report.failed_check("source version contract", "VERSION is missing")
        return None

    version = read_text(version_path).strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        report.failed_check(
            "source version contract",
            f"VERSION must be numeric MAJOR.MINOR.PATCH, got {version!r}",
        )
        return None

    readme = root / "README.md"
    changelog = root / "CHANGELOG.md"
    security = root / "SECURITY.md"
    required = [readme, changelog, security]
    missing = [str(path.relative_to(root)) for path in required if not path.is_file()]
    if missing:
        report.failed_check("release documentation contract", "missing: " + ", ".join(missing))
        return version

    readme_text = read_text(readme)
    changelog_text = read_text(changelog)
    security_text = read_text(security)
    checks = [
        (f"version-{version}-blue.svg" in readme_text, "README version badge"),
        ("## [Unreleased]" in changelog_text, "CHANGELOG Unreleased section"),
        (
            "does not provide a CI build" in readme_text,
            "README local-build disclosure",
        ),
        (
            "does not provide CI builds" in security_text,
            "SECURITY local-release disclosure",
        ),
    ]
    for ok, label in checks:
        if ok:
            report.passed(label)
        else:
            report.failed_check(label, f"does not match VERSION {version}")

    workflow_dir = root / ".github" / "workflows"
    workflows = list(workflow_dir.glob("*")) if workflow_dir.is_dir() else []
    if workflows:
        report.failed_check(
            "local-only release lane",
            ".github/workflows exists; update the release disclosures before publishing",
        )
    else:
        report.passed("local-only release lane (no GitHub Actions workflows)")

    return version


def check_metadata(report: Report, root: Path) -> None:
    metainfo = root / "assets" / "io.github.sysadmindoc.rclonebrowserng.metainfo.xml"
    if not metainfo.is_file():
        report.failed_check("AppStream metadata parse", "metainfo file is missing")
        return
    try:
        document = ET.parse(metainfo)
    except (ET.ParseError, OSError) as exc:
        report.failed_check("AppStream metadata parse", str(exc))
        return
    report.passed("AppStream metadata parse")

    release_nodes = document.getroot().findall("./releases/release")
    source_version = read_text(root / "VERSION").strip()
    release_versions = [node.attrib.get("version", "") for node in release_nodes]
    release_dates = [node.attrib.get("date", "") for node in release_nodes]
    if len(release_nodes) < 3:
        report.failed_check(
            "AppStream release history",
            "at least three release entries are required",
        )
    elif release_versions[0] != source_version:
        report.failed_check(
            "AppStream release history",
            f"newest entry {release_versions[0]!r} does not match VERSION {source_version!r}",
        )
    elif len(set(release_versions)) != len(release_versions):
        report.failed_check(
            "AppStream release history", "release versions must be unique"
        )
    elif any(not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date)
             for date in release_dates):
        report.failed_check(
            "AppStream release history", "every release entry needs an ISO date"
        )
    elif release_dates != sorted(release_dates, reverse=True):
        report.failed_check(
            "AppStream release history", "release entries must be newest-first"
        )
    else:
        report.passed("AppStream release history")

    appstreamcli = shutil.which("appstreamcli")
    if not appstreamcli:
        report.skipped_check("appstreamcli validation", "appstreamcli is not installed")
    else:
        run_checked(
            report,
            "appstreamcli validation",
            [appstreamcli, "validate", "--no-net", str(metainfo)],
            root,
            timeout=60,
        )


def check_release_scripts(report: Report, root: Path) -> None:
    shell_scripts = [
        root / "scripts" / "prepare_icons.sh",
        root / "scripts" / "release_AppImage.sh",
        root / "scripts" / "release_macOS.sh",
        root / "scripts" / "fetch_linuxdeploy_tools.sh",
    ]
    bash = shutil.which("bash")
    if bash:
        for script in shell_scripts:
            if script.is_file():
                script_arg = (
                    script.relative_to(root).as_posix()
                    if os.name == "nt"
                    else str(script)
                )
                run_checked(
                    report,
                    f"bash syntax: {script.name}",
                    [bash, "-n", script_arg],
                    root,
                    timeout=30,
                )
            else:
                report.failed_check(f"bash syntax: {script.name}", "script is missing")
    else:
        report.skipped_check("Bash release-script syntax", "bash is not installed")

    batch = root / "scripts" / "release_windows.cmd"
    if not batch.is_file():
        report.failed_check("Windows release-script contract", "release_windows.cmd is missing")
        return
    batch_text = read_text(batch).lower()
    required_markers = [
        "setlocal",
        "set /p version",
        "cmake --build",
        "windeployqt.exe",
        "if errorlevel 1",
    ]
    missing = [marker for marker in required_markers if marker not in batch_text]
    if missing:
        report.failed_check(
            "Windows release-script contract",
            "missing markers: " + ", ".join(missing),
        )
    else:
        report.passed("Windows release-script contract")

    cmd = shutil.which("cmd.exe") or shutil.which("cmd")
    if cmd:
        run_checked(
            report,
            "Windows release-script dry-run",
            [cmd, "/d", "/c", "call", str(batch), "--dry-run"],
            root,
            timeout=30,
        )
    else:
        report.skipped_check("Windows release-script dry-run", "cmd.exe is not available")

    qt_validator = root / "scripts" / "validate_qt_version.ps1"
    if not qt_validator.is_file():
        report.failed_check("Windows Qt security-floor validator", "validator is missing")
    elif "validate_qt_version.py" not in read_text(qt_validator):
        report.failed_check(
            "Windows Qt security-floor validator",
            "the Windows wrapper no longer delegates to the shared policy",
        )
    else:
        report.passed("Windows Qt security-floor validator contract")


def check_rclone_security_floor(report: Report, root: Path) -> None:
    """The app warns about old rclone builds against a hardcoded floor.

    A floor nobody revisits goes stale silently, and a stale floor is worse
    than none: the previous "1.74.3" was itself an affected version. Fail the
    release when the recorded review date has aged out.
    """
    source = root / "src" / "rclone_security_floor.cpp"
    if not source.is_file():
        report.failed_check("rclone security floor", "src/rclone_security_floor.cpp is missing")
        return

    text = read_text(source)
    version = re.search(r'kMinimumVersion\[\]\s*=\s*"([^"]+)"', text)
    reviewed = re.search(r'kReviewedDate\[\]\s*=\s*"([^"]+)"', text)
    interval = re.search(r"kReviewIntervalDays\s*=\s*(\d+)", text)
    if not version or not reviewed or not interval:
        report.failed_check(
            "rclone security floor",
            "kMinimumVersion, kReviewedDate or kReviewIntervalDays could not be read",
        )
        return

    try:
        reviewed_date = date.fromisoformat(reviewed.group(1))
    except ValueError:
        report.failed_check(
            "rclone security floor",
            f"kReviewedDate '{reviewed.group(1)}' is not an ISO-8601 date",
        )
        return

    max_age = int(interval.group(1))
    age = (date.today() - reviewed_date).days
    if age < 0:
        report.failed_check(
            "rclone security floor",
            f"kReviewedDate {reviewed_date.isoformat()} is in the future",
        )
    elif age > max_age:
        report.failed_check(
            "rclone security floor",
            f"floor {version.group(1)} was last reviewed {age} days ago "
            f"({reviewed_date.isoformat()}), over the {max_age}-day limit. "
            "Re-check https://github.com/rclone/rclone/security/advisories, then "
            "update kMinimumVersion, AdvisorySummary and kReviewedDate.",
        )
    else:
        report.passed(
            f"rclone security floor {version.group(1)} reviewed "
            f"{reviewed_date.isoformat()} ({age}d ago)"
        )


def write_release_checksums(report: Report, root: Path) -> None:
    """Emit release/SHA256SUMS over the artifacts sitting in release/.

    Nothing verifies an unsigned download without this, and the file has to be
    generated from whatever is actually there rather than maintained by hand.
    """
    release_dir = root / "release"
    if not release_dir.is_dir():
        report.skipped_check("release checksums", "release/ does not exist yet")
        return

    artifacts = sorted(
        path
        for path in release_dir.iterdir()
        if path.is_file() and path.name != "SHA256SUMS"
    )
    if not artifacts:
        report.skipped_check("release checksums", "release/ holds no artifacts")
        return

    lines = []
    for artifact in artifacts:
        digest = hashlib.sha256()
        with artifact.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        # sha256sum's own format, so `sha256sum -c SHA256SUMS` just works.
        lines.append(f"{digest.hexdigest()}  {artifact.name}")

    # LF only. sha256sum reads the filename to the end of the line, so a CRLF
    # file makes every entry fail with "No such file or directory" on the
    # trailing carriage return.
    with (release_dir / "SHA256SUMS").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    report.passed(f"release checksums written for {len(artifacts)} artifact(s)")


def check_qt_branch_policy(report: Report, root: Path) -> None:
    """The Qt branch policy is one table, enforced by every release lane.

    It used to be a Windows-only PowerShell script, so an AppImage or DMG
    could ship a Qt nobody had checked.
    """
    validator = root / "scripts" / "validate_qt_version.py"
    if not validator.is_file():
        report.failed_check("Qt branch policy", "scripts/validate_qt_version.py is missing")
        return

    lanes = {
        "scripts/release_windows.cmd": "validate_qt_version.ps1",
        "scripts/release_AppImage.sh": "validate_qt_version.py",
        "scripts/release_macOS.sh": "validate_qt_version.py",
    }
    missing = [
        lane
        for lane, needle in lanes.items()
        if not (root / lane).is_file() or needle not in read_text(root / lane)
    ]
    if missing:
        report.failed_check(
            "Qt branch policy",
            "these release lanes do not validate Qt: " + ", ".join(missing),
        )
        return

    test = root / "tests" / "qt_version_policy_test.py"
    if not test.is_file():
        report.failed_check("Qt branch policy", "tests/qt_version_policy_test.py is missing")
        return
    run_checked(
        report,
        "Qt branch policy contract",
        [sys.executable, str(test)],
        root,
        timeout=60,
    )


def check_package_manifest_generator(report: Report, root: Path) -> None:
    test = root / "tests" / "package_manifest_test.py"
    if not test.is_file():
        report.failed_check("package manifest generator test", "test file is missing")
        return
    run_checked(
        report,
        "package manifest generator contract",
        [sys.executable, str(test)],
        root,
        timeout=60,
    )


def check_appimage_update_verifier(report: Report, root: Path) -> None:
    test = root / "tests" / "appimage_update_test.py"
    if not test.is_file():
        report.failed_check("AppImage update metadata test", "test file is missing")
        return
    run_checked(
        report,
        "AppImage update metadata contract",
        [sys.executable, str(test)],
        root,
        timeout=60,
    )


def smoke_packaged_windows_binary(
    report: Report,
    root: Path,
    build_dir: Path,
    artifact: Path,
    version: str,
    env: dict[str, str],
) -> None:
    if os.name != "nt":
        report.skipped_check(
            "packaged Windows launch smoke", "Windows deployment is unavailable on this host"
        )
        return
    qt_bin = qt_bin_from_cache(build_dir)
    windeployqt = qt_bin / "windeployqt.exe" if qt_bin else None
    if not windeployqt or not windeployqt.is_file():
        report.skipped_check(
            "packaged Windows launch smoke", "windeployqt is unavailable in the selected Qt"
        )
        return
    smoke_script = root / "scripts" / "smoke_package.py"
    with tempfile.TemporaryDirectory(prefix="rclone-browser-deploy-") as temp:
        deployed = Path(temp)
        staged_binary = deployed / artifact.name
        try:
            shutil.copy2(artifact, staged_binary)
        except OSError as exc:
            report.failed_check("Windows packaged staging", str(exc))
            return
        if not run_checked(
            report,
            "Windows packaged staging",
            [
                str(windeployqt),
                "--no-translations",
                "--dir",
                str(deployed),
                str(staged_binary),
            ],
            root,
            env=env,
            timeout=180,
        ):
            return
        offscreen_plugin = qt_bin.parent / "plugins" / "platforms" / "qoffscreen.dll"
        if offscreen_plugin.is_file():
            try:
                (deployed / "platforms").mkdir(parents=True, exist_ok=True)
                shutil.copy2(offscreen_plugin, deployed / "platforms" / offscreen_plugin.name)
            except OSError as exc:
                report.failed_check("Windows packaged staging", str(exc))
                return
        else:
            report.failed_check(
                "Windows packaged staging",
                f"Qt offscreen platform plugin is missing: {offscreen_plugin}",
            )
            return
        run_checked(
            report,
            "Windows packaged --version smoke",
            [
                sys.executable,
                str(smoke_script),
                "--artifact",
                str(deployed),
                "--version",
                version,
            ],
            root,
            env=env,
            timeout=60,
        )


def qt_bin_from_cache(build_dir: Path) -> Path | None:
    candidates: list[Path] = []
    for variable in ("QT", "QT_PREFIX", "CMAKE_PREFIX_PATH"):
        value = os.environ.get(variable, "")
        candidates.extend(Path(part) for part in value.split(os.pathsep) if part)

    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if "CMAKE_PREFIX_PATH" in line or "Qt6_DIR" in line:
                _, _, value = line.partition("=")
                if value:
                    candidates.append(Path(value))

    for candidate in candidates:
        candidate = candidate.resolve()
        possible = [candidate / "bin"]
        if candidate.name == "Qt6" and candidate.parent.name == "cmake":
            possible.append(candidate.parents[2] / "bin")
        if candidate.name == "cmake" and candidate.parent.name == "lib":
            possible.append(candidate.parent.parent / "bin")
        for qt_bin in possible:
            qmake_names = ("qmake.exe", "qmake6.exe") if os.name == "nt" else ("qmake", "qmake6")
            if any((qt_bin / name).exists() for name in qmake_names):
                return qt_bin
    return None


def build_and_test(report: Report, root: Path, build_dir: Path, config: str) -> Path | None:
    cmake = shutil.which("cmake")
    ctest = shutil.which("ctest")
    if not cmake or not ctest:
        report.failed_check("local build toolchain", "cmake and ctest are required")
        return None

    if not (build_dir / "CMakeCache.txt").is_file():
        configure = [cmake, "-S", str(root), "-B", str(build_dir)]
        qt_bin = qt_bin_from_cache(build_dir)
        if qt_bin:
            configure.extend(["-DCMAKE_PREFIX_PATH=" + str(qt_bin.parent)])
        if not run_checked(report, "CMake Release configuration", configure, root, timeout=180):
            return None
    else:
        report.passed("CMake configuration already present")

    if not run_checked(
        report,
        f"Release build ({config})",
        [cmake, "--build", str(build_dir), "--config", config, "--parallel", "4"],
        root,
        timeout=900,
    ):
        return None

    env = os.environ.copy()
    qt_bin = qt_bin_from_cache(build_dir)
    if qt_bin:
        env["PATH"] = str(qt_bin) + os.pathsep + env.get("PATH", "")
    env["QT_QPA_PLATFORM"] = "offscreen"
    if not run_checked(
        report,
        "offscreen CTest suite",
        [ctest, "--test-dir", str(build_dir), "-C", config, "--output-on-failure", "--parallel", "4"],
        root,
        env=env,
        timeout=900,
    ):
        return None

    candidates = [
        build_dir / "build" / config / "RcloneBrowser.exe",
        build_dir / "build" / config / "rclone-browser.exe",
        build_dir / "build" / "rclone-browser",
        build_dir / "build" / config / "rclone-browser",
        build_dir / "build" / "rclone-browser.app" / "Contents" / "MacOS" / "rclone-browser",
    ]
    artifact = next((path for path in candidates if path.is_file()), None)
    if artifact is None:
        report.failed_check("built binary discovery", "no expected Release executable was found")
        return None

    version_result = subprocess.run(
        [str(artifact), "--version"],
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=30,
        check=False,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    output = version_result.stdout or ""
    version = read_text(root / "VERSION").strip()
    expected = f"Rclone Browser NG {version}"
    if version_result.returncode != 0 or expected not in output:
        report.failed_check(
            "built binary --version smoke",
            f"exit {version_result.returncode}, expected {expected!r}, got {output[-1000:]!r}",
        )
    else:
        report.passed(f"built binary --version smoke ({artifact.relative_to(root)})")
    smoke_packaged_windows_binary(report, root, build_dir, artifact, version, env)
    return artifact


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the local-only Rclone Browser NG release readiness checks."
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="CMake build directory relative to the repository (default: build)",
    )
    parser.add_argument(
        "--config",
        default="Release",
        help="CMake configuration to build and test (default: Release)",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = (root / args.build_dir).resolve()
    report = Report()

    print(f"Rclone Browser NG local release verification ({root})")
    version = check_source_contract(report, root)
    check_metadata(report, root)
    check_release_scripts(report, root)
    check_rclone_security_floor(report, root)
    check_qt_branch_policy(report, root)
    write_release_checksums(report, root)
    check_package_manifest_generator(report, root)
    check_appimage_update_verifier(report, root)
    if version is not None:
        build_and_test(report, root, build_dir, args.config)
    else:
        report.failed_check("build and executable smoke", "source version is invalid")

    print("\nCI-only or intentionally unavailable guarantees:")
    print("  - GitHub Actions build, test, and publication workflows are not configured.")
    print("  - CodeQL, clang-tidy CI, Scorecard, and OpenSSF workflow checks are not run.")
    print("  - Artifact attestations, provenance, checksums, and code signing are not provided.")
    print("  - Cross-platform packaging is run only through its platform-specific local script.")

    print("\nLocal release readiness report:")
    if report.failed:
        print(f"FAIL ({report.failed} required check(s) failed; {report.skipped} skipped)")
        return 1
    print(f"PASS (all required checks passed; {report.skipped} optional check(s) skipped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
