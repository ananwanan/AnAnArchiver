"""Check Android configure paths without invoking an Android build or SDK tools."""
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/Build-Android.ps1"
POWERSHELLS = list(filter(None, (
    shutil.which("pwsh"), shutil.which("powershell"),
    str(Path(os.environ.get("SystemRoot", "C:/Windows")) /
        "System32/WindowsPowerShell/v1.0/powershell.exe") if os.name == "nt" else None,
)))
POWERSHELLS = list({os.path.normcase(host): host for host in POWERSHELLS
                   if Path(host).is_file()}.values())
CMAKE = shutil.which("cmake")
NINJA = shutil.which("ninja")
ENVIRONMENT_NAMES = ("JAVA_HOME", "QTDIR", "JAVA_TOOL_OPTIONS", "GRADLE_OPTS")

# Keep Unicode paths as JSON data and explicitly encode the captured streams on
# both Windows PowerShell 5.1 and PowerShell 7. Catching the deliberate configure
# failure also lets the fixture observe the production script's finally block.
PACKAGER_WRAPPER = base64.b64encode(r"""
$ErrorActionPreference = 'Stop'
$taskUtf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $taskUtf8
$OutputEncoding = $taskUtf8
$taskErrorWriter = [IO.StreamWriter]::new([Console]::OpenStandardError(), $taskUtf8)
$taskErrorWriter.AutoFlush = $true
[Console]::SetError($taskErrorWriter)
Set-Location -LiteralPath $env:ANANARCHIVER_ANDROID_TEST_WORKING_DIRECTORY
$taskResult = [ordered]@{ before = @{}; after = @{}; error = $null }
foreach ($taskName in @('JAVA_HOME', 'QTDIR', 'JAVA_TOOL_OPTIONS', 'GRADLE_OPTS')) {
    $taskResult.before[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
}
try {
    $taskParameters = @{}
    $taskJson = $env:ANANARCHIVER_ANDROID_TEST_PARAMETERS | ConvertFrom-Json
    foreach ($taskProperty in $taskJson.PSObject.Properties) {
        $taskParameters[$taskProperty.Name] = $taskProperty.Value
    }
    & $env:ANANARCHIVER_ANDROID_TEST_SCRIPT @taskParameters
} catch {
    $taskResult.error = $_.Exception.Message
    [Console]::Error.WriteLine($_.Exception.Message)
} finally {
    foreach ($taskName in @('JAVA_HOME', 'QTDIR', 'JAVA_TOOL_OPTIONS', 'GRADLE_OPTS')) {
        $taskResult.after[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
    }
    [IO.File]::WriteAllText($env:ANANARCHIVER_ANDROID_TEST_RESULT,
        ($taskResult | ConvertTo-Json -Depth 5), $taskUtf8)
}
if ($taskResult.error) { exit 1 }
""".encode("utf-16-le")).decode("ascii")

CMAKE_SHIM = r"""
$taskCall = [ordered]@{
    arguments = @($args)
    javaHome = $env:JAVA_HOME
    qtDir = $env:QTDIR
    javaToolOptions = $env:JAVA_TOOL_OPTIONS
    gradleOptions = $env:GRADLE_OPTS
}
$taskJson = $taskCall | ConvertTo-Json -Depth 5 -Compress
[IO.File]::AppendAllText($env:ANANARCHIVER_ANDROID_TEST_CALLS,
    $taskJson + "`n", [Text.UTF8Encoding]::new($false))
exit 37
"""


def cmake_path(path):
    return str(path.resolve()).replace("\\", "/")


def snapshot(root):
    return {str(path.relative_to(root)): None if path.is_dir() else path.read_bytes()
            for path in root.rglob("*")}


@unittest.skipUnless(os.name == "nt" and POWERSHELLS, "Windows PowerShell host required")
class AndroidPackagingPathTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ananarchiver-android-\u8def\u5f84-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.repository = self.root / "\u4ed3\u5e93 source with spaces"
        scripts = self.repository / "scripts"
        scripts.mkdir(parents=True)
        self.script = scripts / SCRIPT.name
        shutil.copyfile(SCRIPT, self.script)
        self.shim = self.root / "record cmake.ps1"
        self.shim.write_text(CMAKE_SHIM, encoding="utf-8")
        self.paths = {
            "QtAndroidDir": self.root / "\u5de5\u5177 Qt Android",
            "QtHostDir": self.root / "\u5de5\u5177 Qt host",
            "AndroidNdkDir": self.root / "\u5de5\u5177 Android NDK",
            "AndroidSdkDir": self.root / "\u5de5\u5177 Android SDK",
            "JavaHome": self.root / "\u5de5\u5177 Java JDK",
            "BuildDirectory": self.root / "\u6784\u5efa build with spaces",
            "ArtifactDirectory": self.root / "\u4ea7\u7269 artifacts with spaces",
        }
        for name, marker in (
            ("QtAndroidDir", "lib/cmake/Qt6/qt.toolchain.cmake"),
            ("QtHostDir", "bin/androiddeployqt.exe"),
            ("AndroidNdkDir", "build/cmake/android.toolchain.cmake"),
            ("AndroidSdkDir", "platforms/android-36/android.jar"),
            ("JavaHome", "bin/java.exe"),
        ):
            marker_path = self.paths[name] / marker
            marker_path.parent.mkdir(parents=True)
            marker_path.write_bytes(b"toolchain existence fixture; never executed")
        for name in ("QtAndroidDir", "QtHostDir"):
            self.write_qt_version(name, "6.12.0")
        for name in ("BuildDirectory", "ArtifactDirectory"):
            self.paths[name].mkdir()
            (self.paths[name] / "sentinel.txt").write_bytes(b"keep existing output")
        self.calls = self.root / "cmake-calls.jsonl"
        self.result = self.root / "result.json"

    def write_qt_version(self, kit_name, version):
        metadata = self.paths[kit_name] / "mkspecs/qconfig.pri"
        metadata.parent.mkdir(parents=True, exist_ok=True)
        metadata.write_text(f"QT_ARCH = arm64\nQT_VERSION = {version}\n", encoding="utf-8")

    def write_manifest(self, version):
        manifest = self.repository / "out/dependencies/resolved-dependencies.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({
            "schema_version": 1,
            "dependencies": [{"name": "zlib", "version": "1.3.2"},
                             {"name": "qt", "version": version}],
        }), encoding="utf-8")

    def run_packager(self, host, parameters=None, environment=None, expected_error=None):
        for record in (self.calls, self.result):
            record.unlink(missing_ok=True)
        parameters = ({name: str(path) for name, path in self.paths.items()}
                      if parameters is None else dict(parameters))
        parameters.update({"CMakeCommand": str(self.shim), "Jobs": 3})
        child_environment = {name: value for name, value in os.environ.items()
                             if name.upper() not in ("PSMODULEPATH", "ANDROID_HOME", "ANDROID_SDK_ROOT")}
        child_environment.update({
            "JAVA_HOME": str(self.paths["JavaHome"]),
            "QTDIR": "existing Qt environment",
            "JAVA_TOOL_OPTIONS": "-Dexisting.java.option=true",
            "GRADLE_OPTS": "-Dexisting.gradle.option=true",
            "ANANARCHIVER_ANDROID_TEST_SCRIPT": str(self.script),
            "ANANARCHIVER_ANDROID_TEST_PARAMETERS": json.dumps(parameters, ensure_ascii=False),
            "ANANARCHIVER_ANDROID_TEST_CALLS": str(self.calls),
            "ANANARCHIVER_ANDROID_TEST_RESULT": str(self.result),
            "ANANARCHIVER_ANDROID_TEST_WORKING_DIRECTORY": str(self.root),
        })
        child_environment.update(environment or {})
        before = snapshot(self.repository)
        before_tools = {name: snapshot(path) for name, path in self.paths.items()}
        process = subprocess.run(
            [host, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", PACKAGER_WRAPPER],
            # Keep the process working directory distinct from the PowerShell
            # location so .NET GetFullPath cannot accidentally resolve relative
            # user arguments against the wrong directory.
            cwd=self.repository, env=child_environment, capture_output=True, text=True,
            encoding="utf-8", timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        diagnostics = process.stdout + process.stderr
        self.assertNotEqual(0, process.returncode, diagnostics)
        for fragment in expected_error or ("failed with exit code 37",):
            self.assertIn(fragment, diagnostics)
        result = json.loads(self.result.read_text(encoding="utf-8"))
        self.assertEqual(result["before"], result["after"], diagnostics)
        self.assertEqual(set(ENVIRONMENT_NAMES), set(result["before"]))
        self.assertEqual(before, snapshot(self.repository), diagnostics)
        self.assertEqual(before_tools, {name: snapshot(path) for name, path in self.paths.items()}, diagnostics)
        calls = [json.loads(line) for line in self.calls.read_text(encoding="utf-8").splitlines()] \
            if self.calls.exists() else []
        if expected_error:
            self.assertEqual([], calls, diagnostics)
            return None
        # The first configure failure must stop before --build, APK inspection,
        # artifact publishing, or execution of any toolchain marker file.
        self.assertEqual(1, len(calls), diagnostics)
        self.assertNotIn("--build", calls[0]["arguments"])
        self.assertEqual(cmake_path(self.paths["JavaHome"]), calls[0]["javaHome"])
        self.assertEqual(cmake_path(self.paths["QtAndroidDir"]), calls[0]["qtDir"])
        self.assertEqual("-Dexisting.java.option=true", calls[0]["javaToolOptions"])
        self.assertEqual("-Dexisting.gradle.option=true -Dorg.gradle.daemon=false", calls[0]["gradleOptions"])
        return calls[0]["arguments"]

    def assert_paths(self, arguments, build=None):
        self.assertEqual(cmake_path(self.repository), arguments[arguments.index("-S") + 1])
        self.assertEqual(cmake_path(self.paths["BuildDirectory"] if build is None else build),
                         arguments[arguments.index("-B") + 1])
        cache_arguments = dict(argument[2:].split("=", 1) for argument in arguments
                               if argument.startswith("-D"))
        expected = {
            "CMAKE_TOOLCHAIN_FILE:FILEPATH": cmake_path(
                self.paths["QtAndroidDir"] / "lib/cmake/Qt6/qt.toolchain.cmake"),
            "CMAKE_PREFIX_PATH:PATH": cmake_path(self.paths["QtAndroidDir"]),
            "QT_HOST_PATH:PATH": cmake_path(self.paths["QtHostDir"]),
            "ANDROID_SDK_ROOT:PATH": cmake_path(self.paths["AndroidSdkDir"]),
            "ANDROID_NDK_ROOT:PATH": cmake_path(self.paths["AndroidNdkDir"]),
        }
        for key, value in expected.items():
            self.assertEqual(value, cache_arguments.get(key), arguments)
            self.assertNotIn("\\", cache_arguments[key])
        return {key.split(":", 1)[0]: value for key, value in expected.items()}

    def test_absolute_windows_paths_preserve_spaces_and_unicode(self):
        for host in POWERSHELLS:
            with self.subTest(host=host):
                self.assert_paths(self.run_packager(host))

    def test_relative_paths_resolve_against_invoking_directory(self):
        parameters = {name: ".\\" + str(path.relative_to(self.root))
                      for name, path in self.paths.items()}
        parameters["BuildDirectory"] = ".\\unused child\\..\\" + self.paths["BuildDirectory"].name
        parameters["ArtifactDirectory"] = ".\\unused child\\..\\" + self.paths["ArtifactDirectory"].name
        for host in POWERSHELLS:
            with self.subTest(host=host):
                self.assert_paths(self.run_packager(host, parameters))

    def test_sdk_environment_fallbacks_and_default_build_directory(self):
        parameters = {name: str(path) for name, path in self.paths.items()
                      if name not in ("AndroidSdkDir", "JavaHome", "BuildDirectory", "ArtifactDirectory")}
        for host in POWERSHELLS:
            for sdk_variable in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
                with self.subTest(host=host, sdk_variable=sdk_variable):
                    arguments = self.run_packager(host, parameters,
                                                  {sdk_variable: str(self.paths["AndroidSdkDir"])})
                    self.assert_paths(arguments, self.repository / "out/build/android-arm64-v8a-release")

    def test_mismatched_qt_kits_fail_before_configure(self):
        self.write_qt_version("QtAndroidDir", "6.11.2")
        for host in POWERSHELLS:
            with self.subTest(host=host):
                self.run_packager(host, expected_error=("6.11.2", "6.12.0"))

    def test_qt_version_must_match_dependency_manifest_before_configure(self):
        self.write_manifest("6.13.0")
        for host in POWERSHELLS:
            with self.subTest(host=host):
                self.run_packager(host, expected_error=("Qt kit 6.12.0", "resolved Qt 6.13.0"))

    def test_matching_manifest_version_reaches_configure(self):
        self.write_manifest("6.12.0")
        for host in POWERSHELLS:
            with self.subTest(host=host):
                self.assert_paths(self.run_packager(host))

    def test_missing_qt_version_metadata_fails_before_configure(self):
        (self.paths["QtAndroidDir"] / "mkspecs/qconfig.pri").unlink()
        for host in POWERSHELLS:
            with self.subTest(host=host):
                self.run_packager(host, expected_error=("qconfig.pri",))

    @unittest.skipUnless(CMAKE and NINJA, "CMake and Ninja required for ExternalProject round trip")
    def test_externalproject_log_configure_accepts_recorded_sdk_paths(self):
        paths = self.assert_paths(self.run_packager(POWERSHELLS[0]))
        source = self.root / "external project source"
        child = source / "child"
        child.mkdir(parents=True)
        observed = self.root / "observed-paths.txt"
        names = ("ANDROID_SDK_ROOT", "ANDROID_NDK_ROOT", "QT_HOST_PATH")
        observed_values = "\\n".join("${" + name + "}" for name in names)
        (child / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\nproject(AndroidPathChild NONE)\n"
            f'file(WRITE "{cmake_path(observed)}" "{observed_values}")\n', encoding="utf-8")
        # Superbuild forwards these paths as STRING to children. LOG_CONFIGURE
        # serializes that command into a CMake script: raw D:\\Sdk values fail
        # there with an invalid escape before the child's configure can run.
        forwarded = "\n".join(f'    "-D{name}:STRING={paths[name]}"' for name in names)
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\nproject(AndroidPathParent NONE)\n"
            "include(ExternalProject)\nExternalProject_Add(android_path_probe\n"
            f'  SOURCE_DIR "{cmake_path(child)}"\n'
            f'  BINARY_DIR "{cmake_path(self.root / "child-build")}"\n'
            f"  CMAKE_ARGS\n{forwarded}\n"
            '  BUILD_COMMAND "" INSTALL_COMMAND "" LOG_CONFIGURE ON)\n', encoding="utf-8")
        build = self.root / "external project build"
        for arguments in (
            [CMAKE, "-S", str(source), "-B", str(build), "-G", "Ninja",
             f"-DCMAKE_MAKE_PROGRAM:FILEPATH={cmake_path(Path(NINJA))}"],
            [CMAKE, "--build", str(build)],
        ):
            process = subprocess.run(arguments, capture_output=True, text=True,
                                     encoding="utf-8", errors="replace", timeout=30,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        self.assertEqual([paths[name] for name in names], observed.read_text(encoding="utf-8").splitlines())


if __name__ == "__main__":
    unittest.main()
