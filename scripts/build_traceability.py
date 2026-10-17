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
import re
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile


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
    if not isinstance(manifest, dict):
        raise ValueError("Dependency resolution manifest must be a JSON object")
    if manifest.get("schema_version") != 1:
        raise ValueError("Unsupported dependency resolution schema")
    arguments = [sys.executable, str(Path(config["root"]) / "scripts/resolve-dependencies.py"),
                 "--root", config["root"], "--replay-manifest", str(path), "--verify"]
    if config["source_qt"]:
        arguments.append("--qt")
    else:
        arguments.append("--core-only")
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
    fields = ("name", "path", "url", "version", "ref", "revision", "coupled_to")
    root = Path(config["root"])
    recipe_paths = [root / "cmake/Superbuild.cmake", root / "cmake/Dependencies.cmake"]
    if not config["system_backend"]:
        patch_path = root / "cmake/patches/libarchive-compatibility.patch"
        patch_hash = hashlib.sha256(patch_path.read_bytes()).hexdigest()
        if config.get("libarchive_patch_sha256") != patch_hash:
            raise ValueError("Missing or mismatched libarchive compatibility patch hash; reconfigure and rebuild the dependency prefix.")
        recipe_paths.extend([root / "cmake/PrepareLibarchive.cmake", patch_path])
    recipe_paths.extend(sorted((root / "cmake/vendor").rglob("*")))
    recipe = {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in recipe_paths if path.is_file()}
    return {
        "dependencies": sorted(({key: entry.get(key) for key in fields} for entry in dependencies), key=lambda entry: entry["path"]),
        "toolchain": config["toolchain"],
        "build_recipe": recipe,
        "build_options": {"libarchive_cng": None if config["system_backend"] else config.get("libarchive_cng", True),
                          "libarchive_patch_sha256": None if config["system_backend"] else config["libarchive_patch_sha256"]},
    }


def assert_same(previous: dict, current: dict, location: Path, *, subset: bool = False) -> None:
    if subset:
        expected = {item["path"]: item for item in previous["dependencies"]}
        matches = all(expected.get(item["path"]) == item for item in current["dependencies"])
        matches = matches and previous["toolchain"] == current["toolchain"]
        matches = matches and previous["build_recipe"] == current["build_recipe"]
        matches = matches and previous.get("build_options") == current.get("build_options")
    else:
        matches = previous == current
    if not matches:
        raise ValueError(f"Resolved sources or toolchain differ from {location}. Use a new build directory and dependency prefix; an old binary must not be labelled with new source revisions.")


def qt_build_identity(config: dict, dependencies: list[dict]) -> dict:
    # Qt never links the archive backend. Its reusable source cache is bound to
    # Qt's source commits and actual compilation options, independently of a
    # libarchive update or a descriptive manifest schema change.
    entries = [entry for entry in dependencies if entry["path"] == "third_party/qt"
               or entry["path"].startswith("third_party/qt/") or entry["path"] == "third_party/vulkan-headers"]
    return {"dependencies": sorted(({"path": entry["path"], "url": entry.get("url", entry.get("source")),
                                     "revision": entry["revision"]} for entry in entries), key=lambda row: row["path"]),
            "toolchain": config["toolchain"], "options": config.get("qt_options", [])}


def validate_qt_cache(config: dict, previous: dict, current: dict, cache_directory: Path) -> None:
    old_sources = qt_build_identity({"toolchain": previous["toolchain"]}, previous["dependencies"])
    if old_sources["dependencies"] != current["dependencies"] or previous["toolchain"] != current["toolchain"]:
        raise ValueError("The shared Qt build belongs to different sources or compiler options. Select a new Qt build directory.")
    if "options" in previous:
        if previous["options"] != current["options"]:
            raise ValueError("The shared Qt build uses different Qt options. Select a new Qt build directory.")
        return
    # Upgrade our earlier source-cache record only when the completed CMake
    # configuration proves every requested Qt option. Never infer compilation
    # options from installed headers or an unrecorded directory.
    cache = {}
    path = cache_directory / "CMakeCache.txt"
    if not path.is_file():
        raise ValueError("Qt cache migration requires its recorded CMake configuration")
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"([^#/:][^:]*):[^=]*=(.*)", line)
        if match:
            cache[match[1]] = match[2]
    for option in current["options"]:
        key, value = option[2:].split("=", 1)
        key = key.split(":", 1)[0]
        actual = cache.get(key)
        if actual is None and key.startswith("FEATURE_"):
            actual = cache.get("QT_" + key)
        if actual != value.replace("|", ";"):
            raise ValueError(f"Qt cache migration cannot prove option {key}; select a new Qt build directory.")


def generated_source_files(root: Path) -> dict:
    """Hash every input without following links or trusting preparation metadata."""
    def is_reparse(path: Path) -> bool:
        return bool(getattr(path.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))

    if not root.is_dir() or root.is_symlink() or is_reparse(root):
        raise ValueError(f"Generated libarchive source is missing or is a link: {root}. Reconfigure first.")
    files = {}

    def scan(directory: Path) -> None:
        for path in sorted(directory.iterdir()):
            relative = path.relative_to(root).as_posix()
            if directory == root and path.name in {".git", ".prepared", ".patched-files"}:
                continue
            if path.is_symlink():
                files[relative] = {"kind": "symlink", "target": os.readlink(path)}
            elif is_reparse(path):
                raise ValueError(f"Unexpected junction in generated libarchive source: {relative}. Reconfigure first.")
            elif path.is_dir():
                files[relative] = {"kind": "directory"}
                scan(path)
            elif path.is_file():
                with path.open("rb") as stream:
                    digest = hashlib.sha256()
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(chunk)
                    files[relative] = {"kind": "file", "sha256": digest.hexdigest()}
            else:
                raise ValueError(f"Unexpected entry in generated libarchive source: {relative}. Reconfigure first.")

    scan(root)
    return files


def validate_generated_libarchive(config: dict, dependencies: list[dict], operation: str) -> None:
    if not config["superbuild"] or config["system_backend"]:
        return
    backends = [entry for entry in dependencies if entry["name"] == "libarchive"]
    if len(backends) != 1 or backends[0]["path"] != "third_party/libarchive":
        raise ValueError("Generated libarchive source requires one resolved backend. Reconfigure first.")
    revision, patch_hash = backends[0]["revision"], config["libarchive_patch_sha256"]
    source_id = hashlib.sha256((revision + "\n" + patch_hash).encode("utf-8")).hexdigest()[:16]
    source = Path(config["binary"]) / "dependencies/sources" / ("libarchive-" + source_id)
    record_path = Path(config["binary"]) / "libarchive-generated-source.json"
    identity = {"schema_version": 1, "upstream_revision": revision, "patch_sha256": patch_hash}
    if operation == "prepare":
        # A cached preparation stamp cannot attest files that are actually built.
        # Regenerate the complete expected tree from the exact official commit on
        # every configure; never bless an edited generated source as a baseline.
        with tempfile.TemporaryDirectory(prefix="ananarchiver-libarchive-verify-") as temporary:
            temporary = Path(temporary).resolve()
            archive = temporary / "upstream.tar"
            expected = temporary / "source"
            upstream = Path(config["root"]) / backends[0]["path"]
            subprocess.run(["git", "-C", str(upstream), "archive", "--format=tar",
                            "--output=" + str(archive), revision], check=True, capture_output=True)
            expected.mkdir()
            with tarfile.open(archive) as exported:
                # Git exports only tracked directories, files and symlinks. Use
                # a narrow extraction path compatible with Python 3.10, without
                # following a link while creating another member.
                for member in exported:
                    relative = PurePosixPath(member.name)
                    target = expected.joinpath(*relative.parts)
                    if relative.is_absolute() or ".." in relative.parts or target.is_symlink() \
                            or not target.parent.resolve().is_relative_to(expected):
                        raise ValueError("Unsafe path in exported libarchive source")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if member.isdir():
                        target.mkdir(exist_ok=True)
                    elif member.issym():
                        target.symlink_to(member.linkname)
                    elif member.isfile():
                        with exported.extractfile(member) as source_stream, target.open("wb") as output_stream:
                            shutil.copyfileobj(source_stream, output_stream)
                    else:
                        raise ValueError("Unexpected entry in exported libarchive source")
            subprocess.run(["git", "init", "--quiet", str(expected)], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(expected), "apply",
                            str(Path(config["root"]) / "cmake/patches/libarchive-compatibility.patch")],
                           check=True, capture_output=True)
            record = {**identity, "files": generated_source_files(expected)}
    else:
        if not record_path.is_file():
            raise ValueError(f"Missing generated libarchive source identity: {record_path}. Reconfigure first.")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        if not isinstance(record, dict) or any(record.get(key) != value for key, value in identity.items()) \
                or not isinstance(record.get("files"), dict):
            raise ValueError("Generated libarchive source identity differs from the configured backend. Reconfigure first.")
    actual = generated_source_files(source)
    if record["files"] != actual:
        changed = next(path for path in sorted(record["files"].keys() | actual.keys())
                       if record["files"].get(path) != actual.get(path))
        raise ValueError(f"Generated libarchive source differs from its official revision and patch: {changed}. Reconfigure first; restore the generated sources or use a new build directory.")
    if operation == "prepare":
        write_json(record_path, record)


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
        application_record = Path(config["binary"]) / "application/dependency-provenance.json"
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
        "dependency_options": {"libarchive_cng": None if config["system_backend"] else config.get("libarchive_cng", True)},
    }
    backend = next((entry for entry in dependencies if entry["name"] == "libarchive"), None)
    qt = next((entry for entry in dependencies if entry["name"] == "qt"), None)
    external_backend = next((entry for entry in external_libraries if entry["name"] == "libarchive"), None)
    external_qt = next((entry for entry in external_libraries if entry["name"] == "Qt"), None)
    record.update({"project_version": config["application_version"],
                   "compiler_id": config["toolchain"]["cxx_compiler"]["id"],
                   "compiler_version": config["toolchain"]["cxx_compiler"]["version"],
                   "libarchive_version": (backend or external_backend or {}).get("version"),
                   "libarchive_revision": (backend or {}).get("revision"),
                   "qt_version": (qt or external_qt or {}).get("version"),
                   "qt_source": "bundled-source" if qt else "explicit-developer-kit" if external_qt else "disabled"})
    if config.get("qt_windows_patch_sha256"):
        record["qt_windows_longpaths_sha256"] = config["qt_windows_patch_sha256"]
        record["qtbase_revision"] = config["qtbase_revision"]
    if not config["system_backend"]:
        record["libarchive_compatibility_patch"] = {
            "path": "cmake/patches/libarchive-compatibility.patch",
            "sha256": config["libarchive_patch_sha256"],
            "upstream_revision": (backend or {}).get("revision"),
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
    prefix_path = Path(config["prefix"]) / "share/AnAnArchiver/dependency-build.json"
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
        qt_identity = qt_build_identity(config, dependencies)
        if qt_state_path.exists():
            validate_qt_cache(config, json.loads(qt_state_path.read_text(encoding="utf-8")), qt_identity, qt_binary)
        elif qt_binary.exists() and any(qt_binary.iterdir()):
            raise ValueError("An existing Qt build has no source identity. Select a new Qt build directory.")
    if dependencies and not config["superbuild"]:
        if not prefix_path.is_file():
            raise ValueError(f"Missing compiled dependency identity: {prefix_path}. Build dependencies through the superbuild first.")
        assert_same(json.loads(prefix_path.read_text(encoding="utf-8")), current, prefix_path, subset=True)
    validate_generated_libarchive(config, dependencies, operation)
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
        if manifest:
            write_json(prefix_path.parent / "resolved-dependencies.json",
                       {**manifest, "includes_qt": config["source_qt"], "dependencies": dependencies})
    elif operation == "record":
        write_json(Path(config["binary"]) / "dependency-provenance.json", build_record(config, manifest, dependencies))
        if manifest:
            write_json(Path(config["binary"]) / "resolved-dependencies.json",
                       {**manifest, "includes_qt": config["source_qt"], "dependencies": dependencies})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--operation", choices=("prepare", "verify", "record-prefix", "record"), required=True)
    arguments = parser.parse_args()
    try:
        run(json.loads(arguments.config.read_text(encoding="utf-8")), arguments.operation)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"Build traceability error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
