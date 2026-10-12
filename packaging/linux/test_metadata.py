#!/usr/bin/env python3
"""Portable metadata and isolated XDG registration regression checks.

These tests do not modify the user's desktop or claim Linux/macOS GUI coverage.
Git Bash may exercise the registration scripts on a Windows development host.
"""
from __future__ import annotations

import configparser
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import unittest
from collect_licenses import collect

ROOT = Path(__file__).resolve().parents[2]
LINUX = ROOT / "packaging/linux"
DESKTOP_ID = "com.ananwanan.AnAnArchiver"


def desktop(path: Path) -> configparser.ConfigParser:
    result = configparser.ConfigParser(interpolation=None, strict=True)
    result.optionxform = str
    result.read(path, encoding="utf-8")
    return result


def bash_executable() -> str | None:
    if os.name == "nt":
        git = shutil.which("git")
        if git:
            candidate = Path(git).parent.parent / "bin/bash.exe"
            if candidate.is_file():
                return str(candidate)
        return None  # Do not accidentally invoke the WSL launcher.
    return shutil.which("bash")


def shell_path(path: Path) -> str:
    value = path.resolve().as_posix()
    if os.name == "nt":
        value = f"/{value[0].lower()}{value[2:]}"
    return value


def desktop_executable(value: str) -> str:
    # Undo Desktop Entry string escapes, followed by its quoted Exec escapes.
    value = re.sub(r"\\([sntr\\])", lambda m: {"s": " ", "n": "\n", "t": "\t", "r": "\r", "\\": "\\"}[m[1]], value)
    if not value.startswith('"'):
        return value.split(" ")[0]
    result: list[str] = []
    index = 1
    while index < len(value):
        char = value[index]
        if char == '"':
            break
        if char == "\\" and index + 1 < len(value) and value[index + 1] in '\\"$`':
            index += 1
            char = value[index]
        result.append(char)
        index += 1
    return "".join(result).replace("%%", "%")


class MetadataTests(unittest.TestCase):
    def test_license_collection_and_source_path_validation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="AnAnArchiver-licenses-") as temporary:
            base = Path(temporary)
            source = base / "source"
            dependency = source / "third_party/backend"
            dependency.mkdir(parents=True)
            expected_notice = b"Backend copyright and redistribution permission.\n"
            (dependency / "COPYING").write_bytes(expected_notice)
            qt = base / "Qt/LICENSES"
            qt.mkdir(parents=True)
            (qt / "LGPL-3.0-only.txt").write_text("Qt fixture license\n", encoding="utf-8")
            (qt.parent / "qtbase/LICENSES").mkdir(parents=True)
            (qt.parent / "qtbase/LICENSES/FTL.txt").write_text("Embedded FreeType permission\n", encoding="utf-8")
            (source / "frontend/qt/qml/icons").mkdir(parents=True)
            (source / "frontend/qt/qml/icons/LICENSE.txt").write_text("Original app icon permission\n", encoding="utf-8")
            manifest = base / "resolved-dependencies.json"
            value = {"schema_version": 1, "dependencies": [
                {"name": "backend", "path": "third_party/backend", "version": "1.2.3", "revision": "a" * 40,
                 "url": "https://example.test/backend.git"}]}
            manifest.write_text(json.dumps(value), encoding="utf-8")
            output = base / "package/licenses"
            records = collect(source, manifest, output, qt, base / "Qt", "6.12.0", verify_revision=False)
            self.assertEqual((output / "backend/COPYING").read_bytes(), expected_notice)
            self.assertEqual(records[0]["notices"][0]["sha256"], hashlib.sha256(expected_notice).hexdigest())
            self.assertEqual(records[0]["revision"], "a" * 40)
            self.assertEqual(json.loads((output / "third-party-notices.json").read_text(encoding="utf-8")), records)
            self.assertTrue((output / "qt-runtime/LICENSES/LGPL-3.0-only.txt").is_file())
            self.assertTrue((output / "qt-runtime/source/qtbase/LICENSES/FTL.txt").is_file())
            self.assertTrue((output / "icons/LICENSE.txt").is_file())
            value["dependencies"][0]["path"] = "third_party/../../outside"
            manifest.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unsafe path"):
                collect(source, manifest, base / "rejected", qt, base / "Qt", "6.12.0", verify_revision=False)
            self.assertFalse((base / "rejected").exists())

    def test_linux_archive_handler(self) -> None:
        entry = desktop(LINUX / f"{DESKTOP_ID}.desktop")
        self.assertEqual(entry["Desktop Entry"]["Exec"], "AnAnArchiver %f")
        self.assertEqual(entry["Desktop Entry"]["Icon"], DESKTOP_ID)
        self.assertEqual(entry["Desktop Entry"]["Terminal"], "false")
        types = entry["Desktop Entry"]["MimeType"].split(";")
        for required in ["application/zip", "application/x-7z-compressed", "application/vnd.rar", "application/x-tar", "application/gzip"]:
            self.assertIn(required, types)
        self.assertNotIn("application/octet-stream", types)
        # Launcher quicklists receive no selected archive. File operations must
        # not be exposed as desktop actions that would invoke missing arguments.
        self.assertNotIn("Actions", entry["Desktop Entry"])
        self.assertEqual(entry.sections(), ["Desktop Entry"])

    def test_macos_bundle_and_imported_archive_types(self) -> None:
        source = (ROOT / "packaging/macos/Info.plist.in").read_text(encoding="utf-8")
        substitutions = {
            "MACOSX_BUNDLE_EXECUTABLE_NAME": "AnAnArchiver",
            "MACOSX_BUNDLE_GUI_IDENTIFIER": "com.ananwanan.AnAnArchiver",
            "MACOSX_BUNDLE_ICON_FILE": "ananarchiver.icns",
            "MACOSX_BUNDLE_SHORT_VERSION_STRING": "1.0.0",
            "MACOSX_BUNDLE_BUNDLE_VERSION": "1.0.0",
        }
        for key, value in substitutions.items():
            source = source.replace("${" + key + "}", value)
        info = plistlib.loads(source.encode("utf-8"))
        self.assertEqual(info["CFBundleExecutable"], "AnAnArchiver")
        self.assertEqual(info["CFBundlePackageType"], "APPL")
        self.assertEqual(info["CFBundleIdentifier"], DESKTOP_ID)
        self.assertTrue(info["NSHighResolutionCapable"])
        types = info["CFBundleDocumentTypes"]
        self.assertEqual(types[0]["CFBundleTypeRole"], "Viewer")
        self.assertEqual(types[0]["LSHandlerRank"], "Alternate")
        self.assertIn("public.zip-archive", types[0]["LSItemContentTypes"])
        declarations = {item["UTTypeIdentifier"]: item for item in info["UTImportedTypeDeclarations"]}
        for identifier in types[0]["LSItemContentTypes"]:
            if identifier.startswith("com.ananwanan.") or identifier.startswith("org.7-zip.") or identifier.startswith("com.rarlab."):
                self.assertIn(identifier, declarations)
        self.assertNotIn("UTExportedTypeDeclarations", info)
        self.assertTrue((ROOT / "frontend/qt/resources/icons/ananarchiver.icns").is_file())

    @unittest.skipUnless(bash_executable(), "Bash is unavailable")
    def test_shell_syntax(self) -> None:
        scripts = list(LINUX.glob("*.sh")) + list((ROOT / "scripts").glob("Package-*.sh"))
        for script in scripts:
            with self.subTest(script=script.name):
                subprocess.run([bash_executable(), "-n", shell_path(script)], check=True, capture_output=True)

    @unittest.skipUnless(bash_executable(), "Bash is unavailable")
    def test_isolated_registration_upgrade_and_uninstall(self) -> None:
        with tempfile.TemporaryDirectory(prefix="AnAnArchiver-XDG-") as temporary:
            base = Path(temporary)
            data_home = base / "xdg-data"
            (data_home / "applications").mkdir(parents=True)
            defaults = data_home / "applications/mimeapps.list"
            original_defaults = "[Default Applications]\napplication/zip=existing-archive-manager.desktop;\n"
            defaults.write_text(original_defaults, encoding="utf-8")
            packages = [base / "便携 包 $`% (旧)", base / "便携 包 (新)"]
            if os.name != "nt":
                packages[0] = base / '便携 包 $`% "\\ (旧)'
            environment = os.environ.copy()
            environment["XDG_DATA_HOME"] = shell_path(data_home)
            environment["HOME"] = shell_path(base / "isolated-home")
            tools = base / "isolated-tools"
            tools.mkdir()
            validator = tools / "desktop-file-validate"
            validator.write_text("#!/usr/bin/env bash\nset -eu\n"
                                 "[[ ${1##*/} == com.ananwanan.AnAnArchiver.desktop ]] || exit 72\n",
                                 encoding="utf-8")
            validator.chmod(0o755)
            # Native Linux desktop-file-validate rejects a non-.desktop filename.
            # Keep that boundary covered when the host only offers Git Bash.
            def arguments(package: Path, name: str) -> list[str]:
                # Set the Unix PATH inside Bash; Git Bash converts the Windows
                # process environment before executing the first shell.
                return [bash_executable(), "-c", 'export PATH="$1:$PATH"; shift; exec bash "$@"',
                        "isolated-xdg", shell_path(tools), shell_path(package / name)]
            for package in packages:
                (package / "share/applications").mkdir(parents=True)
                (package / "share/pixmaps").mkdir(parents=True)
                for name in ["install-desktop.sh", "uninstall-desktop.sh", "AnAnArchiver.sh"]:
                    shutil.copy2(LINUX / name, package / name)
                    (package / name).chmod(0o755)
                shutil.copy2(LINUX / f"{DESKTOP_ID}.desktop", package / f"share/applications/{DESKTOP_ID}.desktop")
                (package / f"share/pixmaps/{DESKTOP_ID}.png").write_bytes(b"isolated icon fixture")

            def execute(package: Path, name: str) -> None:
                completed = subprocess.run(arguments(package, name), env=environment,
                                           capture_output=True, text=True, encoding="utf-8")
                self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

            registered = data_home / f"applications/{DESKTOP_ID}.desktop"
            icon = data_home / f"icons/{DESKTOP_ID}.png"
            execute(packages[0], "install-desktop.sh")
            entry = desktop(registered)
            expected = shell_path(packages[0]) + "/AnAnArchiver.sh"
            self.assertEqual(desktop_executable(entry["Desktop Entry"]["Exec"]), expected)
            self.assertTrue(icon.is_file())
            self.assertEqual(defaults.read_text(encoding="utf-8"), original_defaults)
            self.assertFalse(list((data_home / "applications").glob("ananarchiver-check-*")))
            self.assertFalse(list((data_home / "icons").glob(f".{DESKTOP_ID}.*")))
            failed_copy = tools / "cp"
            failed_copy.write_text("#!/usr/bin/env bash\nexit 73\n", encoding="utf-8")
            failed_copy.chmod(0o755)
            failing = subprocess.run(arguments(packages[1], "install-desktop.sh"),
                                     env=environment, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(failing.returncode, 73)
            self.assertFalse(list((data_home / "applications").glob("ananarchiver-check-*")))
            self.assertFalse(list((data_home / "icons").glob(f".{DESKTOP_ID}.*")))
            self.assertEqual(desktop_executable(desktop(registered)["Desktop Entry"]["Exec"]), expected)
            failed_copy.unlink()
            execute(packages[1], "install-desktop.sh")
            execute(packages[0], "uninstall-desktop.sh")
            self.assertTrue(registered.is_file())
            self.assertEqual(desktop_executable(desktop(registered)["Desktop Entry"]["Exec"]),
                             shell_path(packages[1]) + "/AnAnArchiver.sh")
            execute(packages[1], "uninstall-desktop.sh")
            self.assertFalse(registered.exists())
            self.assertFalse(icon.exists())
            self.assertEqual(defaults.read_text(encoding="utf-8"), original_defaults)
            execute(packages[1], "uninstall-desktop.sh")  # Repeated removal is harmless.


if __name__ == "__main__":
    unittest.main(verbosity=2)
