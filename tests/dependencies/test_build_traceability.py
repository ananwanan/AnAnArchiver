"""Regression tests for refusing misleading build provenance."""
import contextlib
import importlib.util
import io
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
        self.entry = {"name": "libarchive", "path": "third_party/libarchive", "url": "https://example.test/libarchive.git",
                      "version": "3.9.0", "ref": "refs/heads/master", "revision": "a" * 40, "coupled_to": None,
                      "resolved_at": "2026-10-11T00:00:00Z"}
        self.manifest = {"schema_version": 1, "resolved_at": self.entry["resolved_at"], "dependencies": [self.entry]}
        self.sources = patch.object(trace, "source_dependencies", side_effect=lambda _: (self.manifest, [self.entry.copy()]))
        self.sources.start()
        self.addCleanup(self.sources.stop)

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

    def test_a_new_resolution_time_does_not_require_recompilation(self):
        trace.run(self.config, "prepare")
        self.entry["resolved_at"] = "2026-10-12T00:00:00Z"
        self.manifest["resolved_at"] = self.entry["resolved_at"]
        trace.run(self.config, "verify")

    def test_changing_dependency_build_options_requires_rebuilding_dependencies(self):
        recipe = Path(self.config["root"]) / "cmake/Superbuild.cmake"
        recipe.parent.mkdir()
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


if __name__ == "__main__":
    unittest.main()
