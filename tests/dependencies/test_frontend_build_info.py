"""Regression coverage for dependency identities displayed in the desktop UI."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("frontend_build_info", ROOT / "scripts/write_frontend_build_info.py")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def source(name, version="1.2.3"):
    return {"name": name, "version": version, "revision": "a" * 40}


class FrontendBuildInfoTests(unittest.TestCase):
    def test_source_versions_and_full_git_revisions(self):
        identity = {"dependencies": [source("qt", "6.12.0"), source("libarchive"), source("qt/qtbase")]}
        result = GENERATOR.metadata(identity, {"external_libraries": []}, "b" * 40, True)
        self.assertEqual(result["components"]["libarchive"]["revision"], "a" * 40)
        self.assertEqual(result["components"]["qt"]["version"], "6.12.0")
        self.assertEqual(result["components"]["qt/qtbase"]["selection"], "bundled-source")
        self.assertEqual(result["applicationRevision"], "b" * 40)
        self.assertTrue(result["applicationDirty"])

    def test_external_kit_never_borrows_unused_source_revisions(self):
        config = {"external_libraries": [
            {"name": "Qt", "version": "6.12.1", "selection": "developer-kit"},
            {"name": "libarchive", "version": "3.9.0", "selection": "system"}]}
        result = GENERATOR.metadata({"dependencies": []}, config, "b" * 40, False)
        self.assertEqual(result["components"]["qt"], {"version": "6.12.1", "revision": "", "selection": "developer-kit"})
        self.assertNotIn("zlib", result["components"])
        self.assertNotIn("qt/qtbase", result["components"])

    def test_missing_or_conflicting_build_identity_is_rejected(self):
        with self.assertRaises(ValueError):
            GENERATOR.metadata({"dependencies": [source("qt")]}, {"external_libraries": []}, "b" * 40, False)
        with self.assertRaises(ValueError):
            GENERATOR.metadata({"dependencies": [source("qt"), source("libarchive"), source("qt")]},
                               {"external_libraries": []}, "b" * 40, False)
        with self.assertRaises(ValueError):
            GENERATOR.metadata({"dependencies": [source("qt"), source("libarchive")]},
                               {"external_libraries": [{"name": "Qt", "version": "6.12.1", "selection": "developer-kit"}]},
                               "b" * 40, False)

    def test_generator_embeds_prepared_record_without_resolution_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "dependency-build.json").write_text(json.dumps({"dependencies": [source("qt"), source("libarchive")]}), encoding="utf-8")
            (build / "build-trace-config.json").write_text(json.dumps({"root": str(ROOT), "external_libraries": []}), encoding="utf-8")
            output = build / "generated/info.h"
            command = [sys.executable, str(ROOT / "scripts/write_frontend_build_info.py"), "--build-dir", str(build), "--output", str(output)]
            subprocess.run(command, check=True, capture_output=True)
            payload = output.read_text(encoding="utf-8").split("ANANARCHIVER_DEPENDENCY_INFO_JSON ", 1)[1].strip()
            result = json.loads(json.loads(payload))
            self.assertEqual(result["components"]["libarchive"]["version"], "1.2.3")
            self.assertNotIn("root", result)
            modified = output.stat().st_mtime_ns
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(output.stat().st_mtime_ns, modified)


if __name__ == "__main__":
    unittest.main()
