"""Exercise Windows packaging build recovery without compiling or deploying.

The production packager is copied beside a recording MSVC wrapper. The wrapper
never runs CMake and normally fails the build deliberately, keeping every case
before deployment, installer downloads, and artifact replacement.
"""
import base64
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

EXPECTED_PROBE = "ANANARCHIVER_HAS_CXX23_EXPECTED"
JTHREAD_PROBE = "ANANARCHIVER_HAS_STD_JTHREAD"
RECORDS = ("dependency-provenance.json", "resolved-dependencies.json")

# Paths and switches travel as data, with explicit UTF-8 diagnostics on both
# Windows PowerShell 5.1 and PowerShell 7.
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
    $taskJson = $env:ANANARCHIVER_BUILD_TEST_PARAMETERS | ConvertFrom-Json
    foreach ($taskProperty in $taskJson.PSObject.Properties) {
        $taskParameters[$taskProperty.Name] = $taskProperty.Value
    }
    & $env:ANANARCHIVER_BUILD_TEST_SCRIPT @taskParameters
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
""".encode("utf-16-le")).decode("ascii")

MSVC_WRAPPER_SHIM = r"""
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)] [string] $Command,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $CommandArguments
)
$ErrorActionPreference = 'Stop'
$taskCall = [ordered]@{ command = $Command; arguments = @($CommandArguments) }
$taskJson = $taskCall | ConvertTo-Json -Depth 5 -Compress
[IO.File]::AppendAllText($env:ANANARCHIVER_BUILD_TEST_CALLS, $taskJson + "`n", [Text.UTF8Encoding]::new($false))
if ($CommandArguments -contains '--build') {
    exit [int]$env:ANANARCHIVER_BUILD_TEST_BUILD_EXIT
}
exit [int]$env:ANANARCHIVER_BUILD_TEST_CONFIGURE_EXIT
"""


def normalize_diagnostics(text):
    return " ".join(re.sub(r"\x1b\[[0-9;]*m", "", text).replace("|", " ").split())


def snapshot(root):
    return {str(path.relative_to(root)): None if path.is_dir() else path.read_bytes()
            for path in root.rglob("*")}


@unittest.skipUnless(os.name == "nt" and POWERSHELL, "Windows PowerShell host required")
class WindowsPackagingBuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ananarchiver-package-build-\u8def\u5f84\U0001f408-")
        self.addCleanup(self.temporary.cleanup)
        # Resolve the Windows TEMP directory's possible 8.3 user-folder alias;
        # the production script expands it when normalizing task paths.
        self.root = Path(self.temporary.name).resolve()
        repository = self.root / "\u4ed3\u5e93 with spaces"
        scripts = repository / "scripts"
        scripts.mkdir(parents=True)
        self.script = scripts / SCRIPT.name
        shutil.copyfile(SCRIPT, self.script)
        (scripts / "Invoke-Msvc.ps1").write_text(MSVC_WRAPPER_SHIM, encoding="utf-8")
        self.source = self.root / "\u539f\u914d\u7f6e source with spaces \U0001f408"
        self.source.mkdir()
        self.build = self.root / "\u6784\u5efa build with spaces \U0001f408"
        self.build.mkdir()
        # An existing executable alone must never be upgraded into a traceable
        # Release build merely because the simulated build returns success.
        (self.build / "LiteArc.exe").write_bytes(b"synthetic existing executable; not a real release")
        self.output = self.root / "\u5df2\u6709 artifacts with spaces"
        stage = self.output / "LiteArc-1.0.0-windows-x64"
        stage.mkdir(parents=True)
        (self.output / "sentinel.txt").write_bytes(b"preserve artifact directory")
        (stage / "sentinel.txt").write_bytes(b"preserve portable directory")
        (self.output / "LiteArc-1.0.0-windows-x64.zip").write_bytes(b"preserve existing ZIP")
        (self.output / "LiteArc-1.0.0-windows-x64-setup.exe").write_bytes(b"preserve existing installer")
        self.calls = self.root / "wrapper-calls.jsonl"
        self.parameters = {
            "BuildDirectory": str(self.build), "OutputDirectory": str(self.output),
            "Jobs": 7, "BootstrapInstaller": True,
        }

    def write_cache(self, directory=None, probes=None, *, source=None):
        directory = self.build if directory is None else directory
        directory.mkdir(parents=True, exist_ok=True)
        source = self.source if source is None else source
        values = {
            "CMAKE_HOME_DIRECTORY:INTERNAL": str(source),
            "CMAKE_BUILD_TYPE:STRING": "Release",
            "ANANARCHIVER_UNRELATED_FAILED_CHECK:INTERNAL": "",
        }
        values.update({key + ":INTERNAL": value for key, value in (probes or {}).items()})
        (directory / "CMakeCache.txt").write_text(
            "".join(f"{key}={value}\n" for key, value in values.items()), encoding="utf-8")

    def run_packager(self, *, configure_exit=0, build_exit=37, parameters=None):
        if self.calls.exists():
            self.calls.unlink()
        before_build = snapshot(self.build)
        before_output = snapshot(self.output)
        child_environment = {name: value for name, value in os.environ.items()
                             if name.upper() != "PSMODULEPATH"}
        child_environment.update({
            "ANANARCHIVER_BUILD_TEST_SCRIPT": str(self.script),
            "ANANARCHIVER_BUILD_TEST_PARAMETERS": json.dumps(
                self.parameters if parameters is None else parameters, ensure_ascii=False),
            "ANANARCHIVER_BUILD_TEST_CALLS": str(self.calls),
            "ANANARCHIVER_BUILD_TEST_CONFIGURE_EXIT": str(configure_exit),
            "ANANARCHIVER_BUILD_TEST_BUILD_EXIT": str(build_exit),
        })
        result = subprocess.run(
            [POWERSHELL, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", PACKAGER_WRAPPER],
            cwd=self.root, env=child_environment, capture_output=True, text=True, encoding="utf-8", timeout=30,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        diagnostics = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, diagnostics)
        self.assertEqual(before_build, snapshot(self.build), diagnostics)
        self.assertEqual(before_output, snapshot(self.output), diagnostics)
        for name in RECORDS:
            self.assertFalse((self.build / name).exists(), diagnostics)
        calls = [json.loads(line) for line in self.calls.read_text(encoding="utf-8").splitlines()] \
            if self.calls.exists() else []
        return calls, normalize_diagnostics(diagnostics)

    def assert_build_call(self, call):
        self.assertEqual("cmake", call["command"])
        self.assertEqual(["--build", str(self.build), "--parallel", "7"], call["arguments"])

    def assert_configure_call(self, call, directory, probes, *, source=None):
        self.assertEqual("cmake", call["command"])
        arguments = call["arguments"]
        options = {"-S": [], "-B": [], "-U": []}
        self.assertEqual(0, len(arguments) % 2, arguments)
        for index in range(0, len(arguments), 2):
            option, value = arguments[index:index + 2]
            self.assertIn(option, options, arguments)
            options[option].append(value)
        self.assertEqual([str(self.source if source is None else source)], options["-S"])
        self.assertEqual([str(directory)], options["-B"])
        self.assertCountEqual(probes, options["-U"])

    def test_build_uses_msvc_wrapper_and_preserves_unicode_and_space_arguments(self):
        self.write_cache(probes={EXPECTED_PROBE: "1", JTHREAD_PROBE: "1"})
        calls, diagnostics = self.run_packager()
        self.assertIn("exited with code 37", diagnostics)
        self.assertEqual(1, len(calls), diagnostics)
        self.assert_build_call(calls[0])

    def test_absent_probes_do_not_trigger_a_separate_reconfigure(self):
        self.write_cache()
        calls, diagnostics = self.run_packager()
        self.assertEqual(1, len(calls), diagnostics)
        self.assert_build_call(calls[0])

    def test_only_failed_project_probes_are_retried(self):
        for failed in ((EXPECTED_PROBE,), (JTHREAD_PROBE,), (EXPECTED_PROBE, JTHREAD_PROBE)):
            with self.subTest(failed=failed):
                self.write_cache(probes={key: "" if key in failed else "1"
                                         for key in (EXPECTED_PROBE, JTHREAD_PROBE)})
                calls, diagnostics = self.run_packager()
                self.assertEqual(2, len(calls), diagnostics)
                self.assert_configure_call(calls[0], self.build, failed)
                self.assert_build_call(calls[1])

    def test_failed_superbuild_application_probes_are_retried_in_their_own_cache(self):
        self.write_cache(probes={EXPECTED_PROBE: "1", JTHREAD_PROBE: "1"})
        application = self.build / "application"
        application_source = self.source / "\u5e94\u7528 application source"
        application_source.mkdir()
        self.write_cache(application, {EXPECTED_PROBE: "1", JTHREAD_PROBE: ""}, source=application_source)
        calls, diagnostics = self.run_packager()
        self.assertEqual(2, len(calls), diagnostics)
        self.assert_configure_call(calls[0], application, [JTHREAD_PROBE], source=application_source)
        self.assert_build_call(calls[1])

    def test_root_and_application_failed_probes_are_both_retried(self):
        self.write_cache(probes={EXPECTED_PROBE: "", JTHREAD_PROBE: "1"})
        application = self.build / "application"
        self.write_cache(application, {EXPECTED_PROBE: "1", JTHREAD_PROBE: ""})
        calls, diagnostics = self.run_packager()
        self.assertEqual(3, len(calls), diagnostics)
        configure_calls = {call["arguments"][call["arguments"].index("-B") + 1]: call
                           for call in calls[:-1]}
        self.assertCountEqual([str(self.build), str(application)], configure_calls)
        self.assert_configure_call(configure_calls[str(self.build)], self.build, [EXPECTED_PROBE])
        self.assert_configure_call(configure_calls[str(application)], application, [JTHREAD_PROBE])
        self.assert_build_call(calls[-1])

    def test_reconfigure_failure_stops_before_build_and_preserves_artifacts(self):
        self.write_cache(probes={EXPECTED_PROBE: "1", JTHREAD_PROBE: ""})
        calls, diagnostics = self.run_packager(configure_exit=23)
        self.assertIn("exited with code 23", diagnostics)
        self.assertEqual(1, len(calls), diagnostics)
        self.assert_configure_call(calls[0], self.build, [JTHREAD_PROBE])

    def test_build_failure_after_reconfigure_preserves_artifacts(self):
        self.write_cache(probes={EXPECTED_PROBE: "1", JTHREAD_PROBE: ""})
        calls, diagnostics = self.run_packager(build_exit=41)
        self.assertIn("exited with code 41", diagnostics)
        self.assertEqual(2, len(calls), diagnostics)
        self.assert_configure_call(calls[0], self.build, [JTHREAD_PROBE])
        self.assert_build_call(calls[1])

    def test_successful_build_does_not_fabricate_missing_build_records(self):
        self.write_cache(probes={EXPECTED_PROBE: "1", JTHREAD_PROBE: "1"})
        calls, diagnostics = self.run_packager(build_exit=0)
        self.assertIn("required build records are missing", diagnostics)
        self.assertEqual(1, len(calls), diagnostics)
        self.assert_build_call(calls[0])
        for name in RECORDS:
            self.assertIn(name, diagnostics)

    def test_skip_build_does_not_initialize_msvc_or_retry_failed_probes(self):
        self.write_cache(probes={EXPECTED_PROBE: "", JTHREAD_PROBE: ""})
        calls, diagnostics = self.run_packager(parameters={**self.parameters, "SkipBuild": True})
        self.assertIn("required build records are missing", diagnostics)
        self.assertEqual([], calls, diagnostics)


if __name__ == "__main__":
    unittest.main()
