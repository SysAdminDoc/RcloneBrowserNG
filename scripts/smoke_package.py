#!/usr/bin/env python3
"""Run the non-GUI --version smoke against a packaged application artifact."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile


def _inside(root: Path, path: Path) -> bool:
    return path == root or root in path.parents


def safe_extract(archive: Path, destination: Path) -> None:
    # zipfile drops the Unix mode that ditto stores, so a macOS .app.zip came
    # out with a binary that couldn't run and framework symlinks written as
    # small text files. Both are put back from external_attr here.
    destination_root = destination.resolve()
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            target = (destination / member.filename).resolve()
            if not _inside(destination_root, target):
                raise ValueError(f"archive contains an unsafe path: {member.filename}")
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            mode = (member.external_attr >> 16) if member.create_system == 3 else 0
            if stat.S_ISLNK(mode):
                link = source.read(member).decode("utf-8")
                if os.path.isabs(link) or not _inside(
                    destination_root, (target.parent / link).resolve()
                ):
                    raise ValueError(
                        f"archive link points outside it: {member.filename} -> {link}"
                    )
                target.symlink_to(link)
                continue
            with source.open(member) as source_stream, target.open("wb") as target_stream:
                shutil.copyfileobj(source_stream, target_stream)
            if mode & 0o777:
                target.chmod(mode & 0o777)


def find_binary(root: Path) -> Path:
    candidates = [
        path
        for path in root.rglob("*")
        if path.is_file()
        and path.name.lower() in {"rclonebrowser.exe", "rclone-browser", "rclone-browser.exe"}
    ]
    if not candidates:
        candidates = [
            path
            for path in root.rglob("*")
            if path.is_file()
            and path.as_posix().lower().endswith(".app/contents/macos/rclone-browser")
        ]
    if not candidates:
        raise ValueError(f"no packaged Rclone Browser executable found under {root}")
    return sorted(candidates, key=lambda path: (len(path.parts), str(path)))[0]


@contextmanager
def prepared_artifact(artifact: Path):
    if not artifact.exists():
        raise ValueError(f"package artifact does not exist: {artifact}")
    if artifact.is_dir():
        yield find_binary(artifact)
        return
    if artifact.suffix.lower() == ".zip":
        with tempfile.TemporaryDirectory(prefix="rclone-package-smoke-") as temp:
            extracted = Path(temp)
            safe_extract(artifact, extracted)
            yield find_binary(extracted)
        return
    if artifact.suffix.lower() == ".app" and artifact.is_dir():
        yield find_binary(artifact)
        return
    if artifact.suffix.lower() == ".appimage":
        if sys.platform != "linux":
            raise ValueError("AppImage smoke requires a Linux host")
        artifact.chmod(artifact.stat().st_mode | 0o111)
        yield artifact
        return
    if artifact.name.lower().endswith("-setup.exe"):
        raise ValueError(
            "installer files are not launched by this smoke; smoke the deployed "
            "directory or zip produced beside the installer"
        )
    yield artifact


def smoke(artifact: Path, version: str) -> None:
    with prepared_artifact(artifact) as binary:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        result = subprocess.run(
            [str(binary), "--version"],
            cwd=binary.parent,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=30,
            check=False,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        expected = f"Rclone Browser NG {version}"
        if result.returncode != 0 or expected not in (result.stdout or ""):
            raise ValueError(
                f"packaged binary exited {result.returncode}; expected {expected!r}; "
                f"output was {(result.stdout or '')[-1000:]!r}"
            )
        print(f"[PASS] packaged --version smoke: {binary}")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument(
        "--version",
        default=(root / "VERSION").read_text(encoding="utf-8").strip(),
    )
    args = parser.parse_args()
    artifact = args.artifact
    if not artifact.is_absolute():
        artifact = (Path.cwd() / artifact).resolve()
    try:
        smoke(artifact, args.version)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
