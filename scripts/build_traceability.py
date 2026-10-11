"""Bind a build to resolved sources and record the libraries actually selected.

This script never contacts the network or updates a dependency. Initialization
is the only place that selects versions; compilation verifies that snapshot.
"""
from __future__ import annotations

import argparse
import datetime
import json
import importlib.metadata
import hashlib
import os
from pathlib import Path
import subprocess
import sys


def write_json(path: Path, value: dict) -> None:
    text = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def source_dependencies(config: dict) -> tuple[dict | None, list[dict]]:
    if config["system_backend"] and not config["source_qt"]:
        return None, []
    path = Path(config["manifest"])
    if not path.is_file():
        raise ValueError(f"Missing dependency resolution: {path}. Run cmake -P scripts/init-dependencies.cmake first.")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("Unsupported dependency resolution schema")
    arguments = [sys.executable, str(Path(config["root"]) / "scripts/resolve_dependencies.py"),
                 "--root", config["root"], "--manifest", str(path), "--verify"]
    if config["source_qt"]:
        arguments.append("--with-qt")
    subprocess.run(arguments, check=True)
    selected = []
    for entry in manifest["dependencies"]:
        is_qt = entry["path"] == "third_party/qt" or entry["path"].startswith("third_party/qt/") or entry["path"] == "third_party/vulkan-headers"
        if (is_qt and config["source_qt"]) or (not is_qt and not config["system_backend"]):
            selected.append(entry)
    return manifest, selected


def identity(config: dict, dependencies: list[dict]) -> dict:
    # Resolution timestamps do not change compiled sources. Parent relationships
    # and source URLs do: preserve them along with exact commit identities.
    fields = ("name", "path", "source", "version", "tag", "revision", "parent")
    root = Path(config["root"])
    recipe_paths = [root / "cmake/Superbuild.cmake", root / "cmake/Dependencies.cmake"]
    recipe_paths.extend(sorted((root / "cmake/vendor").rglob("*")))
    recipe = {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in recipe_paths if path.is_file()}
    return {
        "dependencies": sorted(({key: entry.get(key) for key in fields} for entry in dependencies), key=lambda entry: entry["path"]),
        "toolchain": config["toolchain"],
        "build_recipe": recipe,
    }


def assert_same(previous: dict, current: dict, location: Path, *, subset: bool = False) -> None:
    if subset:
        expected = {item["path"]: item for item in previous["dependencies"]}
        matches = all(expected.get(item["path"]) == item for item in current["dependencies"])
        matches = matches and previous["toolchain"] == current["toolchain"]
        matches = matches and previous["build_recipe"] == current["build_recipe"]
    else:
        matches = previous == current
    if not matches:
        raise ValueError(f"Resolved sources or toolchain differ from {location}. Use a new build directory and dependency prefix; an old binary must not be labelled with new source revisions.")


def build_record(config: dict, manifest: dict | None, dependencies: list[dict]) -> dict:
    git = ["git", "-C", config["root"]]
    revision = subprocess.check_output(git + ["rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(git + ["status", "--porcelain", "--untracked-files=no"], text=True).strip())
    if "SOURCE_DATE_EPOCH" in os.environ:
        timestamp = datetime.datetime.fromtimestamp(int(os.environ["SOURCE_DATE_EPOCH"]), datetime.timezone.utc)
    else:
        timestamp = datetime.datetime.now(datetime.timezone.utc)
    external_libraries = config["external_libraries"]
    if config["superbuild"]:
        # This target waits for the complete application build. Its package
        # discovery is authoritative for explicitly selected developer libraries.
        application_record = Path(config["binary"]) / "application/build-info.json"
        if not application_record.is_file():
            raise ValueError("Application build provenance is missing; complete the application build first.")
        external_libraries = json.loads(application_record.read_text(encoding="utf-8"))["external_libraries"]
    record = {
        "schema_version": 1,
        "built_at": timestamp.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "application": {"version": config["application_version"], "revision": revision, "dirty": dirty},
        "toolchain": config["toolchain"],
        "cmake_version": config["cmake_version"],
        "generator": config["generator"],
        "resolution_time": manifest["resolved_at"] if manifest else None,
        "dependencies": dependencies,
        "external_libraries": external_libraries,
        "python_version": sys.version.split()[0],
        "source_generation_tools": {},
    }
    for package in ("jinja2", "jsonschema"):
        try:
            record["source_generation_tools"][package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            record["source_generation_tools"][package] = None
    return record


def run(config: dict, operation: str) -> None:
    manifest, dependencies = source_dependencies(config)
    current = identity(config, dependencies)
    state_path = Path(config["binary"]) / "dependency-build.json"
    prefix_path = Path(config["prefix"]) / "share/ananarchiver/dependency-build.json"
    if state_path.exists():
        assert_same(json.loads(state_path.read_text(encoding="utf-8")), current, state_path)
    elif operation != "prepare":
        raise ValueError(f"Missing configured build identity: {state_path}. Reconfigure first.")
    elif config["superbuild"] and (Path(config["binary"]) / "dependencies/build").exists():
        raise ValueError("An existing dependency build has no source identity. Configure a new build directory to establish provenance.")
    elif config["superbuild"] and dependencies and not prefix_path.exists():
        prefix = Path(config["prefix"])
        if prefix.exists() and any(prefix.iterdir()):
            raise ValueError("A nonempty dependency prefix has no source identity. Select a new empty dependency prefix.")
    qt_state_path = None
    if config["superbuild"] and config["source_qt"]:
        qt_binary = Path(config["qt_build_binary"])
        qt_state_path = qt_binary / "dependency-source.json"
        qt_identity = {**current, "dependencies": [entry for entry in current["dependencies"]
                       if entry["path"] == "third_party/qt" or entry["path"].startswith("third_party/qt/")
                       or entry["path"] == "third_party/vulkan-headers"]}
        if qt_state_path.exists():
            assert_same(json.loads(qt_state_path.read_text(encoding="utf-8")), qt_identity, qt_state_path, subset=True)
        elif qt_binary.exists() and any(qt_binary.iterdir()):
            raise ValueError("An existing Qt build has no source identity. Select a new Qt build directory.")
    if dependencies and not config["superbuild"]:
        if not prefix_path.is_file():
            raise ValueError(f"Missing compiled dependency identity: {prefix_path}. Build dependencies through the superbuild first.")
        assert_same(json.loads(prefix_path.read_text(encoding="utf-8")), current, prefix_path, subset=True)
    if operation == "prepare":
        if config["superbuild"] and prefix_path.exists():
            assert_same(json.loads(prefix_path.read_text(encoding="utf-8")), current, prefix_path)
        write_json(state_path, current)
        if qt_state_path:
            write_json(qt_state_path, qt_identity)
    elif operation == "record-prefix":
        # Called only after all ExternalProject dependency builds/installations
        # succeed. A failed build never grants provenance to an old prefix.
        write_json(prefix_path, current)
    elif operation == "record":
        write_json(Path(config["binary"]) / "build-info.json", build_record(config, manifest, dependencies))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--operation", choices=("prepare", "verify", "record-prefix", "record"), required=True)
    arguments = parser.parse_args()
    try:
        run(json.loads(arguments.config.read_text(encoding="utf-8")), arguments.operation)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"Build traceability error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
