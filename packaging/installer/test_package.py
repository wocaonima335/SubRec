"""Offline regression checks for release eligibility, payload cleanup and hashes.

Temporary fixture bytes are not upstream translations, licenses or executables.
No Windows application, installer or network request is executed here.
"""
import importlib.util
from pathlib import Path
import tempfile
import shutil
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location("subrec_package", Path(__file__).with_name("package.py"))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackagingTests(unittest.TestCase):
    def test_shared_version(self):
        self.assertEqual(package.version("tag", "v0.2.1-beta.2", "abcdef012345"), {
            "version": "0.2.1-beta.2", "numeric": "0.2.1.0", "stem": "SubRec-v0.2.1-beta.2"})
        self.assertEqual(package.version("branch", "main", "abcdef012345")["version"],
                         "0.0.0-ci.abcdef0")

    def test_reject_ineligible_or_unsafe_versions(self):
        for ref in ("v0.2.1", "0.2.1-beta", "v65536.0.0-beta", "v01.2.3-beta",
                    "v1.2.3-beta/evil", 'v1.2.3-beta"', "v1.2.3-beta\n", "v1.2.3-"):
            with self.subTest(ref=ref), self.assertRaises(ValueError):
                package.version("tag", ref, "abcdef012345")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.here, self.payload, self.compiler, self.output = [root / n for n in
                                                             ("installer", "payload", "compiler", "out")]
        for directory in (self.here, self.payload, self.compiler, self.output):
            directory.mkdir()
        self.patch = patch.object(package, "HERE", self.here)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        for name in ("ChineseSimplified.isl", "ChineseSimplified.LICENSE.txt", "THIRD_PARTY_SOURCES.md"):
            (self.here / name).write_text("TEST FIXTURE ONLY", encoding="utf-8")
        (self.here / "cleanup-directories.txt").write_text("qml\nlicenses\nsources\nretiredplugin\n")
        (self.compiler / "License.txt").write_bytes(b"TEST LICENSE FIXTURE ONLY\r\n")
        for name in ("appSubRec.exe", "LICENSE.txt", "THIRD_PARTY_LICENSES.md", "SOURCES.txt",
                     "sources/SHA256SUMS", "sources/BUILD_CONFIG.txt", "licenses/ffmpeg/COPYING",
                     "qml/Module/qmldir", "test.dll"):
            path = self.payload / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"TEST PAYLOAD FIXTURE ONLY\r\n")
        self.stem = "SubRec-v0.2.1-beta"

    def prepare(self, is_tag=False):
        return package.prepare(self.payload, self.compiler, self.stem, is_tag)

    def bundle(self):
        self.prepare()
        (self.output / package.asset_names(self.stem)[0]).write_bytes(b"TEST SETUP FIXTURE ONLY")
        package.bundle(self.payload, self.output, self.stem)

    def test_prepare_keeps_retired_dirs_and_original_license(self):
        self.assertEqual(self.prepare(is_tag=True), self.stem)
        generated = (self.here / "installdelete.generated.iss").read_text(encoding="utf-8-sig")
        self.assertIn('Name: "{app}\\retiredplugin"', generated)
        self.assertNotIn("SubRec.log", generated)
        self.assertEqual((self.payload / "licenses/inno-setup/LICENSE.txt").read_bytes(),
                         (self.compiler / "License.txt").read_bytes())

    def test_missing_translation_fails(self):
        (self.here / "ChineseSimplified.isl").unlink()
        with self.assertRaisesRegex(ValueError, "Missing ChineseSimplified.isl"):
            self.prepare()

    def test_missing_compiler_license_fails(self):
        (self.compiler / "License.txt").unlink()
        with self.assertRaisesRegex(ValueError, "License.txt missing"):
            self.prepare()

    def test_incomplete_names_and_tag_rejection(self):
        (self.payload / "sources" / package.MARKER).write_text("test missing source")
        self.assertEqual(self.prepare(), self.stem + "-INCOMPLETE")
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            self.prepare(is_tag=True)

    def test_new_directory_requires_history_before_tag(self):
        (self.payload / "newplugin").mkdir()
        with self.assertRaisesRegex(ValueError, "New deployment directories"):
            self.prepare(is_tag=True)

    def test_unsafe_cleanup_path_rejected(self):
        (self.here / "cleanup-directories.txt").write_text("../recordings\n")
        with self.assertRaisesRegex(ValueError, "Unsafe deployment directory"):
            self.prepare()

    def test_archive_layout_and_manifest(self):
        self.bundle()
        package.verify(self.output, self.stem)
        with zipfile.ZipFile(self.output / package.asset_names(self.stem)[1]) as archive:
            self.assertEqual(archive.read("sources/BUILD_CONFIG.txt"),
                             (self.payload / "sources/BUILD_CONFIG.txt").read_bytes())
            self.assertIn("licenses/inno-setup/LICENSE.txt", archive.namelist())
            self.assertIn("appSubRec.exe", archive.namelist())
        with zipfile.ZipFile(self.output / package.asset_names(self.stem)[2]) as archive:
            self.assertIn("sources/BUILD_CONFIG.txt", archive.namelist())
            self.assertIn("licenses/COPYING", archive.namelist())

    def test_tampering_or_extra_asset_rejected(self):
        self.bundle()
        extra = self.output / "unexpected.exe"
        extra.write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            package.verify(self.output, self.stem)
        extra.unlink()
        (self.output / package.asset_names(self.stem)[0]).write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "manifest mismatch"):
            package.verify(self.output, self.stem)

    def test_hidden_incomplete_marker_rejected_even_with_valid_hashes(self):
        (self.payload / "sources" / package.MARKER).write_text("test missing source")
        self.bundle()
        with self.assertRaisesRegex(ValueError, "Incomplete source/license"):
            package.verify(self.output, self.stem)

    def add_asr(self):
        (self.payload / "asr").mkdir()
        for name in ("subrec-asr.exe", "sherpa-onnx-c-api.dll", "onnxruntime.dll",
                     "onnxruntime_providers_shared.dll"):
            (self.payload / "asr" / name).write_bytes(b"TEST ASR FIXTURE ONLY")
        shutil.copytree(Path(__file__).resolve().parents[1] / "licenses/asr",
                        self.payload / "licenses/asr")
        with (self.here / "cleanup-directories.txt").open("a") as stream:
            stream.write("asr\n")

    def test_asr_absent_remains_valid_for_tag(self):
        self.assertFalse((self.payload / "asr").exists())
        self.assertEqual(self.prepare(is_tag=True), self.stem)

    def test_complete_asr_without_vad_valid_for_tag(self):
        self.add_asr()
        self.assertEqual(self.prepare(is_tag=True), self.stem)
        self.assertFalse((self.payload / "asr/silero_vad.onnx").exists())
        generated = (self.here / "installdelete.generated.iss").read_text(encoding="utf-8-sig")
        self.assertIn('Name: "{app}\\asr"', generated)

    def test_asr_missing_required_files_fails(self):
        self.add_asr()
        required = list((self.payload / "asr").iterdir()) + list((self.payload / "licenses/asr").iterdir())
        for path in required:
            original = path.read_bytes()
            path.unlink()
            try:
                with self.subTest(name=path.name), self.assertRaisesRegex(ValueError, "ASR payload file"):
                    self.prepare()
            finally:
                path.write_bytes(original)

    def test_asr_license_tampering_fails(self):
        self.add_asr()
        (self.payload / "licenses/asr/onnxruntime.LICENSE").write_text("not the upstream license")
        with self.assertRaisesRegex(ValueError, "ASR license size/SHA256 mismatch"):
            self.prepare()

    def test_asr_wrong_vad_and_unapproved_files_fail(self):
        self.add_asr()
        vad = self.payload / "asr/silero_vad.onnx"
        vad.write_bytes(b"unapproved VAD")
        with self.assertRaisesRegex(ValueError, "reserved v6.2.3"):
            self.prepare()
        vad.unlink()
        for name in ("silero_vad_v6.2.3.onnx", "silero_vad_v4_sherpa.onnx", "model.int8.onnx",
                     "sherpa-onnx-cxx-api.dll", "sherpa-onnx-offline.exe"):
            path = self.payload / "asr" / name
            path.write_bytes(b"UNAPPROVED FIXTURE")
            try:
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "Unexpected ASR payload entry"):
                    self.prepare()
            finally:
                path.unlink()

    def test_empty_asr_or_file_instead_of_directory_fails(self):
        path = self.payload / "asr"
        path.write_text("not a directory")
        with self.assertRaisesRegex(ValueError, "must be a directory"):
            self.prepare()
        path.unlink()
        path.mkdir()
        with self.assertRaisesRegex(ValueError, "ASR payload file"):
            self.prepare()


if __name__ == "__main__":
    unittest.main()
