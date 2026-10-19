"""Exercise compiler-flag forwarding through the real superbuild's child builds.

The fixture enables no compiler and uses no SDK. It simulates the NDK distinction
between raw user flags in the CMake cache and effective normal variables with
platform flags prepended, then builds both actual ExternalProject routes.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")
NINJA = shutil.which("ninja")
FLAG_NAMES = tuple(f"CMAKE_{language}_FLAGS{configuration}"
                   for configuration in ("", "_RELEASE", "_DEBUG", "_RELWITHDEBINFO", "_MINSIZEREL")
                   for language in ("C", "CXX"))
RUNTIME_NAME = "CMAKE_MSVC_RUNTIME_LIBRARY"

FIXTURE = r"""
cmake_minimum_required(VERSION 3.25)
project(SuperbuildFlagForwarding NONE)
option(ANANARCHIVER_SUPERBUILD "Exercise production ExternalProject wiring" ON)
set(ANANARCHIVER_USE_SYSTEM_LIBARCHIVE ON CACHE BOOL "Skip backend sources" FORCE)
set(ANANARCHIVER_BUILD_QT OFF CACHE BOOL "Skip Qt sources" FORCE)
set(ANANARCHIVER_USE_SYSTEM_QT ON CACHE BOOL "Skip Qt sources" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "No nested test discovery" FORCE)
if(NOT ANANARCHIVER_DEPENDENCY_PREFIX)
    set(ANANARCHIVER_DEPENDENCY_PREFIX "${CMAKE_BINARY_DIR}/install")
endif()

# The production Android forwarding carries ANDROID_ABI into both children.
# Simulate its toolchain without enabling Android platform/compiler detection.
set(ANDROID OFF)
if(ANDROID_ABI STREQUAL "fixture-arm64")
    set(ANDROID ON)
endif()
set(flag_names @FLAG_NAMES@)
foreach(variable IN LISTS flag_names)
    set(${variable} "" CACHE STRING "Raw caller flags")
    if(ANDROID OR ANANARCHIVER_SUPERBUILD)
        if(ANDROID)
            set(platform_flag "-DNDK_${variable}=1")
        else()
            set(platform_flag "-DHOST_${variable}=1")
        endif()
        # Like the NDK, apply a platform prefix to the cache's user value. An
        # empty cache string must stay empty when forwarded into the child.
        set(${variable} "${platform_flag} $CACHE{${variable}}")
    endif()
endforeach()
set(CMAKE_MSVC_RUNTIME_LIBRARY "fixture-runtime-cache" CACHE STRING "Runtime cache value")
if(ANANARCHIVER_SUPERBUILD)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "fixture-runtime-normal")
endif()

file(WRITE "${CMAKE_BINARY_DIR}/effective-flags.txt" "")
file(WRITE "${CMAKE_BINARY_DIR}/cache-flags.txt" "")
foreach(variable IN LISTS flag_names ITEMS CMAKE_MSVC_RUNTIME_LIBRARY)
    file(APPEND "${CMAKE_BINARY_DIR}/effective-flags.txt" "${variable}=${${variable}}\n")
    file(APPEND "${CMAKE_BINARY_DIR}/cache-flags.txt" "${variable}=$CACHE{${variable}}\n")
endforeach()

if(ANANARCHIVER_SUPERBUILD)
    # Replace provenance I/O only; the production dependency/app configure,
    # build, log wrappers, install command and dependency wiring remain intact.
    function(ananarchiver_configure_dependency_provenance)
        add_custom_target(ananarchiver_verify_dependencies)
        add_custom_target(ananarchiver_build_info)
        set(ANANARCHIVER_TRACE_COMMAND
            "${CMAKE_COMMAND}" -P "${PROJECT_SOURCE_DIR}/trace-noop.cmake" PARENT_SCOPE)
    endfunction()
    include("@SUPERBUILD@")
    ananarchiver_dependency(flag_probe "${PROJECT_SOURCE_DIR}"
        -DANANARCHIVER_SUPERBUILD:BOOL=OFF)
    add_dependencies(ananarchiver_record_dependencies dependency_flag_probe)
else()
    # Both child builds have real build/install steps but perform no compile or
    # archive operation, so this test never needs NDK, Qt or libarchive binaries.
    add_custom_target(flag_fixture ALL COMMAND "${CMAKE_COMMAND}" -E true)
    install(FILES "${CMAKE_BINARY_DIR}/effective-flags.txt" DESTINATION flag-fixture)
endif()
"""


def cmake_path(path):
    return str(path.resolve()).replace("\\", "/")


def read_flags(directory, name):
    return dict(line.split("=", 1) for line in (directory / name).read_text(encoding="utf-8").splitlines())


@unittest.skipUnless(CMAKE and NINJA, "CMake and Ninja required")
class AndroidSuperbuildFlagTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ananarchiver-flags-\u8def\u5f84-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.source = self.root / "fixture source with spaces"
        self.source.mkdir()
        self.build = self.root / "fixture build with spaces"
        source_text = FIXTURE.replace("@FLAG_NAMES@", " ".join(FLAG_NAMES)).replace(
            "@SUPERBUILD@", cmake_path(ROOT / "cmake/Superbuild.cmake"))
        (self.source / "CMakeLists.txt").write_text(source_text, encoding="utf-8")
        (self.source / "trace-noop.cmake").write_text("# No provenance I/O in this fixture.\n", encoding="utf-8")

    def run_command(self, arguments):
        process = subprocess.run(arguments, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=45,
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)

    def build_fixture(self, *, android, user_flags):
        arguments = [CMAKE, "-S", str(self.source), "-B", str(self.build), "-G", "Ninja",
                     f"-DCMAKE_MAKE_PROGRAM:FILEPATH={cmake_path(Path(NINJA))}",
                     "-DCMAKE_BUILD_TYPE:STRING=Release"]
        if android:
            arguments.append("-DANDROID_ABI:STRING=fixture-arm64")
        arguments.extend(f"-D{variable}:STRING={user_flags[variable]}" for variable in FLAG_NAMES)
        self.run_command(arguments)
        self.run_command([CMAKE, "--build", str(self.build), "--parallel", "2"])
        parent_effective = read_flags(self.build, "effective-flags.txt")
        parent_cache = read_flags(self.build, "cache-flags.txt")
        self.assertEqual(user_flags, {name: parent_cache[name] for name in FLAG_NAMES})
        children = (self.build / "application", self.build / "dependencies/build/flag_probe")
        # Confirm the generic dependency's real install step completed as well
        # as its LOG_CONFIGURE wrapper, rather than just inspecting arguments.
        self.assertTrue((self.build / "install/flag-fixture/effective-flags.txt").is_file())
        return parent_effective, children

    def assert_forwarded(self, parent_effective, children, expected_cache):
        for directory in children:
            with self.subTest(child=str(directory.relative_to(self.build))):
                effective = read_flags(directory, "effective-flags.txt")
                cache = read_flags(directory, "cache-flags.txt")
                # Exact comparison catches repeated platform prefixes, lost
                # user options, changed ordering and whitespace alterations.
                self.assertEqual(parent_effective, effective)
                self.assertEqual(expected_cache, {name: cache[name] for name in FLAG_NAMES})
                # The special MSVC runtime option must retain its existing
                # normal-variable forwarding policy even in the Android case.
                self.assertEqual("fixture-runtime-normal", cache[RUNTIME_NAME])

    def test_android_raw_user_flags_are_applied_once_and_in_order(self):
        user_flags = {name: f"-DUSER_{name}_FIRST=1 -DUSER_{name}_SECOND=2" for name in FLAG_NAMES}
        effective, children = self.build_fixture(android=True, user_flags=user_flags)
        self.assert_forwarded(effective, children, user_flags)
        for directory in children:
            flags = read_flags(directory, "effective-flags.txt")
            for name in FLAG_NAMES:
                self.assertEqual(1, flags[name].count(f"-DNDK_{name}=1"))
                self.assertEqual(1, flags[name].count(f"-DUSER_{name}_FIRST=1"))
                self.assertEqual(1, flags[name].count(f"-DUSER_{name}_SECOND=2"))

    def test_android_empty_cache_flags_do_not_forward_platform_defaults(self):
        user_flags = dict.fromkeys(FLAG_NAMES, "")
        effective, children = self.build_fixture(android=True, user_flags=user_flags)
        self.assert_forwarded(effective, children, user_flags)

    def test_non_android_keeps_effective_normal_variable_forwarding(self):
        user_flags = {name: f"-DUSER_{name}=1" for name in FLAG_NAMES}
        effective, children = self.build_fixture(android=False, user_flags=user_flags)
        expected_cache = {name: f"-DHOST_{name}=1 {user_flags[name]}" for name in FLAG_NAMES}
        self.assert_forwarded(effective, children, expected_cache)


if __name__ == "__main__":
    unittest.main()
