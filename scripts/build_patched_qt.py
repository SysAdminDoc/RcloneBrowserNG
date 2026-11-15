#!/usr/bin/env python3
"""Build qtbase from the release sources with the published security diffs.

The Qt Company publishes fixes for a release branch as source diffs on
download.qt.io weeks before the next patch release brings them as binaries.
This script builds the qtbase that release lanes link against from those
sources: it downloads the qtbase source archive and each diff, refuses any
file whose SHA-256 differs from the value pinned below, applies the diffs,
then configures, builds and installs qtbase into a prefix of its own.

The app only links qtbase modules (Core, Gui, Widgets, Network) and deploys
without translations. The package also carries Qt SVG's image and icon
plugins, which the remote browser's .svg preview loads at run time, so Qt
SVG is built from its unchanged release source on top of the patched
qtbase. Together they're a complete Qt for building, testing and packaging
the app.

--modules-only builds just Qt SVG into a prefix this script already put the
patched qtbase in.

The install gets a record, qt-security-patches.json at the prefix root,
listing every diff that went in. scripts/validate_qt_version.py reads it
with read_record and covered_cves below to decide whether a Qt older than
the release carrying those fixes may ship.

Usage:
    python scripts/build_patched_qt.py --prefix C:\\Qt\\6.12.0-patched\\msvc2022_64
    python3 scripts/build_patched_qt.py --prefix ~/Qt/6.12.0-patched/gcc_64

On Windows it finds Visual Studio's x64 build environment with vswhere, so
it runs from any shell. Elsewhere the compiler on PATH is used. CMake 3.22
or newer and Ninja must be on PATH.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

QT_VERSION = "6.12.0"
RECORD_NAME = "qt-security-patches.json"

_BASE = "https://download.qt.io/official_releases/qt/6.12"

# SHA-256 values come from download.qt.io's own MirrorBrain checksums
# (append .sha256 to each URL), checked 2026-11-15. A mirror serving
# anything else is refused.
SOURCE = {
    "file": f"qtbase-everywhere-src-{QT_VERSION}.tar.xz",
    "url": f"{_BASE}/{QT_VERSION}/submodules/qtbase-everywhere-src-{QT_VERSION}.tar.xz",
    "sha256": "a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307",
}

# Only fixes the app can reach. CVE-2026-105924 (CBOR to JSON) has a qtbase
# 6.12 diff too, but the app reads no CBOR.
#
# Applied in upstream commit order, which isn't the file names' order: 0002
# (header block limit) was committed first, and 0001's test hunk sits on top
# of the test 0002 adds, so 0001 first fails to apply.
PATCHES = (
    {
        "cve": "CVE-2026-116337",
        "file": "CVE-2026-116337-qtbase-6.12-0002.diff",
        "sha256": "83abc89d507dbd9a94dc5718b0125492c1c37b0609a8f404a1f164925940faec",
    },
    {
        "cve": "CVE-2026-116337",
        "file": "CVE-2026-116337-qtbase-6.12-0001.diff",
        "sha256": "74c5028985dcc0455255909de4dc80f50cf5472dbf465ce46ebc791a7b080f12",
    },
    {
        "cve": "CVE-2026-105943",
        "file": "CVE-2026-105943-qtbase-6.12.diff",
        "sha256": "dd22629ce948cf0a900421323cca331907180242a9b51c3fd75b25c75f068099",
    },
)

# Built unchanged on top of the patched qtbase. Release 2.0.2 shipped Qt SVG
# from the stock install, and without it .svg files stop previewing.
EXTRA_MODULES = (
    {
        "module": "qtsvg",
        "file": f"qtsvg-everywhere-src-{QT_VERSION}.tar.xz",
        "url": f"{_BASE}/{QT_VERSION}/submodules/qtsvg-everywhere-src-{QT_VERSION}.tar.xz",
        "sha256": "e4ab39534ec97987b1b9b60ef7f4d3253d5912a69f8c713a01753174a4029331",
    },
)

CONFIGURE_OPTIONS = (
    "-release",
    "-opensource",
    "-confirm-license",
    "-nomake",
    "examples",
    "-nomake",
    "tests",
)


def log(message: str) -> None:
    print(f"[build_patched_qt] {message}", flush=True)


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(url: str, expected: str, dest: Path) -> Path:
    """Download url to dest unless a copy with the right hash is there."""
    if dest.exists() and sha256_of(dest) == expected:
        log(f"have {dest.name}")
        return dest
    log(f"downloading {url}")
    partial = dest.with_name(dest.name + ".part")
    request = urllib.request.Request(
        url, headers={"User-Agent": "RcloneBrowserNG-build"}
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        with partial.open("wb") as handle:
            shutil.copyfileobj(response, handle, 1 << 20)
    actual = sha256_of(partial)
    if actual != expected:
        partial.unlink()
        raise SystemExit(
            f"ERROR: {dest.name} has SHA-256 {actual}, expected {expected}. "
            "Refusing to build from it."
        )
    partial.replace(dest)
    return dest


def extract(archive: Path, into: Path) -> Path:
    if into.exists():
        shutil.rmtree(into)
    into.mkdir(parents=True)
    log(f"extracting {archive.name}")
    with tarfile.open(archive) as tar:
        tar.extractall(into, filter="data")
    roots = [entry for entry in into.iterdir() if entry.is_dir()]
    if len(roots) != 1:
        raise SystemExit(f"ERROR: expected one top folder in {archive.name}")
    return roots[0]


def apply_patches(source_root: Path, patch_files: list[Path]) -> None:
    git = shutil.which("git")
    if git is None:
        raise SystemExit("ERROR: git is needed to apply the diffs.")
    for patch in patch_files:
        log(f"applying {patch.name}")
        # git apply works outside a repository. --check first, so a diff
        # that doesn't fit leaves the tree untouched.
        for extra in (["--check"], []):
            subprocess.run(
                [git, "apply", "--whitespace=nowarn", *extra, str(patch)],
                cwd=source_root,
                check=True,
            )


def msvc_environment() -> dict[str, str]:
    """The environment vcvars64.bat sets up, captured once."""
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise SystemExit(f"ERROR: vswhere not found at {vswhere}")
    install = subprocess.run(
        [
            str(vswhere), "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    if not install:
        raise SystemExit("ERROR: no Visual Studio with the x64 C++ tools found.")
    vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    output = subprocess.run(
        f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',
        capture_output=True, text=True, check=True,
    ).stdout
    env = {}
    for line in output.splitlines():
        name, sep, value = line.partition("=")
        if sep and name:
            env[name] = value
    if "VCToolsInstallDir" not in env:
        raise SystemExit(f"ERROR: {vcvars} did not set up a compiler.")
    return env


def write_record(prefix: Path, modules: list[dict] | None = None) -> None:
    record = {
        "qt_version": QT_VERSION,
        "module": "qtbase",
        "source": {"file": SOURCE["file"], "sha256": SOURCE["sha256"]},
        "patches": [dict(patch) for patch in PATCHES],
    }
    if modules:
        record["extra_modules"] = modules
    (prefix / RECORD_NAME).write_text(
        json.dumps(record, indent=2) + "\n", encoding="utf-8"
    )


def build_extra_module(module: dict, prefix: Path, work: Path,
                       env: dict[str, str], cmake: str, jobs: int) -> dict:
    """Configure, build and install one unpatched module into prefix."""
    archive = fetch(module["url"], module["sha256"],
                    work / "downloads" / module["file"])
    source_root = extract(archive, work / f"{module['module']}-src")
    build = work / f"{module['module']}-build"
    if build.exists():
        shutil.rmtree(build)
    build.mkdir()
    script = "qt-configure-module.bat" if os.name == "nt" else "qt-configure-module"
    configure = prefix / "bin" / script
    if not configure.exists():
        raise SystemExit(f"ERROR: {configure} is missing. Build qtbase first.")
    log(f"configuring {module['module']}")
    subprocess.run([str(configure), str(source_root)], cwd=build, env=env,
                   check=True)
    log(f"building {module['module']} with {jobs} job(s)")
    subprocess.run([cmake, "--build", ".", "--parallel", str(jobs)],
                   cwd=build, env=env, check=True)
    subprocess.run([cmake, "--install", "."], cwd=build, env=env, check=True)
    return {"module": module["module"], "file": module["file"],
            "sha256": module["sha256"]}


def read_record(prefix: Path) -> dict | None:
    """The record in a Qt prefix, or None when there isn't one.

    Raises ValueError when the file is there but isn't a JSON object, so a
    damaged record is reported instead of read as "no patches".
    """
    path = prefix / RECORD_NAME
    if not path.is_file():
        return None
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{path} can't be read: {exc}") from exc
    if not isinstance(record, dict):
        raise ValueError(f"{path} is not a JSON object")
    return record


def covered_cves(record: dict, qt_version: str) -> set[str]:
    """The CVEs a record shows were fixed in a qtbase of that version.

    A CVE counts only when every diff pinned for it above is listed with the
    pinned file name and SHA-256, the record is for this Qt version and
    qtbase, and the source archive matches the pinned one. Anything else, a
    hand-edited record or one from another Qt included, covers nothing.
    """
    if record.get("qt_version") != qt_version or qt_version != QT_VERSION:
        return set()
    if record.get("module") != "qtbase":
        return set()
    source = record.get("source")
    if not isinstance(source, dict) or source.get("sha256") != SOURCE["sha256"]:
        return set()
    patches = record.get("patches")
    if not isinstance(patches, list):
        return set()
    listed = {
        (entry.get("cve"), entry.get("file"), entry.get("sha256"))
        for entry in patches
        if isinstance(entry, dict)
    }
    covered = set()
    for cve in {patch["cve"] for patch in PATCHES}:
        needed = {
            (patch["cve"], patch["file"], patch["sha256"])
            for patch in PATCHES
            if patch["cve"] == cve
        }
        if needed <= listed:
            covered.add(cve)
    return covered


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build qtbase from the release sources with the published security diffs."
    )
    parser.add_argument("--prefix", required=True, type=Path,
                        help="where to install the patched qtbase")
    parser.add_argument("--work", type=Path,
                        default=Path(tempfile.gettempdir()) / f"qtb-{QT_VERSION}",
                        help="downloads, sources and build tree (keep it short on Windows)")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--modules-only", action="store_true",
                        help="keep the patched qtbase already in --prefix and "
                             "only build Qt SVG into it")
    args = parser.parse_args()

    prefix = args.prefix.expanduser().resolve()
    work = args.work.expanduser().resolve()
    downloads = work / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    if os.name == "nt":
        env = msvc_environment()
    # Windows looks a bare program name up on this process's PATH, not the
    # one passed in env, so resolve the tools against the build PATH.
    search_path = env.get("PATH") or env.get("Path")
    tools = {}
    for tool in ("cmake", "ninja"):
        tools[tool] = shutil.which(tool, path=search_path)
        if tools[tool] is None:
            raise SystemExit(f"ERROR: {tool} is not on PATH.")

    all_cves = {patch["cve"] for patch in PATCHES}
    if args.modules_only:
        record = read_record(prefix)
        if record is None or covered_cves(record, QT_VERSION) != all_cves:
            raise SystemExit(
                f"ERROR: {prefix} has no patched qtbase {QT_VERSION} from this "
                "script. Run it without --modules-only first."
            )
    else:
        if prefix.exists() and any(prefix.iterdir()):
            if not (prefix / RECORD_NAME).exists():
                raise SystemExit(
                    f"ERROR: {prefix} exists and wasn't made by this script. "
                    "Pick an empty prefix."
                )
            log(f"replacing the earlier build in {prefix}")
            shutil.rmtree(prefix)

        archive = fetch(SOURCE["url"], SOURCE["sha256"], downloads / SOURCE["file"])
        patch_files = [
            fetch(f"{_BASE}/{patch['file']}", patch["sha256"], downloads / patch["file"])
            for patch in PATCHES
        ]

        source_root = extract(archive, work / "src")
        apply_patches(source_root, patch_files)

        build = work / "build"
        if build.exists():
            shutil.rmtree(build)
        build.mkdir()
        configure = source_root / ("configure.bat" if os.name == "nt" else "configure")

        log("configuring")
        subprocess.run(
            [str(configure), "-prefix", str(prefix), *CONFIGURE_OPTIONS],
            cwd=build, env=env, check=True,
        )
        log(f"building with {args.jobs} job(s)")
        subprocess.run(
            [tools["cmake"], "--build", ".", "--parallel", str(args.jobs)],
            cwd=build, env=env, check=True,
        )
        log("installing")
        subprocess.run([tools["cmake"], "--install", "."], cwd=build, env=env, check=True)
        write_record(prefix)

    modules = [
        build_extra_module(module, prefix, work, env, tools["cmake"], args.jobs)
        for module in EXTRA_MODULES
    ]
    write_record(prefix, modules)
    log(f"done: Qt {QT_VERSION} with {', '.join(sorted(all_cves))} and "
        f"{', '.join(m['module'] for m in modules)} in {prefix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
