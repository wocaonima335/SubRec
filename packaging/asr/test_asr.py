"""Offline script regressions; all downloads and binary inputs are test fixtures."""
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import assets
import assemble
import fetch


class AssetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = b"TEST DOWNLOAD FIXTURE ONLY"
        self.pin = {"cache_path": "fixture.bin", "url": "https://example.invalid/fixture",
                    "size_bytes": len(self.data), "sha256": hashlib.sha256(self.data).hexdigest()}

    def test_verify_rejects_same_size_tampering(self):
        path = self.root / "fixture"
        path.write_bytes(self.data)
        assets.verify(path, self.pin)
        path.write_bytes(b"X" * len(self.data))
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            assets.verify(path, self.pin)

    def test_fetch_default_is_offline_and_rejects_missing_or_corrupt_cache(self):
        with patch.object(fetch, "manifest", return_value={"assets": {"fixture": self.pin}}), \
                patch.object(fetch.urllib.request, "urlopen") as network:
            with self.assertRaisesRegex(ValueError, "offline"):
                fetch.fetch(self.root, ["fixture"])
            (self.root / "fixture.bin").write_bytes(self.data)
            fetch.fetch(self.root, ["fixture"])
            (self.root / "fixture.bin").write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                fetch.fetch(self.root, ["fixture"], download=True)
            network.assert_not_called()

    def test_explicit_mock_download_publishes_only_verified_content(self):
        with patch.object(fetch, "manifest", return_value={"assets": {"fixture": self.pin}}), \
                patch.object(fetch.urllib.request, "urlopen", return_value=io.BytesIO(b"bad")):
            with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                fetch.fetch(self.root, ["fixture"], download=True)
            self.assertEqual(list(self.root.iterdir()), [])
        with patch.object(fetch, "manifest", return_value={"assets": {"fixture": self.pin}}), \
                patch.object(fetch.urllib.request, "urlopen", return_value=io.BytesIO(self.data)):
            fetch.fetch(self.root, ["fixture"], download=True)
            self.assertEqual((self.root / "fixture.bin").read_bytes(), self.data)

    def test_cache_in_repo_rejected(self):
        with self.assertRaisesRegex(ValueError, "outside the repository"):
            assets.outside_repo(assets.HERE / "downloads")

    def fixture_package(self):
        pins = assets.manifest()
        header = self.root / "package/include/sherpa-onnx/c-api/c-api.h"
        header.parent.mkdir(parents=True)
        header.write_text("TEST HEADER FIXTURE ONLY")
        lib = self.root / "package/lib"
        lib.mkdir()
        for name in pins["runtime"]["ship"]:
            (lib / name).write_bytes(self.data)
            pins["runtime"]["lib_files"][name] = len(self.data)
        for name in ("sherpa-onnx-cxx-api.dll", "sherpa-onnx-cli.exe"):
            (lib / name).write_bytes(b"EXCLUDED TEST FIXTURE")
        exe = self.root / "host.exe"
        exe.write_bytes(b"TEST HOST FIXTURE ONLY")
        return pins, exe

    def test_assembly_allowlist_no_default_vad_and_no_stale_reuse(self):
        pins, exe = self.fixture_package()
        payload = self.root / "payload"
        with patch.object(assemble, "manifest", return_value=pins):
            assemble.assemble(self.root / "package", exe, payload)
            self.assertEqual({p.name for p in (payload / "asr").iterdir()},
                             {"subrec-asr.exe", *pins["runtime"]["ship"]})
            for name, pin in pins["licenses"].items():
                assets.verify(payload / "licenses/asr" / name, pin)
            with self.assertRaisesRegex(ValueError, "fresh ASR destination"):
                assemble.assemble(self.root / "package", exe, payload)

    def test_assembly_rejects_bad_vad_before_writing_payload(self):
        pins, exe = self.fixture_package()
        payload = self.root / "payload"
        vad = self.root / "bad.onnx"
        vad.write_bytes(b"UNAPPROVED TEST VAD")
        with patch.object(assemble, "manifest", return_value=pins):
            with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                assemble.assemble(self.root / "package", exe, payload, "v6.2.3", vad)
            with self.assertRaisesRegex(ValueError, "requires both"):
                assemble.assemble(self.root / "package", exe, payload, vad_file=vad)
        self.assertFalse(payload.exists())


if __name__ == "__main__":
    unittest.main()
