"""Run the production packager against incomplete builds without touching artifacts."""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/Package-Windows.ps1"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")
if not POWERSHELL and os.name == "nt":
    windows_powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if windows_powershell.is_file():
        POWERSHELL = str(windows_powershell)
RECORDS = ("dependency-provenance.json", "resolved-dependencies.json")
# Windows PowerShell can emit redirected host diagnostics in its local code page.
# Set the child streams explicitly and write caught script errors as UTF-8. Paths
# travel through environment variables rather than becoming PowerShell source.
PACKAGER_WRAPPER = base64.b64encode(r"""
$ErrorActionPreference = 'Stop'
$taskUtf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $taskUtf8
$OutputEncoding = $taskUtf8
$taskErrorWriter = [IO.StreamWriter]::new([Console]::OpenStandardError(), $taskUtf8)
$taskErrorWriter.AutoFlush = $true
[Console]::SetError($taskErrorWriter)
try {
    & $env:ANANARCHIVER_PREFLIGHT_SCRIPT -BuildDirectory $env:ANANARCHIVER_PREFLIGHT_BUILD -OutputDirectory $env:ANANARCHIVER_PREFLIGHT_OUTPUT -SkipBuild -BootstrapInstaller
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
""".encode("utf-16-le")).decode("ascii")


def normalize_diagnostics(text):
    # PowerShell's diagnostic gutter can split a sentence across wrapped lines.
    return " ".join(re.sub(r"\x1b\[[0-9;]*m", "", text).replace("|", " ").split())


def snapshot(root):
    return {str(path.relative_to(root)): None if path.is_dir() else path.read_bytes()
            for path in root.rglob("*")}


@unittest.skipUnless(os.name == "nt" and POWERSHELL, "Windows PowerShell host required")
class WindowsPackagingPreflightTests(unittest.TestCase):
    def assert_refused(self, layout, records, expected, *, require_build_guidance=True):
        with tempfile.TemporaryDirectory(prefix="ananarchiver-package-preflight-\u8def\u5f84\U0001f408-") as temporary:
            root = Path(temporary)
            build = root / "build"
            application = build if layout == "direct" else build / "application"
            application.mkdir(parents=True)
            # Deliberately not an executable or a formal build identity. A missing
            # record must be diagnosed before validating or deploying this fixture.
            (application / "LiteArc.exe").write_bytes(b"negative-test-fixture")
            for name, contents in records.items():
                (application / name).write_text(contents, encoding="utf-8")
            output = root / "artifacts"
            stage = output / "LiteArc-1.0.0-windows-x64"
            stage.mkdir(parents=True)
            (output / "sentinel.txt").write_bytes(b"keep artifact root")
            (stage / "sentinel.txt").write_bytes(b"keep existing portable directory")
            (output / "LiteArc-1.0.0-windows-x64.zip").write_bytes(b"keep existing ZIP")
            before = snapshot(root)
            # Each PowerShell host needs its own standard module paths. An
            # inherited pwsh path can hide Get-FileHash in Windows PowerShell.
            child_environment = {name: value for name, value in os.environ.items()
                                 if name.upper() != "PSMODULEPATH"}
            child_environment.update({
                "ANANARCHIVER_PREFLIGHT_SCRIPT": str(SCRIPT),
                "ANANARCHIVER_PREFLIGHT_BUILD": str(build),
                "ANANARCHIVER_PREFLIGHT_OUTPUT": str(output),
            })
            result = subprocess.run(
                [POWERSHELL, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", PACKAGER_WRAPPER],
                cwd=ROOT, env=child_environment, capture_output=True, text=True, encoding="utf-8", timeout=30,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            diagnostics = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, diagnostics)
            # PowerShell wraps diagnostics at its output width and may style them.
            normalized = normalize_diagnostics(diagnostics)
            self.assertIn(expected, normalized)
            if require_build_guidance:
                self.assertIn("new build directory", normalized)
                self.assertIn("do not create or copy records to label old binaries", normalized.lower())
                self.assertIn("packaging/windows/README.md", diagnostics)
                # The script can expand Windows short names in the temporary root.
                self.assertIn(root.name, diagnostics)
            after = snapshot(root)
            self.assertEqual(before, after, diagnostics)
            return diagnostics

    def test_missing_records_are_actionable_for_direct_and_superbuild(self):
        for layout in ("direct", "superbuild"):
            for missing in ((RECORDS[0],), (RECORDS[1],), RECORDS):
                with self.subTest(layout=layout, missing=missing):
                    records = {name: '{"negative_test_fixture": true}'
                               for name in RECORDS if name not in missing}
                    diagnostics = self.assert_refused(layout, records, "required build records are missing")
                    normalized = normalize_diagnostics(diagnostics)
                    self.assertIn("-SkipBuild only packages a Release build completed", normalized)
                    for name in missing:
                        self.assertIn(name, diagnostics)

    def test_invalid_json_is_refused_before_artifact_changes(self):
        for layout in ("direct", "superbuild"):
            for invalid in RECORDS:
                with self.subTest(layout=layout, invalid=invalid):
                    records = {name: '{"negative_test_fixture": true}' for name in RECORDS}
                    records[invalid] = "{invalid-json"
                    diagnostics = self.assert_refused(layout, records, "Invalid build record")
                    self.assertIn(invalid, diagnostics)

    def test_non_object_records_are_refused_before_artifact_changes(self):
        for layout in ("direct", "superbuild"):
            for contents in ("[]", "null", '[{"negative_test_fixture": true}]', "42"):
                with self.subTest(layout=layout, contents=contents):
                    records = {RECORDS[0]: contents, RECORDS[1]: '{"negative_test_fixture": true}'}
                    self.assert_refused(layout, records, "expected a JSON object from a completed build")

    def backend_records(self):
        revision = "a" * 40
        patch_path = "cmake/patches/libarchive-compatibility.patch"
        provenance = {"libarchive_compatibility_patch": {
            "path": patch_path,
            "sha256": hashlib.sha256((ROOT / patch_path).read_bytes()).hexdigest(),
            "upstream_revision": revision,
        }}
        sources = {"dependencies": [{"name": "libarchive", "revision": revision}]}
        return provenance, sources

    def assert_patch_preflight(self, provenance, sources, expected, *, layout="direct"):
        records = {RECORDS[0]: json.dumps(provenance), RECORDS[1]: json.dumps(sources)}
        return self.assert_refused(layout, records, expected, require_build_guidance=False)

    def test_bundled_backend_patch_is_required_for_direct_and_superbuild(self):
        for layout in ("direct", "superbuild"):
            with self.subTest(layout=layout):
                _, sources = self.backend_records()
                self.assert_patch_preflight({}, sources, "requires its matching compatibility patch record", layout=layout)

    def test_malformed_backend_patch_records_are_refused_before_artifact_changes(self):
        for invalid in (None, [], "not a record", {}, {"path": "cmake/patches/libarchive-compatibility.patch"}):
            with self.subTest(record=invalid):
                provenance, sources = self.backend_records()
                provenance["libarchive_compatibility_patch"] = invalid
                self.assert_patch_preflight(provenance, sources, "patch record differs from the resolved backend")
        for missing in ("path", "sha256", "upstream_revision"):
            with self.subTest(missing=missing):
                provenance, sources = self.backend_records()
                del provenance["libarchive_compatibility_patch"][missing]
                self.assert_patch_preflight(provenance, sources, "patch record differs from the resolved backend")

    def test_backend_patch_path_hash_and_revision_must_match_the_build(self):
        for field, value, expected in (
                ("path", "../libarchive-compatibility.patch", "patch record differs from the resolved backend"),
                ("path", "C:/libarchive-compatibility.patch", "patch record differs from the resolved backend"),
                ("sha256", "not a hash", "patch record differs from the resolved backend"),
                ("sha256", "0" * 64, "compatibility patch differs from its build record"),
                ("upstream_revision", "b" * 40, "patch record differs from the resolved backend")):
            with self.subTest(field=field, value=value):
                provenance, sources = self.backend_records()
                provenance["libarchive_compatibility_patch"][field] = value
                self.assert_patch_preflight(provenance, sources, expected)

    def test_orphan_and_ambiguous_backend_patch_records_are_refused(self):
        for backend_count in (0, 2):
            with self.subTest(backend_count=backend_count):
                provenance, sources = self.backend_records()
                sources["dependencies"] *= backend_count
                self.assert_patch_preflight(provenance, sources, "requires its matching compatibility patch record")

    def test_matching_patch_and_system_backend_pass_patch_preflight(self):
        for layout in ("direct", "superbuild"):
            for bundled in (True, False):
                with self.subTest(layout=layout, bundled=bundled):
                    provenance, sources = self.backend_records() if bundled else ({}, {"dependencies": []})
                    # Reaching the next production guard proves patch validation
                    # accepted the records without staging or deploying anything.
                    self.assert_patch_preflight(provenance, sources, "application is not a Windows executable", layout=layout)


if __name__ == "__main__":
    unittest.main()
