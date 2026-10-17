"""Regression tests for refusing misleading build provenance."""
import contextlib
import importlib.util
import io
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/build_traceability.py"
spec = importlib.util.spec_from_file_location("build_traceability", SCRIPT)
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class BuildTraceabilityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.config = {
            "root": str(root), "binary": str(root / "build"), "prefix": str(root / "prefix"),
            "manifest": str(root / "resolution.json"), "superbuild": True,
            "source_qt": False, "system_backend": False, "application_version": "1.0.0",
            "cmake_version": "current", "generator": "Ninja", "external_libraries": [],
            "toolchain": {"system": "test-host", "cxx_compiler": {"id": "test", "version": "1"}},
        }
        self.backend_patch = root / "cmake/patches/libarchive-compatibility.patch"
        self.backend_patch.parent.mkdir(parents=True)
        self.backend_patch.write_text("fixture compatibility patch\n", encoding="utf-8")
        self.config["libarchive_patch_sha256"] = hashlib.sha256(self.backend_patch.read_bytes()).hexdigest()
        self.entry = {"name": "libarchive", "path": "third_party/libarchive", "url": "https://example.test/libarchive.git",
                      "version": "3.9.0", "ref": "refs/heads/master", "revision": "a" * 40, "coupled_to": None,
                      "resolved_at": "2026-10-11T00:00:00Z"}
        self.manifest = {"schema_version": 1, "resolved_at": self.entry["resolved_at"], "dependencies": [self.entry]}
        self.sources = patch.object(trace, "source_dependencies", side_effect=lambda _: (self.manifest, [self.entry.copy()]))
        self.sources.start()
        self.addCleanup(self.sources.stop)
        # These tests isolate identity/prefix policy with synthetic revisions.
        # Generated source attestation has real Git fixtures in its own suite.
        self.generated_sources = patch.object(trace, "validate_generated_libarchive")
        self.generated_sources.start()
        self.addCleanup(self.generated_sources.stop)

    def test_a_changed_source_cannot_reuse_a_configured_build(self):
        trace.run(self.config, "prepare")
        self.entry["revision"] = "b" * 40
        with self.assertRaisesRegex(ValueError, "new build directory"):
            trace.run(self.config, "record-prefix")
        self.assertFalse((Path(self.config["prefix"]) / "share/AnAnArchiver/dependency-build.json").exists())

    def test_non_object_manifest_fails_before_resolver_or_metadata_changes(self):
        trace.run(self.config, "prepare")
        trace.run(self.config, "record-prefix")
        provenance = Path(self.config["binary"]) / "dependency-provenance.json"
        trace.write_json(provenance, {"build": "previous-success"})
        records = [Path(self.config["binary"]) / "dependency-build.json", provenance,
                   Path(self.config["prefix"]) / "share/AnAnArchiver/dependency-build.json",
                   Path(self.config["prefix"]) / "share/AnAnArchiver/resolved-dependencies.json"]
        previous = {path: path.read_bytes() for path in records}
        self.sources.stop()  # Exercise the real file-reading boundary, not the fixture's source mock.
        config_path = Path(self.config["root"]) / "config.json"
        config_path.write_text(json.dumps(self.config), encoding="utf-8")
        for manifest in ([], None):
            with self.subTest(manifest=manifest):
                Path(self.config["manifest"]).write_text(json.dumps(manifest), encoding="utf-8")
                diagnostics = io.StringIO()
                with patch.object(trace.subprocess, "run") as resolver, \
                        patch.object(trace, "write_json") as metadata_writer, \
                        patch.object(sys, "argv", ["build_traceability.py", "--config", str(config_path), "--operation", "record"]), \
                        contextlib.redirect_stderr(diagnostics):
                    self.assertEqual(1, trace.main())
                self.assertIn("Build traceability error: Dependency resolution manifest must be a JSON object", diagnostics.getvalue())
                self.assertNotIn("Traceback", diagnostics.getvalue())
                resolver.assert_not_called()
                metadata_writer.assert_not_called()
                self.assertEqual(previous, {path: path.read_bytes() for path in records})

    def test_changing_compilers_does_not_relabel_old_libraries(self):
        trace.run(self.config, "prepare")
        self.config["toolchain"]["cxx_compiler"]["version"] = "2"
        with self.assertRaisesRegex(ValueError, "toolchain differ"):
            trace.run(self.config, "verify")

    def test_changing_crypto_provider_requires_a_new_compiled_prefix(self):
        trace.run(self.config, "prepare")
        self.config["libarchive_cng"] = False
        with self.assertRaisesRegex(ValueError, "new build directory"):
            trace.run(self.config, "verify")

    def test_missing_or_forged_backend_patch_hash_is_rejected(self):
        for invalid in (None, "0" * 64):
            with self.subTest(hash=invalid):
                self.config["libarchive_patch_sha256"] = invalid
                with self.assertRaisesRegex(ValueError, "patch hash"):
                    trace.run(self.config, "prepare")
        self.assertFalse((Path(self.config["binary"]) / "dependency-build.json").exists())

    def test_changed_backend_patch_requires_rebuilding_the_compiled_prefix(self):
        trace.run(self.config, "prepare")
        trace.run(self.config, "record-prefix")
        self.backend_patch.write_text("updated compatibility patch\n", encoding="utf-8")
        self.config["libarchive_patch_sha256"] = hashlib.sha256(self.backend_patch.read_bytes()).hexdigest()
        self.config["binary"] += "-application"
        self.config["superbuild"] = False
        with self.assertRaisesRegex(ValueError, "new build directory"):
            trace.run(self.config, "prepare")

    def test_legacy_prefix_without_backend_patch_identity_is_rejected(self):
        trace.run(self.config, "prepare")
        trace.run(self.config, "record-prefix")
        path = Path(self.config["prefix"]) / "share/AnAnArchiver/dependency-build.json"
        previous = json.loads(path.read_text(encoding="utf-8"))
        previous["build_options"].pop("libarchive_patch_sha256")
        trace.write_json(path, previous)
        self.config["binary"] += "-application"
        self.config["superbuild"] = False
        with self.assertRaisesRegex(ValueError, "new build directory"):
            trace.run(self.config, "prepare")

    def test_a_new_resolution_time_does_not_require_recompilation(self):
        trace.run(self.config, "prepare")
        self.entry["resolved_at"] = "2026-10-12T00:00:00Z"
        self.manifest["resolved_at"] = self.entry["resolved_at"]
        trace.run(self.config, "verify")

    def test_changing_dependency_build_options_requires_rebuilding_dependencies(self):
        recipe = Path(self.config["root"]) / "cmake/Superbuild.cmake"
        recipe.parent.mkdir(exist_ok=True)
        recipe.write_text("set(CRYPTO_THREADS ON)", encoding="utf-8")
        trace.run(self.config, "prepare")
        recipe.write_text("set(CRYPTO_THREADS OFF)", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "new build directory"):
            trace.run(self.config, "verify")

    def test_untracked_old_build_artifacts_do_not_acquire_provenance(self):
        (Path(self.config["binary"]) / "dependencies/build").mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "no source identity"):
            trace.run(self.config, "prepare")

    def test_application_requires_successfully_built_dependency_prefix(self):
        self.config["superbuild"] = False
        with self.assertRaisesRegex(ValueError, "Missing compiled dependency identity"):
            trace.run(self.config, "prepare")

    def test_an_old_install_cannot_be_relabelled_from_a_fresh_build_directory(self):
        prefix = Path(self.config["prefix"])
        prefix.mkdir()
        (prefix / "archive.lib").write_bytes(b"old library")
        with self.assertRaisesRegex(ValueError, "nonempty dependency prefix"):
            trace.run(self.config, "prepare")

    def test_an_existing_shared_qt_build_requires_provenance(self):
        self.config["source_qt"] = True
        self.config["qt_build_binary"] = str(Path(self.temporary.name) / "shared-qt")
        qt_build = Path(self.config["qt_build_binary"])
        qt_build.mkdir()
        (qt_build / "CMakeCache.txt").write_text("old kit", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "existing Qt build"):
            trace.run(self.config, "prepare")

    def test_shared_qt_cache_does_not_depend_on_archive_backend_revisions(self):
        qt = {"path": "third_party/qt", "url": "https://example.test/qt.git", "revision": "c" * 40}
        previous = trace.qt_build_identity(self.config, [self.entry, qt])
        self.entry["revision"] = "b" * 40
        current = trace.qt_build_identity(self.config, [self.entry, qt])
        trace.validate_qt_cache(self.config, previous, current, Path(self.temporary.name))

    def test_shared_qt_cache_rejects_changed_qt_compilation_options(self):
        self.config["qt_options"] = ["-DBUILD_SHARED_LIBS=ON"]
        previous = trace.qt_build_identity(self.config, [])
        self.config["qt_options"] = ["-DBUILD_SHARED_LIBS=OFF"]
        current = trace.qt_build_identity(self.config, [])
        with self.assertRaisesRegex(ValueError, "different Qt options"):
            trace.validate_qt_cache(self.config, previous, current, Path(self.temporary.name))

    def test_legacy_qt_record_requires_actual_cache_options_before_upgrade(self):
        self.config["qt_options"] = ["-DFEATURE_system_png=OFF"]
        qt = {"path": "third_party/qt", "url": "https://example.test/qt.git", "revision": "c" * 40}
        current = trace.qt_build_identity(self.config, [qt])
        previous = {"dependencies": [qt], "toolchain": self.config["toolchain"], "build_recipe": {}}
        cache = Path(self.temporary.name) / "CMakeCache.txt"
        cache.write_text("QT_FEATURE_system_png:BOOL=ON\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "cannot prove option"):
            trace.validate_qt_cache(self.config, previous, current, cache.parent)
        cache.write_text("QT_FEATURE_system_png:BOOL=OFF\n", encoding="utf-8")
        trace.validate_qt_cache(self.config, previous, current, cache.parent)

    def test_prefix_from_other_sources_is_rejected(self):
        trace.run(self.config, "prepare")
        trace.run(self.config, "record-prefix")
        self.entry["revision"] = "b" * 40
        self.config["binary"] += "-application"
        self.config["superbuild"] = False
        with self.assertRaisesRegex(ValueError, "new build directory"):
            trace.run(self.config, "prepare")

    def test_application_accepts_matching_subset_of_a_source_qt_prefix(self):
        trace.run(self.config, "prepare")
        trace.run(self.config, "record-prefix")
        self.config["binary"] += "-application"
        self.config["superbuild"] = False
        trace.run(self.config, "prepare")
        trace.run(self.config, "verify")

    def test_build_record_uses_selected_developer_kit_and_reproducible_time(self):
        root = self.config["root"]
        subprocess.run(["git", "init", "-q", root], check=True)
        subprocess.run(["git", "-C", root, "-c", "user.name=fixture", "-c", "user.email=fixture@example.test",
                        "commit", "-q", "--allow-empty", "-m", "fixture"], check=True)
        self.config["external_libraries"] = [{"name": "Qt", "selection": "developer-kit", "version": "6.99.1", "revision": None}]
        trace.run(self.config, "prepare")
        trace.run(self.config, "record-prefix")
        self.config["superbuild"] = False
        with patch.dict("os.environ", {"SOURCE_DATE_EPOCH": "1700000000"}):
            trace.run(self.config, "record")
        record = json.loads((Path(self.config["binary"]) / "dependency-provenance.json").read_text(encoding="utf-8"))
        self.assertEqual(record["built_at"], "2023-11-14T22:13:20Z")
        self.assertEqual(record["dependencies"][0]["revision"], "a" * 40)
        self.assertEqual(record["external_libraries"][0]["version"], "6.99.1")
        self.assertEqual(len(record["application"]["revision"]), 40)
        self.assertEqual(record["libarchive_compatibility_patch"]["sha256"], self.config["libarchive_patch_sha256"])
        self.assertEqual(record["libarchive_compatibility_patch"]["upstream_revision"], "a" * 40)


class GeneratedLibarchiveTraceabilityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ananarchiver-generated-source-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.upstream = self.root / "third_party/libarchive"
        self.upstream.mkdir(parents=True)
        self.git("init", "--quiet", str(self.upstream))
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.symlinks", "true")
        (self.upstream / "patched.txt").write_bytes(b"original\n")
        (self.upstream / "untouched.c").write_bytes(b"upstream\n")
        (self.upstream / "include").mkdir()
        (self.upstream / "include/header.h").write_bytes(b"header\n")
        self.has_symlink = False
        try:
            (self.upstream / "source-link").symlink_to("untouched.c")
            self.has_symlink = True
        except OSError:
            pass
        self.git("add", ".")
        self.git("-c", "user.name=fixture", "-c", "user.email=fixture@example.test", "commit", "-q", "-m", "upstream")
        self.revision = self.git("rev-parse", "HEAD").strip()
        self.patch_path = self.root / "cmake/patches/libarchive-compatibility.patch"
        self.patch_path.parent.mkdir(parents=True)
        self.patch_path.write_bytes(b"diff --git a/patched.txt b/patched.txt\n"
                                    b"--- a/patched.txt\n+++ b/patched.txt\n@@ -1 +1 @@\n-original\n+compatible\n")
        self.patch_hash = hashlib.sha256(self.patch_path.read_bytes()).hexdigest()
        self.config = {"root": str(self.root), "binary": str(self.root / "build"), "prefix": str(self.root / "prefix"),
                       "superbuild": True, "system_backend": False, "source_qt": False,
                       "libarchive_patch_sha256": self.patch_hash,
                       "toolchain": {"cxx_compiler": {"id": "fixture", "version": "1"}}}
        self.backend = {"name": "libarchive", "path": "third_party/libarchive", "revision": self.revision}
        source_id = hashlib.sha256((self.revision + "\n" + self.patch_hash).encode()).hexdigest()[:16]
        self.source = Path(self.config["binary"]) / "dependencies/sources" / ("libarchive-" + source_id)
        self.source.mkdir(parents=True)
        (self.source / "patched.txt").write_bytes(b"compatible\n")
        (self.source / "untouched.c").write_bytes(b"upstream\n")
        (self.source / "include").mkdir()
        (self.source / "include/header.h").write_bytes(b"header\n")
        if self.has_symlink:
            (self.source / "source-link").symlink_to("untouched.c")
        (self.source / ".git").mkdir()
        (self.source / ".git/ignored").write_bytes(b"metadata\n")
        for name in (".prepared", ".patched-files"):
            (self.source / name).write_bytes(b"metadata\n")
        self.record_path = Path(self.config["binary"]) / "libarchive-generated-source.json"
        self.dependencies = patch.object(trace, "source_dependencies", return_value=(None, [self.backend]))
        self.dependencies.start()
        self.addCleanup(self.dependencies.stop)

    def git(self, *arguments):
        return subprocess.check_output(["git", "-C", str(self.upstream), *arguments], text=True, encoding="utf-8")

    def test_prepare_attests_the_full_official_tree_and_verify_is_read_only(self):
        trace.run(self.config, "prepare")
        record = json.loads(self.record_path.read_text(encoding="utf-8"))
        self.assertEqual(self.revision, record["upstream_revision"])
        self.assertEqual(self.patch_hash, record["patch_sha256"])
        self.assertEqual(hashlib.sha256(b"upstream\n").hexdigest(), record["files"]["untouched.c"]["sha256"])
        self.assertEqual(hashlib.sha256(b"compatible\n").hexdigest(), record["files"]["patched.txt"]["sha256"])
        self.assertFalse({".git", ".prepared", ".patched-files"} & record["files"].keys())
        before = self.record_path.read_bytes()
        for name in (".prepared", ".patched-files"):
            (self.source / name).write_bytes(b"irrelevant metadata changes\n")
        with patch.object(trace, "write_json", side_effect=AssertionError("verify must not write")), \
                patch.object(trace.subprocess, "run", side_effect=AssertionError("verify must not export or apply")):
            trace.run(self.config, "verify")
        self.assertEqual(before, self.record_path.read_bytes())
        self.assertEqual(b"original\n", (self.upstream / "patched.txt").read_bytes())
        self.assertEqual("", self.git("status", "--porcelain"))

    def test_every_build_operation_rejects_modified_missing_and_added_inputs(self):
        trace.run(self.config, "prepare")
        original_record = self.record_path.read_bytes()
        for name, content in (("patched.txt", b"tampered patch\n"), ("untouched.c", b"tampered source\n"),
                              ("include/header.h", None), ("unexpected.c", b"added source\n")):
            path = self.source / name
            original = path.read_bytes() if path.exists() else None
            if content is None:
                path.unlink()
            else:
                path.write_bytes(content)
            try:
                for operation in ("verify", "record-prefix", "record"):
                    with self.subTest(path=name, operation=operation), \
                            patch.object(trace, "write_json", side_effect=AssertionError("invalid inputs must not write")):
                        with self.assertRaisesRegex(ValueError, "Generated libarchive source differs.*Reconfigure"):
                            trace.run(self.config, operation)
                self.assertEqual(original_record, self.record_path.read_bytes())
            finally:
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)

    def test_missing_or_mismatched_attestation_requires_reconfigure_without_writes(self):
        trace.run(self.config, "prepare")
        record = json.loads(self.record_path.read_text(encoding="utf-8"))
        for invalid in (None, {**record, "upstream_revision": "b" * 40}, {**record, "patch_sha256": "0" * 64}):
            if invalid is None:
                self.record_path.unlink()
            else:
                self.record_path.write_text(json.dumps(invalid), encoding="utf-8")
            for operation in ("verify", "record-prefix", "record"):
                with self.subTest(record=invalid is not None, operation=operation), \
                        patch.object(trace, "write_json", side_effect=AssertionError("verification must not regenerate records")):
                    with self.assertRaisesRegex(ValueError, "[Gg]enerated libarchive source identity.*Reconfigure"):
                        trace.run(self.config, operation)

    def test_reconfigure_cannot_bless_an_edited_cache_or_its_forged_file_list(self):
        trace.run(self.config, "prepare")
        (self.source / "untouched.c").write_bytes(b"changed after configure\n")
        record = json.loads(self.record_path.read_text(encoding="utf-8"))
        record["files"]["untouched.c"]["sha256"] = hashlib.sha256(b"changed after configure\n").hexdigest()
        self.record_path.write_text(json.dumps(record), encoding="utf-8")
        forged = self.record_path.read_bytes()
        with patch.object(trace, "write_json", side_effect=AssertionError("prepare must reject edited source before writing")):
            with self.assertRaisesRegex(ValueError, "Generated libarchive source differs.*untouched.c"):
                trace.run(self.config, "prepare")
        self.assertEqual(forged, self.record_path.read_bytes())

    def test_symlink_targets_and_entry_types_are_checked_without_following_them(self):
        if not self.has_symlink:
            self.skipTest("Host cannot create symbolic links")
        trace.run(self.config, "prepare")
        link = self.source / "source-link"
        link.unlink()
        link.symlink_to("patched.txt")
        with self.assertRaisesRegex(ValueError, "source-link.*Reconfigure"):
            trace.run(self.config, "verify")
        link.unlink()
        link.write_bytes(b"upstream\n")
        with self.assertRaisesRegex(ValueError, "source-link.*Reconfigure"):
            trace.run(self.config, "verify")

    def test_system_and_application_builds_skip_generated_source_attestation(self):
        for option in ("system_backend", "superbuild"):
            config = {**self.config, option: option == "system_backend"}
            with self.subTest(option=option), \
                    patch.object(trace, "generated_source_files", side_effect=AssertionError("generated source must be skipped")), \
                    patch.object(trace, "write_json", side_effect=AssertionError("generated record must be skipped")):
                trace.validate_generated_libarchive(config, [], "prepare")
                trace.validate_generated_libarchive(config, [], "verify")


if __name__ == "__main__":
    unittest.main()
