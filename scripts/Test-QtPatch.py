#!/usr/bin/env python3
"""Check private Qt source preparation against an existing resolved QtBase tree."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def execute(args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qtbase", type=Path, required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="AnAnArchiver-QtPrepare-") as temporary:
        work = Path(temporary)
        project = work / "project"
        base = project / "third_party/qt/qtbase"
        loader = base / "src/corelib/plugin/qlibrary_win.cpp"
        loader.parent.mkdir(parents=True)
        shutil.copyfile(args.qtbase / "src/corelib/plugin/qlibrary_win.cpp", loader)
        platform = base / "src/plugins/platforms/windows/qwindowswindow.cpp"
        platform.parent.mkdir(parents=True)
        shutil.copyfile(args.qtbase / "src/plugins/platforms/windows/qwindowswindow.cpp", platform)
        execute(["git", "init", "--quiet", str(base)])
        execute(["git", "-C", str(base), "add", "."])
        execute(["git", "-C", str(base), "-c", "user.name=Qt patch regression",
                 "-c", "user.email=qt-patch@example.invalid", "commit", "--quiet", "-m", "Pristine loader fixture"])
        revision = execute(["git", "-C", str(base), "rev-parse", "HEAD"])
        original_hash = hashlib.sha256(loader.read_bytes()).hexdigest()
        original_platform_hash = hashlib.sha256(platform.read_bytes()).hexdigest()
        patch_dir = project / "cmake/patches"
        patch_dir.mkdir(parents=True)
        shutil.copyfile(repository / "cmake/patches/qt-windows-longpaths.patch", patch_dir / "qt-windows-longpaths.patch")
        manifest = work / "manifest.json"
        manifest.write_text(json.dumps({"dependencies": [{"path": "third_party/qt/qtbase", "revision": revision,
                                                          "url": "https://code.qt.io/qt/qtbase.git"}]}), encoding="utf-8")
        script = work / "test.cmake"
        script.write_text(f'''cmake_minimum_required(VERSION 3.25)
set(WIN32 TRUE)
set(PROJECT_SOURCE_DIR "{project.as_posix()}")
set(CMAKE_BINARY_DIR "{(work / 'build').as_posix()}")
set(ANANARCHIVER_BUILD_SOURCE_MANIFEST "{manifest.as_posix()}")
find_package(Git REQUIRED)
include("{(repository / 'cmake/PrepareQt.cmake').as_posix()}")
ananarchiver_prepare_qt(output)
file(WRITE "{(work / 'output.txt').as_posix()}" "${{output}}\\n${{ANANARCHIVER_QT_PATCH_SHA256}}\\n")
''', encoding="utf-8")
        execute(["cmake", "-P", str(script)])
        lines = (work / "output.txt").read_text().splitlines()
        prepared = Path(lines[0]) / "qtbase"
        assert lines[1]
        assert execute(["git", "-C", str(prepared), "rev-parse", "HEAD"]) == revision
        assert "LoadLibraryW" in (prepared / "src/corelib/plugin/qlibrary_win.cpp").read_text()
        assert "wchar_t buffer[MAX_PATH]" not in (prepared / "src/corelib/plugin/qlibrary_win.cpp").read_text()
        assert "w->surfaceType() != QSurface::OpenGLSurface" in (prepared / "src/plugins/platforms/windows/qwindowswindow.cpp").read_text()
        assert hashlib.sha256(loader.read_bytes()).hexdigest() == original_hash
        assert hashlib.sha256(platform.read_bytes()).hexdigest() == original_platform_hash
        assert not execute(["git", "-C", str(base), "status", "--porcelain"])
        # Upstream can integrate the Core and QPA fixes in separate releases.
        # Preparation must apply only the missing file without reapplying Core.
        execute(["git", "-C", str(base), "apply", "--include=src/corelib/plugin/qlibrary_win.cpp",
                 str(patch_dir / "qt-windows-longpaths.patch")])
        execute(["git", "-C", str(base), "add", "."])
        execute(["git", "-C", str(base), "-c", "user.name=Qt patch regression",
                 "-c", "user.email=qt-patch@example.invalid", "commit", "--quiet", "-m", "Upstream Core fix"])
        revision = execute(["git", "-C", str(base), "rev-parse", "HEAD"])
        manifest.write_text(json.dumps({"dependencies": [{"path": "third_party/qt/qtbase", "revision": revision,
                                                          "url": "https://code.qt.io/qt/qtbase.git"}]}), encoding="utf-8")
        execute(["cmake", "-P", str(script)])
        partial = Path((work / "output.txt").read_text().splitlines()[0]) / "qtbase"
        assert "LoadLibraryW" in (partial / "src/corelib/plugin/qlibrary_win.cpp").read_text()
        assert "w->surfaceType() != QSurface::OpenGLSurface" in (partial / "src/plugins/platforms/windows/qwindowswindow.cpp").read_text()
        # Simulate upstream integrating this exact fix. Preparation must stop
        # applying the local patch and use the resolved checkout directly.
        execute(["git", "-C", str(base), "apply", "--include=src/plugins/platforms/windows/qwindowswindow.cpp",
                 str(patch_dir / "qt-windows-longpaths.patch")])
        execute(["cmake", "-P", str(script)])
        lines = (work / "output.txt").read_text().splitlines()
        assert Path(lines[0]) == project / "third_party/qt"
        assert len(lines) == 2 and not lines[1]
        print("PASS immutable private source, retained revision, long loader, raster GPU isolation and integrated-upstream skip")


if __name__ == "__main__":
    main()
