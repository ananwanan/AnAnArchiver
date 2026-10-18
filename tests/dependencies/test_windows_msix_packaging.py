"""Exercise MSIX input validation without building or changing existing artifacts.

The tiny portable payload below is deliberately synthetic test data, including a
minimal PE header. It is never installed, signed, or presented as a real release.
An absent explicit MakeAppx path marks the boundary after input validation.
"""
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/Package-MSIX.ps1"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")
if not POWERSHELL and os.name == "nt":
    windows_powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if windows_powershell.is_file():
        POWERSHELL = str(windows_powershell)

PROVENANCE = "share/AnAnArchiver/dependency-provenance.json"
SOURCES = "share/AnAnArchiver/resolved-dependencies.json"
REQUIRED_PAYLOAD = (
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll", "Qt6Svg.dll",
    "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
    "plugins/platforms/qwindows.dll", "plugins/imageformats/qsvg.dll",
    "plugins/iconengines/qsvgicon.dll", "qml/QtQuick/qmldir", "qt.conf",
    "THIRD-PARTY-NOTICES.txt", "share/AnAnArchiver/third-party-notices.json",
)

# Keep paths/data outside PowerShell source, and use UTF-8 diagnostics on both
# Windows PowerShell 5.1 and PowerShell 7, including Chinese/emoji temp paths.
PACKAGER_WRAPPER = base64.b64encode(r"""
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$taskUtf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $taskUtf8
$OutputEncoding = $taskUtf8
$taskErrorWriter = [IO.StreamWriter]::new([Console]::OpenStandardError(), $taskUtf8)
$taskErrorWriter.AutoFlush = $true
[Console]::SetError($taskErrorWriter)
try {
    $taskParameters = @{}
    $taskJson = $env:ANANARCHIVER_MSIX_TEST_PARAMETERS | ConvertFrom-Json
    foreach ($taskProperty in $taskJson.PSObject.Properties) {
        $taskParameters[$taskProperty.Name] = $taskProperty.Value
    }
    & $env:ANANARCHIVER_MSIX_TEST_SCRIPT @taskParameters
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
""".encode("utf-16-le")).decode("ascii")


def normalize_diagnostics(text):
    return " ".join(re.sub(r"\x1b\[[0-9;]*m", "", text).replace("|", " ").split())


def snapshot(root):
    return {str(path.relative_to(root)): None if path.is_dir() else path.read_bytes()
            for path in root.rglob("*")}


def write_json(path, record):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")


def create_portable(root):
    """Create an internally consistent input fixture, not a completed build."""
    root.mkdir(parents=True)
    # Enough for the packager's architecture guard: DOS header, PE signature,
    # AMD64 machine and PE32+ optional-header magic. No executable code exists.
    image = bytearray(512)
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", image, 0x84, 0x8664)
    struct.pack_into("<H", image, 0x98, 0x20B)
    (root / "LiteArc.exe").write_bytes(image)
    for relative in REQUIRED_PAYLOAD:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if relative.endswith(".json"):
            write_json(path, [{"name": "synthetic-test-fixture", "notices": []}])
        else:
            path.write_bytes(b"synthetic MSIX preflight test fixture\n")
    (root / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=plugins\nQmlImports=qml\n", encoding="utf-8")
    build = {
        "schema_version": 1, "project_version": "1.0.0", "compiler_id": "MSVC",
        "synthetic_test_fixture": True,
        "application": {"version": "1.0.0"},
        "toolchain": {"system": "Windows", "processor": "AMD64", "build_type": "Release",
                      "cxx_compiler": {"id": "MSVC"}},
    }
    write_json(root / PROVENANCE, build)
    write_json(root / SOURCES, {"schema_version": 1, "dependencies": [], "synthetic_test_fixture": True})
    manifest = {
        "schema_version": 1, "platform": "windows-x64", "project_version": "1.0.0",
        "build": copy.deepcopy(build), "files": [],
    }
    refresh_manifest(root, manifest)
    return manifest


def refresh_manifest(root, manifest):
    manifest["files"] = [
        {"relative_path": path.relative_to(root).as_posix(), "size": path.stat().st_size,
         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        for path in sorted(root.rglob("*")) if path.is_file() and path.name != "package-manifest.json"
    ]
    write_json(root / "package-manifest.json", manifest)


@unittest.skipUnless(os.name == "nt" and POWERSHELL, "Windows PowerShell host required")
class WindowsMsixPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ananarchiver-msix-\u8def\u5f84\U0001f408-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.portable = self.root / "\u4fbf\u643a\u5305 \u7a7a\u683c"
        self.manifest = create_portable(self.portable)
        self.output = self.root / "\u5df2\u6709\u4ea7\u7269"
        self.output.mkdir()
        (self.output / "sentinel.txt").write_bytes(b"preserve output directory")
        (self.output / "LiteArc_1.0.0.0_x64.msix").write_bytes(b"preserve existing MSIX")
        self.parameters = {
            "PortableDirectory": str(self.portable), "OutputDirectory": str(self.output),
            "IdentityName": "AnAnArchiver.TestFixture", "Publisher": "CN=AnAnArchiver Tests",
            "PublisherDisplayName": "AnAnArchiver Tests", "PackageVersion": "1.0.0.0",
            "MakeAppxPath": str(self.root / "missing-sdk" / "MakeAppx.exe"),
        }

    def assert_refused(self, expected, parameters=None):
        before = snapshot(self.root)
        child_environment = {name: value for name, value in os.environ.items()
                             if name.upper() != "PSMODULEPATH"}
        child_environment.update({
            "ANANARCHIVER_MSIX_TEST_SCRIPT": str(SCRIPT),
            "ANANARCHIVER_MSIX_TEST_PARAMETERS": json.dumps(parameters or self.parameters, ensure_ascii=False),
        })
        result = subprocess.run(
            [POWERSHELL, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", PACKAGER_WRAPPER],
            cwd=ROOT, env=child_environment, capture_output=True, text=True, encoding="utf-8", timeout=30,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        diagnostics = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, diagnostics)
        self.assertIn(expected.lower(), normalize_diagnostics(diagnostics).lower(), diagnostics)
        self.assertEqual(before, snapshot(self.root), diagnostics)
        return diagnostics

    def save_manifest(self):
        write_json(self.portable / "package-manifest.json", self.manifest)

    def test_identity_names_are_checked_before_input_or_tool_discovery(self):
        for invalid in ("ab", "a" * 51, "invalid/name", "invalid name", "invalid_name", "\u5b89\u5b89\u89e3\u538b"):
            with self.subTest(identity=invalid):
                parameters = dict(self.parameters, IdentityName=invalid,
                                  PortableDirectory=str(self.root / "missing-portable"))
                self.assert_refused("IdentityName", parameters)

    def test_store_versions_require_four_bounded_parts_and_zero_revision(self):
        for invalid in ("1.0.0", "1.0.0.0.0", "0.1.0.0", "1.0.0.1", "1.65536.0.0",
                        "65536.0.0.0", "1.-1.0.0", "1.0.0.beta", " 1.0.0.0"):
            with self.subTest(version=invalid):
                parameters = dict(self.parameters, PackageVersion=invalid,
                                  PortableDirectory=str(self.root / "missing-portable"))
                self.assert_refused("PackageVersion", parameters)

    def test_publisher_must_have_a_valid_x500_common_name(self):
        for invalid in ("AnAnArchiver Tests", "O=AnAnArchiver Tests", "CN=", "CN=Tests,broken"):
            with self.subTest(publisher=invalid):
                parameters = dict(self.parameters, Publisher=invalid,
                                  PortableDirectory=str(self.root / "missing-portable"))
                self.assert_refused("Publisher", parameters)

    def test_missing_portable_manifest_is_refused(self):
        (self.portable / "package-manifest.json").unlink()
        self.assert_refused("package-manifest.json")

    def test_invalid_or_non_object_portable_manifest_is_refused(self):
        for content in ("{invalid-json", "null", "[]", '[{"schema_version": 1}]'):
            with self.subTest(content=content):
                (self.portable / "package-manifest.json").write_text(content, encoding="utf-8")
                self.assert_refused("package-manifest.json")

    def test_portable_manifest_requires_supported_schema_platform_and_build(self):
        for field, value, expected in (("schema_version", 2, "schema_version"),
                                       ("platform", "windows-arm64", "platform")):
            with self.subTest(field=field):
                manifest = copy.deepcopy(self.manifest)
                manifest[field] = value
                write_json(self.portable / "package-manifest.json", manifest)
                self.assert_refused(expected)
        for field, value, expected in (("compiler_id", "Clang", "MSVC"),
                                       ("project_version", "2.0.0", "project_version")):
            with self.subTest(build_field=field):
                manifest = copy.deepcopy(self.manifest)
                manifest["build"][field] = value
                write_json(self.portable / "package-manifest.json", manifest)
                self.assert_refused(expected)
        manifest = copy.deepcopy(self.manifest)
        manifest["build"]["toolchain"]["build_type"] = "Debug"
        write_json(self.portable / "package-manifest.json", manifest)
        self.assert_refused("Release")

    def test_both_provenance_records_are_mandatory_even_if_manifest_is_rehashed(self):
        for relative in (PROVENANCE, SOURCES):
            with self.subTest(relative=relative):
                path = self.portable / relative
                original = path.read_bytes()
                path.unlink()
                refresh_manifest(self.portable, self.manifest)
                self.assert_refused(Path(relative).name)
                path.write_bytes(original)
                refresh_manifest(self.portable, self.manifest)

    def test_provenance_must_match_build_even_when_its_hash_is_updated(self):
        provenance = copy.deepcopy(self.manifest["build"])
        provenance["project_version"] = "2.0.0"
        write_json(self.portable / PROVENANCE, provenance)
        refresh_manifest(self.portable, self.manifest)
        self.assert_refused("dependency-provenance.json")

    def test_required_runtime_and_notices_cannot_be_removed_and_rehashed(self):
        for relative in ("plugins/platforms/qwindows.dll", "msvcp140.dll", "THIRD-PARTY-NOTICES.txt"):
            with self.subTest(relative=relative):
                path = self.portable / relative
                original = path.read_bytes()
                path.unlink()
                refresh_manifest(self.portable, self.manifest)
                self.assert_refused(Path(relative).name)
                path.write_bytes(original)
                refresh_manifest(self.portable, self.manifest)

    def test_modified_manifest_payload_is_refused(self):
        (self.portable / "Qt6Core.dll").write_bytes(b"modified since portable packaging")
        self.assert_refused("Qt6Core.dll")

    def test_manifest_entries_must_exist(self):
        (self.portable / "Qt6Gui.dll").unlink()
        self.assert_refused("Qt6Gui.dll")

    def test_unlisted_payload_file_is_refused(self):
        (self.portable / "unexpected-payload.dll").write_bytes(b"not recorded by the portable packager")
        self.assert_refused("unexpected-payload.dll")

    def test_manifest_paths_cannot_escape_portable_root(self):
        for invalid in ("../outside.dll", "C:/outside.dll", "plugins/../../outside.dll"):
            with self.subTest(relative=invalid):
                manifest = copy.deepcopy(self.manifest)
                manifest["files"][0]["relative_path"] = invalid
                write_json(self.portable / "package-manifest.json", manifest)
                self.assert_refused("outside.dll")

    def test_manifest_rejects_duplicate_payload_entries(self):
        self.manifest["files"].append(copy.deepcopy(self.manifest["files"][0]))
        self.save_manifest()
        self.assert_refused(self.manifest["files"][0]["relative_path"])

    def test_same_version_release_artifacts_are_never_overwritten(self):
        stem = "LiteArc-1.0.0.0-windows-x64"
        for suffix in (".msix", ".AppxManifest.xml", "-artifacts.json"):
            for explicit_version in (True, False):
                with self.subTest(suffix=suffix, explicit_version=explicit_version):
                    artifact = self.output / (stem + suffix)
                    artifact.write_bytes(b"immutable existing release artifact")
                    parameters = dict(self.parameters)
                    if not explicit_version:
                        del parameters["PackageVersion"]
                    self.assert_refused("Existing MSIX artifact", parameters)
                    artifact.unlink()

    def test_valid_unicode_input_reaches_sdk_validation_without_mutation(self):
        # A valid fixture must reach tool resolution. Otherwise a negative test
        # could accidentally pass because an earlier, unrelated guard rejected it.
        self.assert_refused("MakeAppx")


if __name__ == "__main__":
    unittest.main()
