"""Exercise exclusive snapshot restoration, including filesystems without hard links."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / "open-splash/restore_transport_parts.py"
spec = importlib.util.spec_from_file_location("restore_water_parts", MODULE)
restore = importlib.util.module_from_spec(spec)
spec.loader.exec_module(restore)


class RestoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = b"verified native state bytes" * 5
        self.target = self.root / "state.bin"
        (self.root / "part").write_bytes(self.data)
        sha = hashlib.sha256(self.data).hexdigest()
        self.manifest = {"format": "lossless-file-parts-v1", "files": [{
            "path": "state.bin", "bytes": len(self.data), "sha256": sha,
            "parts": [{"path": "part", "bytes": len(self.data), "sha256": sha}]}]}
        self.write_manifest()

    def write_manifest(self):
        (self.root / "transport-parts.json").write_text(json.dumps(self.manifest))

    def run_restore(self, fake_os=None, check=False):
        with patch.object(restore, "ROOT", self.root), patch.object(
                sys, "argv", ["restore"] + (["--check-only"] if check else [])), \
                contextlib.redirect_stdout(io.StringIO()):
            if fake_os is None:
                restore.main()
            else:
                with patch.object(restore, "os", fake_os):
                    restore.main()

    def windows_without_links(self, error_code, race=False):
        calls = []
        def link(source, destination):
            if race:
                destination.write_bytes(b"concurrent writer")
            error = OSError("filesystem does not support hard links")
            error.winerror = error_code
            raise error
        def rename(source, destination):
            calls.append((source, destination))
            # Windows rename refuses existing destinations. Model it when the
            # Windows branch is exercised by the Linux/macOS test matrix.
            if destination.exists():
                raise FileExistsError(destination)
            os.rename(source, destination)
        fake = types.SimpleNamespace(name="nt", fdopen=os.fdopen, link=link, rename=rename)
        return fake, calls

    def test_successful_windows_fallback_for_both_supported_errors(self):
        for code in (1, 50):
            with self.subTest(winerror=code):
                fake, calls = self.windows_without_links(code)
                self.run_restore(fake)
                self.assertEqual(self.target.read_bytes(), self.data)
                self.assertEqual(len(calls), 1)
                self.assertEqual(list(self.root.glob("*.tmp")), [])
                self.target.unlink()

    def test_concurrent_destination_is_preserved(self):
        fake, calls = self.windows_without_links(1, race=True)
        with self.assertRaises(FileExistsError):
            self.run_restore(fake)
        self.assertEqual(self.target.read_bytes(), b"concurrent writer")
        self.assertEqual(len(calls), 1)
        self.assertEqual(list(self.root.glob("*.tmp")), [])

    def test_permission_error_does_not_attempt_rename(self):
        fake, calls = self.windows_without_links(5)
        with self.assertRaises(OSError):
            self.run_restore(fake)
        self.assertFalse(self.target.exists())
        self.assertEqual(calls, [])
        self.assertEqual(list(self.root.glob("*.tmp")), [])

    def test_non_windows_does_not_use_windows_fallback(self):
        fake, calls = self.windows_without_links(1)
        fake.name = "posix"
        with self.assertRaises(OSError):
            self.run_restore(fake)
        self.assertEqual(calls, [])
        self.assertFalse(self.target.exists())

    def test_matching_existing_state_is_kept_different_state_refused(self):
        self.target.write_bytes(self.data)
        before = self.target.stat().st_mtime_ns
        self.run_restore()
        self.assertEqual(self.target.stat().st_mtime_ns, before)
        self.target.write_bytes(b"different")
        with self.assertRaises(FileExistsError):
            self.run_restore()
        self.assertEqual(self.target.read_bytes(), b"different")

    def test_damaged_part_rejected_and_check_only_creates_no_state(self):
        self.run_restore(check=True)
        self.assertFalse(self.target.exists())
        (self.root / "part").write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Damaged part"):
            self.run_restore()
        self.assertFalse(self.target.exists())

    def test_combined_digest_and_traversal_rejected(self):
        self.manifest["files"][0]["sha256"] = "0" * 64
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "Combined snapshot"):
            self.run_restore()
        self.manifest["files"][0]["path"] = "../outside"
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "Unsafe relative path"):
            self.run_restore()
        self.assertFalse(self.target.exists())


if __name__ == "__main__":
    unittest.main()
