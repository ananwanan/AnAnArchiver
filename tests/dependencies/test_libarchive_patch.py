"""Exercise the private official-source export and fail-closed patch handling."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "cmake/patches/libarchive-compatibility.patch"


def execute(arguments, **kwargs):
    return subprocess.run([str(value) for value in arguments], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)


class LibarchivePatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "third_party/libarchive"
        self.source.mkdir(parents=True)
        self.patch = self.root / "cmake/patches/libarchive-compatibility.patch"
        self.patch.parent.mkdir(parents=True)
        # Small synthetic inputs keep these source-preparation tests offline
        # even in CI's dependency-tools job, which has no submodules checked out.
        fixture = {
            "libarchive/archive_cryptor.c": "/* pristine crypto */\n",
            "libarchive/archive_digest.c": "/* pristine digest */\n",
            "libarchive/archive_write_set_format_zip.c": "/* pristine ZIP */\n",
        }
        for path, content in fixture.items():
            destination = self.source / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content, encoding="utf-8", newline="\n")
        self.patch.write_text('''diff --git a/libarchive/archive_cryptor.c b/libarchive/archive_cryptor.c
--- a/libarchive/archive_cryptor.c
+++ b/libarchive/archive_cryptor.c
@@ -1 +1,2 @@
 /* pristine crypto */
+#define ARCHIVE_CRYPTO_MBEDTLS_PSA 1
diff --git a/libarchive/archive_write_set_format_zip.c b/libarchive/archive_write_set_format_zip.c
--- a/libarchive/archive_write_set_format_zip.c
+++ b/libarchive/archive_write_set_format_zip.c
@@ -1 +1,2 @@
 /* pristine ZIP */
+archive_write_zip_end_compressor(zip);
diff --git a/libarchive/archive_mbedtls.c b/libarchive/archive_mbedtls.c
new file mode 100644
--- /dev/null
+++ b/libarchive/archive_mbedtls.c
@@ -0,0 +1 @@
+/* PSA adapter fixture */
''', encoding="utf-8", newline="\n")
        self.paths = re.findall(r"^\+\+\+ b/(.+)$", self.patch.read_text(encoding="utf-8"), re.MULTILINE)
        self.checked(["git", "init", "--quiet", self.source])
        self.commit("Pristine official patch inputs")
        self.manifest = self.root / "resolution.json"
        self.record_revision()
        self.script = self.root / "prepare.cmake"
        self.script.write_text(f'''cmake_minimum_required(VERSION 3.24)
set(PROJECT_SOURCE_DIR "{self.root.as_posix()}")
set(CMAKE_BINARY_DIR "{(self.root / 'build').as_posix()}")
set(ANANARCHIVER_DEPENDENCY_MANIFEST "{self.manifest.as_posix()}")
include("{(ROOT / 'cmake/PrepareLibarchive.cmake').as_posix()}")
ananarchiver_prepare_libarchive(prepared_source)
file(WRITE "{(self.root / 'result.txt').as_posix()}" "${{prepared_source}}")
''', encoding="utf-8")

    def checked(self, arguments):
        result = execute(arguments)
        self.assertEqual(0, result.returncode, result.stdout)
        return result.stdout.strip()

    def commit(self, message):
        self.checked(["git", "-C", self.source, "add", "."])
        self.checked(["git", "-C", self.source, "-c", "user.name=patch fixture",
                      "-c", "user.email=fixture@example.invalid", "commit", "--quiet", "-m", message])

    def record_revision(self):
        revision = self.checked(["git", "-C", self.source, "rev-parse", "HEAD"])
        self.manifest.write_text(json.dumps({"dependencies": [{"path": "third_party/libarchive",
                                                                  "revision": revision}]}), encoding="utf-8")

    def prepare(self):
        self.checked(["cmake", "-P", self.script])
        return Path((self.root / "result.txt").read_text(encoding="utf-8"))

    def test_exports_and_patches_without_modifying_resolved_checkout(self):
        before = {path: hashlib.sha256((self.source / path).read_bytes()).hexdigest()
                  for path in self.paths if (self.source / path).exists()}
        prepared = self.prepare()
        self.assertNotEqual(prepared, self.source)
        self.assertIn("ARCHIVE_CRYPTO_MBEDTLS_PSA", (prepared / "libarchive/archive_cryptor.c").read_text())
        self.assertIn("archive_write_zip_end_compressor(zip)",
                      (prepared / "libarchive/archive_write_set_format_zip.c").read_text())
        self.assertTrue((prepared / "libarchive/archive_mbedtls.c").is_file())
        self.assertEqual(before, {path: hashlib.sha256((self.source / path).read_bytes()).hexdigest()
                                  for path in before})
        self.assertEqual("", self.checked(["git", "-C", self.source, "status", "--porcelain"]))
        self.assertEqual(prepared, self.prepare())

    def test_modified_generated_patch_output_is_rejected(self):
        prepared = self.prepare()
        output = prepared / "libarchive/archive_mbedtls.c"
        output.write_text(output.read_text() + "\n/* changed */\n")
        result = execute(["cmake", "-P", self.script])
        self.assertNotEqual(0, result.returncode)
        self.assertIn("generated libarchive source was modified", result.stdout)

    def test_missing_preparation_record_is_rejected(self):
        prepared = self.prepare()
        (prepared / ".prepared").unlink()
        result = execute(["cmake", "-P", self.script])
        self.assertNotEqual(0, result.returncode)
        self.assertIn("incomplete libarchive source export", result.stdout)

    def test_changed_patch_uses_a_new_source_export(self):
        previous = self.prepare()
        self.patch.write_text(self.patch.read_text(encoding="utf-8") + "\n# recipe revision\n", encoding="utf-8", newline="\n")
        self.assertNotEqual(previous, self.prepare())

    def test_upstream_already_containing_fixes_requires_explicit_retirement(self):
        self.checked(["git", "-C", self.source, "apply", self.patch])
        self.commit("Official source already includes fixes")
        self.record_revision()
        result = execute(["cmake", "-P", self.script])
        self.assertNotEqual(0, result.returncode)
        self.assertIn("Review and retire fixes already included upstream", result.stdout)
        self.assertFalse((self.root / "result.txt").exists())

    def test_dirty_resolved_source_is_rejected(self):
        output = self.source / "libarchive/archive_digest.c"
        output.write_bytes(output.read_bytes() + b"\n/* dirty */\n")
        result = execute(["cmake", "-P", self.script])
        self.assertNotEqual(0, result.returncode)
        self.assertIn("clean checkout of the resolved official revision", result.stdout)


if __name__ == "__main__":
    unittest.main()
