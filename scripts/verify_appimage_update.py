#!/usr/bin/env python3
"""Verify that an AppImage contains the expected zsync update metadata."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


def verify(artifact: Path, expected: str) -> None:
    if not artifact.is_file():
        raise ValueError(f"AppImage does not exist: {artifact}")

    marker = expected.encode("utf-8")
    data = artifact.read_bytes()
    if marker in data:
        return

    readelf = shutil_which("readelf")
    if readelf:
        result = subprocess.run(
            [readelf, "-p", ".upd_info", str(artifact)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=30,
        )
        if expected in (result.stdout or ""):
            return
    raise ValueError(f"AppImage is missing update metadata: {expected}")


def shutil_which(command: str) -> str | None:
    # Keep the verifier's imports tiny and make it easy to embed in packaging.
    import shutil

    return shutil.which(command)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--expected", required=True)
    args = parser.parse_args()
    try:
        verify(args.artifact, args.expected)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"[PASS] AppImage update metadata: {args.expected}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
