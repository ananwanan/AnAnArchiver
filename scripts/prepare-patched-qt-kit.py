#!/usr/bin/env python3
"""Rebuild Qt's Windows loader and raster-window GPU guard from a matching cache.

The stock SDK, original source files and original Ninja graph remain unchanged.
Generated cached Core/QPA objects/libraries are relinked into a shadow SDK.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import time


def run(arguments: list[str], cwd: Path | None = None) -> str:
    completed = subprocess.run(arguments, cwd=cwd, capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
    if completed.returncode:
        raise RuntimeError(f"Command failed ({completed.returncode}): {arguments[0]}\n{completed.stdout[-6000:]}\n{completed.stderr[-3000:]}")
    return completed.stdout


def cache_values(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        matched = re.match(r"^([^/#][^:]*):[^=]+=(.*)$", line)
        if matched:
            values[matched[1]] = matched[2]
    return values


def features(path: Path) -> dict[str, int]:
    return {key: int(value) for key, value in re.findall(r"^#define (QT_FEATURE_\w+) (-?\d+)",
                                                        path.read_text(encoding="utf-8"), re.MULTILINE)}


def exports(dumpbin: Path, dll: Path, minimum: int = 1000) -> dict[str, int]:
    output = run([str(dumpbin), "/nologo", "/exports", str(dll)])
    result = {name: int(ordinal) for ordinal, name in re.findall(
        r"^\s+(\d+)\s+[A-Fa-f0-9]+\s+[A-Fa-f0-9]+\s+(\S+)", output, re.MULTILINE)}
    if len(result) < minimum:
        raise RuntimeError(f"Exports could not be read reliably: {dll.name}")
    return result


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ninja_path(path: Path) -> str:
    return str(path).replace("$", "$$").replace(":", "$:").replace(" ", "$ ")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    kit, build, repository = (path.resolve(strict=True) for path in (args.kit, args.cache, args.repository))
    config = cache_values(build / "CMakeCache.txt")
    if config.get("CMAKE_BUILD_TYPE") != "Release":
        raise RuntimeError("The cached Core must be a Release build")
    source = Path(config["CMAKE_HOME_DIRECTORY"]).resolve(strict=True) / "qtbase"
    revision = run(["git", "-C", str(source), "rev-parse", "HEAD"]).strip()
    if run(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=all"]).strip():
        raise RuntimeError("The original QtBase source tree must be clean")
    version = re.search(r"^QT_VERSION\s*=\s*(\S+)", (kit / "mkspecs/qconfig.pri").read_text(), re.MULTILINE)[1]
    stock_source_revisions = set()
    for sbom in (kit / "sbom").glob("qtbase-*.spdx.json"):
        document = json.loads(sbom.read_text(encoding="utf-8"))
        for package in document.get("packages", []):
            matched = re.fullmatch(r"git://code\.qt\.io/qt/qtbase\.git@([0-9a-f]{40})",
                                   package.get("downloadLocation", ""))
            if matched:
                stock_source_revisions.add(matched[1])
    if stock_source_revisions != {revision}:
        raise RuntimeError("The stock kit's QtBase SBOM revision differs from the cached source")
    if f"QT_VERSION_STR \"{version}\"" not in (build / "qtbase/src/corelib/global/qconfig.h").read_text():
        raise RuntimeError("Cached QtCore version differs from the stock kit")
    patch = repository / "cmake/patches/qt-windows-longpaths.patch"
    patch_hash = sha256(patch)
    work = (args.work or repository / "out/qt-patch-work" / patch_hash[:12]).resolve()
    output = (args.output or repository / "out/qt-sdk" / f"patched-{version}-m5").resolve()
    if output == kit or output.is_relative_to(kit) or kit.is_relative_to(output):
        raise RuntimeError("The shadow SDK must be separate from the original SDK")
    if output.exists():
        raise RuntimeError("Use a new shadow SDK directory; existing kits are never deleted")
    work.mkdir(parents=True, exist_ok=True)
    dumpbin = Path(config["CMAKE_CXX_COMPILER"]).parent / "dumpbin.exe"
    stock_dll = kit / "bin/Qt6Core.dll"
    cached_dll = build / "qtbase/bin/Qt6Core.dll"
    stock_platform = kit / "plugins/platforms/qwindows.dll"
    cached_platform = build / "qtbase/plugins/platforms/qwindows.dll"
    if not cached_dll.is_file():
        raise RuntimeError("This recipe requires a completed Core build cache")
    headers = kit / "include/QtCore"
    core_features = features(build / "qtbase/src/corelib/qtcore-config.h")
    if core_features != features(headers / "qtcore-config.h"):
        raise RuntimeError("QtCore's feature configuration differs from the official kit")
    if features(build / "qtbase/src/gui/qtgui-config.h") != features(kit / "include/QtGui/qtgui-config.h"):
        raise RuntimeError("QtGui's feature configuration differs from the official kit")
    if not cached_platform.is_file():
        raise RuntimeError("This recipe requires a completed Windows QPA plugin cache")
    stock_platform_exports = exports(dumpbin, stock_platform, minimum=2)
    if stock_platform_exports != exports(dumpbin, cached_platform, minimum=2):
        raise RuntimeError("The cached qwindows plugin exports differ from the stock plugin")
    stock_exports = exports(dumpbin, stock_dll)
    if stock_exports != exports(dumpbin, cached_dll):
        raise RuntimeError("The cached QtCore's exported names/ordinals differ from the stock kit")
    local_global = features(build / "qtbase/src/corelib/global/qconfig.h")
    stock_global = features(headers / "qconfig.h")
    differences = {key: {"stock": stock_global.get(key), "cache": local_global.get(key)}
                   for key in sorted(set(local_global) | set(stock_global))
                   if stock_global.get(key) != local_global.get(key)}

    # Extract only the plugin implementation/header directory from the exact
    # source commit. No original checkout needs a temporary modification.
    source_export = work / "qtbase-source"
    if source_export.exists():
        raise RuntimeError("Use a fresh work directory for this patch build")
    source_export.mkdir()
    archive = subprocess.run(["git", "-C", str(source), "archive", revision,
                              "src/corelib/plugin", "src/plugins/platforms/windows"],
                             capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as package:
        package.extractall(source_export, filter="data")
    run(["git", "-C", str(source_export), "apply", "--check", str(patch)])
    run(["git", "-C", str(source_export), "apply", str(patch)])
    input_paths = ["src/corelib/plugin/qlibrary_win.cpp", "src/plugins/platforms/windows/qwindowswindow.cpp"]
    original_graph = build / "build.ninja"
    graph = original_graph.read_text(encoding="utf-8")
    original_inputs = {path: sha256(source / path) for path in input_paths}
    for path, target_directory in zip(input_paths, ["CMakeFiles\\Core.dir", "CMakeFiles\\QWindowsIntegrationPlugin.dir"]):
        original_input, patched_input = source / path, source_export / path
        lines = graph.splitlines(keepends=True)
        matching = [index for index, line in enumerate(lines) if line.startswith("build ")
                    and target_directory in line and ninja_path(original_input) in line]
        if len(matching) != 1:
            raise RuntimeError(f"The cached Ninja graph does not identify exactly one input: {path}")
        index = matching[0]
        lines[index] = lines[index].replace(ninja_path(original_input), ninja_path(patched_input))
        graph = "".join(lines)
    override_graph = work / "qtcore-longpaths.ninja"
    override_graph.write_text(graph, encoding="utf-8")
    targets = ["Core", "QWindowsIntegrationPlugin"]
    dry_run = run(["ninja", "-C", str(build), "-f", str(override_graph), "-n", *targets])
    (work / "build-dry-run.txt").write_text(dry_run, encoding="utf-8")
    compilations = [line for line in dry_run.splitlines() if "Building CXX object" in line or "Building C object" in line]
    if len(compilations) != 2 or not all(any(name in line for line in compilations)
                                       for name in ["qlibrary_win.cpp.obj", "qwindowswindow.cpp.obj"]):
        raise RuntimeError("The patch build would rebuild more than two modified sources; inspect build-dry-run.txt")
    started = time.monotonic()
    print("Recompiling qlibrary_win.cpp/qwindowswindow.cpp and linking QtCore/qwindows", flush=True)
    build_log = run(["ninja", "-C", str(build), "-f", str(override_graph), "-j", str(args.jobs), *targets])
    (work / "build-log.txt").write_text(build_log, encoding="utf-8")
    elapsed = time.monotonic() - started
    patched_exports = exports(dumpbin, cached_dll)
    if patched_exports != stock_exports:
        raise RuntimeError("The patched QtCore changed exported names/ordinals")
    if exports(dumpbin, cached_platform, minimum=2) != stock_platform_exports:
        raise RuntimeError("The patched qwindows plugin changed exported names/ordinals")
    platform_imports = {}
    for module, provider in [("Qt6Core.dll", cached_dll), ("Qt6Gui.dll", kit / "bin/Qt6Gui.dll")]:
        text = run([str(dumpbin), "/nologo", "/imports:" + module, str(cached_platform)])
        text = re.split(r"^\s+Summary\s*$", text, maxsplit=1, flags=re.MULTILINE)[0]
        symbols = set(re.findall(r"^\s+[0-9A-Fa-f]+\s+(\S+)\s*$", text, re.MULTILINE))
        if not symbols or symbols - set(exports(dumpbin, provider)):
            raise RuntimeError(f"The patched qwindows imports unavailable {module} symbols")
        platform_imports[module] = len(symbols)
    imported_symbols: set[str] = set()
    imported_modules = 0
    for dll in kit.rglob("*.dll"):
        text = run([str(dumpbin), "/nologo", "/imports:Qt6Core.dll", str(dll)])
        text = re.split(r"^\s+Summary\s*$", text, maxsplit=1, flags=re.MULTILINE)[0]
        symbols = set(re.findall(r"^\s+[0-9A-Fa-f]+\s+(\S+)\s*$", text, re.MULTILINE))
        if symbols:
            imported_modules += 1
            imported_symbols.update(symbols)
    missing = imported_symbols - set(patched_exports)
    if missing:
        raise RuntimeError(f"The stock kit imports missing QtCore symbols: {sorted(missing)[:10]}")
    original_graph_hash = sha256(original_graph)
    print(f"ABI verified: {len(patched_exports)} exports, {len(imported_symbols)} imports from {imported_modules} DLLs", flush=True)
    copied = subprocess.run(["robocopy", str(kit), str(output), "/E", "/COPY:DAT", "/R:0", "/W:0",
                             "/NFL", "/NDL", "/NJH", "/NJS", "/NP"], capture_output=True, text=True)
    if copied.returncode >= 8:
        raise RuntimeError(f"Copying the stock kit failed: {copied.stdout[-3000:]}")
    shutil.copyfile(cached_dll, output / "bin/Qt6Core.dll")
    shutil.copyfile(build / "qtbase/lib/Qt6Core.lib", output / "lib/Qt6Core.lib")
    shutil.copyfile(cached_platform, output / "plugins/platforms/qwindows.dll")
    if sha256(output / "bin/Qt6Gui.dll") != sha256(kit / "bin/Qt6Gui.dll"):
        raise RuntimeError("The shadow QtGui provider differs from the verified stock provider")
    record = {"schema_version": 1, "qt_version": version, "qtbase_revision": revision,
              "patch_sha256": patch_hash, "patch": "cmake/patches/qt-windows-longpaths.patch",
              "stock_kit": str(kit), "cached_build": str(build), "source_export": str(source_export),
              "stock_qtbase_source_revisions": sorted(stock_source_revisions),
              "private_ninja_graph": str(override_graph), "original_ninja_sha256": original_graph_hash,
              "original_source_sha256": original_inputs,
              "patched_source_sha256": {path: sha256(source_export / path) for path in input_paths},
              "original_loader_sha256": original_inputs[input_paths[0]],
              "patched_loader_sha256": sha256(source_export / input_paths[0]),
              "compiler": config["CMAKE_CXX_COMPILER"], "core_features_equal": True,
              "global_feature_differences": differences, "exports_names_and_ordinals_equal": True,
              "export_count": len(patched_exports), "required_import_count": len(imported_symbols),
              "importing_dll_count": imported_modules, "core_build_seconds": round(elapsed, 3),
              "gui_features_equal": True, "qwindows_exports_names_and_ordinals_equal": True,
              "qwindows_required_imports_verified": True, "qwindows_required_import_counts": platform_imports,
              "qwindows_import_provider_sha256": {"Qt6Core.dll": sha256(output / "bin/Qt6Core.dll"),
                                                  "Qt6Gui.dll": sha256(output / "bin/Qt6Gui.dll")},
              "stock_qwindows_sha256": sha256(stock_platform),
              "patched_qwindows_sha256": sha256(output / "plugins/platforms/qwindows.dll"),
              "modified_binaries": [
                  {"relative_path": "bin/Qt6Core.dll", "sha256": sha256(output / "bin/Qt6Core.dll"), "stock_sha256": sha256(stock_dll)},
                  {"relative_path": "plugins/platforms/qwindows.dll", "sha256": sha256(output / "plugins/platforms/qwindows.dll"), "stock_sha256": sha256(stock_platform)}],
              "stock_qtcore_sha256": sha256(stock_dll), "patched_qtcore_sha256": sha256(output / "bin/Qt6Core.dll"),
              "patched_import_library_sha256": sha256(output / "lib/Qt6Core.lib"),
              "created_utc": datetime.now(timezone.utc).isoformat()}
    destination = output / "share/AnAnArchiver/qt-windows-longpaths.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(patch, destination.parent / "qt-windows-longpaths.patch")
    if sha256(original_graph) != original_graph_hash or any(sha256(source / path) != original_inputs[path] for path in input_paths):
        raise RuntimeError("The original source or Ninja graph unexpectedly changed")
    print(f"Shadow Qt kit: {output}", flush=True)
    print(f"Patch provenance: {destination}", flush=True)


if __name__ == "__main__":
    main()
