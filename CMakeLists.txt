cmake_minimum_required(VERSION 3.25)
project(AnAnArchiver VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>\nint main() { std::expected<int, int> value{42}; return value.value() == 42 ? 0 : 1; }"
    ANANARCHIVER_HAS_CXX23_EXPECTED)
if(NOT ANANARCHIVER_HAS_CXX23_EXPECTED)
    message(FATAL_ERROR "The compiler and C++ standard library must support C++23 std::expected. Select a current MSVC, GCC, or Clang toolchain (and a compatible libc++/libstdc++) before building dependencies.")
endif()

include(CTest)
option(ANANARCHIVER_SUPERBUILD "Build pinned dependencies before the application" ON)
option(ANANARCHIVER_BUILD_QT "Build the Qt Quick desktop frontend" ON)
option(ANANARCHIVER_USE_SYSTEM_QT "Use an explicitly selected developer Qt kit" OFF)
option(ANANARCHIVER_USE_SYSTEM_LIBARCHIVE "Use a developer-provided libarchive" OFF)
option(ANANARCHIVER_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
set(ANANARCHIVER_DEPENDENCY_PREFIX "${CMAKE_BINARY_DIR}/dependencies/install" CACHE PATH
    "Prefix containing dependencies built from third_party")

if(ANANARCHIVER_SUPERBUILD)
    include(cmake/Superbuild.cmake)
    return()
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

add_library(ananarchiver_core STATIC
    src/core/archive/archive_format.cpp
    src/core/archive/unicode.cpp
    src/core/filesystem/path_security.cpp)
target_include_directories(ananarchiver_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ananarchiver_core PUBLIC cxx_std_23)
ananarchiver_project_options(ananarchiver_core)

add_library(ananarchiver_libarchive STATIC
    src/backend/libarchive/libarchive_reader.cpp
    src/backend/libarchive/libarchive_writer.cpp)
if(WIN32)
    target_sources(ananarchiver_libarchive PRIVATE
        src/platform/windows/safe_output.cpp
        src/platform/windows/compression_io.cpp)
    target_link_libraries(ananarchiver_libarchive PRIVATE advapi32)
else()
    target_sources(ananarchiver_libarchive PRIVATE
        src/platform/posix/safe_output.cpp
        src/platform/posix/compression_io.cpp)
endif()
target_link_libraries(ananarchiver_libarchive PUBLIC ananarchiver_core PRIVATE AnAnArchiver::Archive)
ananarchiver_project_options(ananarchiver_libarchive)

add_library(ananarchiver_application STATIC
    src/application/archive_service.cpp
    src/application/task_manager.cpp
    src/application/directory_service.cpp)
target_link_libraries(ananarchiver_application PUBLIC ananarchiver_core Threads::Threads
    PRIVATE ananarchiver_libarchive)
ananarchiver_project_options(ananarchiver_application)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
if(ANANARCHIVER_BUILD_QT)
    ananarchiver_find_qt()
    qt_standard_project_setup(REQUIRES 6.10)
    add_subdirectory(frontend/qt)
endif()
