#!/usr/bin/env python3
"""Verify downloaded Personal-VPN release archives against portable sidecars."""

from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path


CHECKSUM_LINE = re.compile(r"^([0-9a-f]{64})  ([^/\\]+)$")


def fail(message: str) -> int:
    print(f"release checksum verification failed: {message}", file=sys.stderr)
    return 1


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as artifact:
        for chunk in iter(lambda: artifact.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(directory: Path) -> int:
    if not directory.is_dir():
        return fail(f"asset directory does not exist: {directory}")

    entries = list(directory.iterdir())
    if any(not entry.is_file() or entry.is_symlink() for entry in entries):
        return fail("asset directory contains a non-regular file")

    archives = {
        entry.name: entry
        for entry in entries
        if entry.name.endswith(".zip") or entry.name.endswith(".tar.gz")
    }
    sidecars = {entry.name: entry for entry in entries if entry.name.endswith(".sha256")}
    if not archives or len(sidecars) != len(archives):
        return fail("every archive must have exactly one SHA-256 sidecar")
    if set(sidecars) != {f"{name}.sha256" for name in archives}:
        return fail("directory contains an unexpected or missing release asset")
    if {entry.name for entry in entries} != set(archives) | set(sidecars):
        return fail("directory contains an unexpected release asset")

    for archive_name, archive_path in archives.items():
        sidecar_path = sidecars[f"{archive_name}.sha256"]
        try:
            lines = sidecar_path.read_text(encoding="ascii").splitlines()
        except (OSError, UnicodeError) as error:
            return fail(f"cannot read {sidecar_path.name}: {error}")
        if len(lines) != 1:
            return fail(f"{sidecar_path.name} must contain exactly one checksum")
        match = CHECKSUM_LINE.fullmatch(lines[0])
        if match is None or match.group(2) != archive_name:
            return fail(f"{sidecar_path.name} has invalid checksum format or filename")
        if sha256(archive_path) != match.group(1):
            return fail(f"checksum mismatch: {archive_name}")

    print(f"Verified {len(archives)} release archive checksum(s).")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} ASSET_DIRECTORY")
    raise SystemExit(main(Path(sys.argv[1])))
