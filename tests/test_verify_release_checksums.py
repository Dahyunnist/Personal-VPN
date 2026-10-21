#!/usr/bin/env python3
"""Regression tests for the draft-release checksum gate."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "verify-release-checksums.py"
ARCHIVE_NAME = "personal-vpn-0.1.0-Windows-AMD64.zip"


class ReleaseChecksumVerifierTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory(dir=SCRIPT.parents[1])
        self.directory = Path(self.temporary_directory.name)
        self.archive = self.directory / ARCHIVE_NAME
        self.archive.write_bytes(b"test release archive\n")
        self.digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.sidecar = self.directory / f"{ARCHIVE_NAME}.sha256"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def run_verifier(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), str(self.directory)],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_accepts_valid_checksum_with_windows_line_endings(self) -> None:
        self.sidecar.write_bytes(f"{self.digest}  {ARCHIVE_NAME}\r\n".encode("ascii"))

        result = self.run_verifier()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Verified 1 release archive checksum(s).", result.stdout)

    def test_rejects_tampered_archive_digest(self) -> None:
        self.sidecar.write_text(f"{'0' * 64}  {ARCHIVE_NAME}\n", encoding="ascii")

        result = self.run_verifier()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum mismatch", result.stderr)

    def test_rejects_non_basename_checksum_path(self) -> None:
        self.sidecar.write_text(f"{self.digest}  ../{ARCHIVE_NAME}\n", encoding="ascii")

        result = self.run_verifier()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid checksum format or filename", result.stderr)

    def test_rejects_unexpected_release_asset(self) -> None:
        self.sidecar.write_text(f"{self.digest}  {ARCHIVE_NAME}\n", encoding="ascii")
        (self.directory / "unexpected.txt").write_text("unexpected", encoding="ascii")

        result = self.run_verifier()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected release asset", result.stderr)


if __name__ == "__main__":
    unittest.main()
