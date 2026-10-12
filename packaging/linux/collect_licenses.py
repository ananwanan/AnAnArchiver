#!/usr/bin/env python3
"""Collect notices from the exact dependency source trees used by a package."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess

NOTICE_NAME = re.compile(r"^(LICENSE|LICENCE|COPYING|COPYRIGHT)([._-].*)?$|licen[cs]e.*\.txt$", re.IGNORECASE)


def relative_path(value: str) -> PurePosixPath:
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_.\-/]+", value):
        raise ValueError("Unsafe path in dependency provenance")
    result = PurePosixPath(value)
    if result.is_absolute() or any(part in {"", ".", ".."} for part in value.split("/")):
        raise ValueError("Unsafe path in dependency provenance")
    return result


def notices(source: Path, all_files: bool = False) -> list[Path]:
    files: list[Path] = []
    source = source.resolve(strict=True)
    for directory, directories, filenames in os.walk(source, followlinks=False):
        directories[:] = sorted(name for name in directories
                                if name not in {".git", "__pycache__"}
                                and not (Path(directory) / name).is_symlink())
        for filename in sorted(filenames):
            path = Path(directory) / filename
            inside_licenses = any(part.upper() == "LICENSES" for part in path.relative_to(source).parts[:-1])
            if not (all_files or inside_licenses or NOTICE_NAME.search(filename)):
                continue
            if path.is_symlink() or not path.resolve().is_relative_to(source):
                raise ValueError(f"License source escapes its dependency directory: {path}")
            files.append(path)
    if not files:
        raise ValueError(f"No license notices found in {source}")
    return files


def collect(source_root: Path, manifest_file: Path, output: Path,
            qt_license_directory: Path, qt_prefix: Path, qt_version: str,
            project_root: Path | None = None, verify_revision: bool = True) -> list[dict]:
    source_root = source_root.resolve(strict=True)
    project_root = (project_root or source_root).resolve(strict=True)
    output = output.resolve()
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or not isinstance(manifest.get("dependencies"), list):
        raise ValueError("Unsupported dependency provenance")
    records: list[dict] = []

    def copy_files(source: Path, target: Path, files: list[Path]) -> list[dict]:
        copied = []
        source = source.resolve(strict=True)
        for path in files:
            relative = path.relative_to(source)
            destination = target / relative
            if not destination.resolve().is_relative_to(output):
                raise ValueError("License destination escapes package directory")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
            destination.chmod(0o644)
            copied.append({"path": destination.relative_to(output).as_posix(),
                           "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()})
        return copied

    for dependency in manifest["dependencies"]:
        name = relative_path(dependency["name"])
        path = relative_path(dependency["path"])
        if not path.parts or path.parts[0] != "third_party":
            raise ValueError("Dependency path is outside third_party")
        source = (source_root / path).resolve(strict=True)
        if not source.is_relative_to(source_root):
            raise ValueError("Dependency source escapes the selected source root")
        if verify_revision:
            revision = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"], check=True,
                                      capture_output=True, text=True).stdout.strip()
            if revision != dependency["revision"]:
                raise ValueError(f"License source revision differs from built dependency: {name}")
        copied = copy_files(source, output / name, notices(source))
        records.append({"name": str(name), "version": dependency["version"],
                        "revision": dependency["revision"], "source": dependency["url"], "notices": copied})

    qt_license_directory = qt_license_directory.resolve(strict=True)
    qt_notices = copy_files(qt_license_directory, output / "qt-runtime/LICENSES",
                           notices(qt_license_directory, all_files=True))
    # A supermodule LICENSES directory contains the Qt framework licenses, while
    # runtime modules also carry notices for embedded fonts/codecs and libraries.
    # Include those module texts alongside the SBOM instead of losing them when
    # packaging an explicitly selected developer kit with a Core-only manifest.
    for module in ["qtbase", "qtdeclarative", "qtshadertools", "qtsvg"]:
        module_source = qt_license_directory.parent / module
        if module_source.is_dir():
            qt_notices.extend(copy_files(module_source, output / "qt-runtime/source" / module,
                                         notices(module_source)))
    for relative in ["sbom", "share/qt6/sbom", "share/Qt6/sbom"]:
        directory = qt_prefix / relative
        if directory.is_dir():
            qt_notices.extend(copy_files(directory, output / "qt-runtime/sbom", notices(directory, all_files=True)))
    records.append({"name": "qt-runtime", "version": qt_version,
                    "source": "https://download.qt.io/official_releases/qt/", "notices": qt_notices})
    icon_source = project_root / "frontend/qt/qml/icons"
    icon_notices = copy_files(icon_source, output / "icons", [icon_source / "LICENSE.txt"])
    records.append({"name": "application-icons", "source": "https://github.com/ananwanan/AnAnArchiver", "notices": icon_notices})
    output.mkdir(parents=True, exist_ok=True)
    (output / "third-party-notices.json").write_text(json.dumps(records, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (output / "THIRD-PARTY-NOTICES.txt").write_text(
        "AnAnArchiver contains open source archive backends/codecs and dynamically linked Qt.\n"
        "Exact dependency revisions, source URLs and license text hashes are in third-party-notices.json.\n"
        "The package permits replacement of Qt with compatible modified builds and debugging those modifications.\n"
        "Application source and build instructions: https://github.com/ananwanan/AnAnArchiver\n"
        "Backend source: https://github.com/ananwanan/libarchive\n"
        "Replay the included resolved-dependencies.json with scripts/resolve-dependencies.py --replay-manifest\n"
        "to obtain the exact selected sources. Qt's matching notices and available runtime SBOM are in qt-runtime.\n",
        encoding="utf-8")
    return records


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--project-root", type=Path)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qt-license-dir", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--qt-version", required=True)
    args = parser.parse_args()
    records = collect(args.source_root, args.manifest, args.output, args.qt_license_dir,
                      args.qt_prefix, args.qt_version, args.project_root)
    print(f"Collected notices for {len(records)} dependency source records")


if __name__ == "__main__":
    main()
