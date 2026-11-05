cmake_minimum_required(VERSION 3.25)
project(LiteArc VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>\nint main() { std::expected<int, int> value{42}; return value.value() == 42 ? 0 : 1; }"
    ANANARCHIVER_HAS_CXX23_EXPECTED)
if(NOT ANANARCHIVER_HAS_CXX23_EXPECTED)
    message(FATAL_ERROR "The compiler and C++ standard library must support C++23 std::expected. Select a current MSVC, GCC, or Clang toolchain (and a compatible libc++/libstdc++) before building dependencies.")
endif()
find_package(Threads REQUIRED)
set(ananarchiver_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
set(ananarchiver_stop_token_probe
    "#include <thread>\n#include <stop_token>\nint main() { std::jthread worker([](std::stop_token) {}); worker.request_stop(); worker.join(); }")
check_cxx_source_compiles("${ananarchiver_stop_token_probe}" ANANARCHIVER_HAS_STD_JTHREAD)
if(NOT ANANARCHIVER_HAS_STD_JTHREAD)
    if(ANDROID)
        # NDK r27/r28 libc++ exposes C++20 stop_token/jthread behind its documented
        # experimental-library option. Check both compilation and linkage first.
        set(ananarchiver_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
        string(APPEND CMAKE_REQUIRED_FLAGS " -fexperimental-library")
        check_cxx_source_compiles("${ananarchiver_stop_token_probe}"
            ANANARCHIVER_HAS_EXPERIMENTAL_STD_JTHREAD)
        set(CMAKE_REQUIRED_FLAGS "${ananarchiver_saved_required_flags}")
        if(NOT ANANARCHIVER_HAS_EXPERIMENTAL_STD_JTHREAD)
            message(FATAL_ERROR "The Android C++ toolchain must support std::stop_token and std::jthread, including their runtime linkage.")
        endif()
        set(ANANARCHIVER_ANDROID_EXPERIMENTAL_LIBRARY ON)
    else()
        message(FATAL_ERROR "The C++ toolchain must support std::stop_token and std::jthread, including their runtime linkage. When selecting libc++ 18 or 19, enable its documented -fexperimental-library option.")
    endif()
endif()
set(CMAKE_REQUIRED_LIBRARIES "${ananarchiver_saved_required_libraries}")

include(CTest)
option(ANANARCHIVER_SUPERBUILD "Build resolved dependencies before the application" ON)
option(ANANARCHIVER_BUILD_QT "Build the Qt Quick desktop frontend" ON)
option(ANANARCHIVER_BUILD_C_API "Build the frontend-independent shared C ABI" ON)
option(ANANARCHIVER_USE_SYSTEM_QT "Use an explicitly selected developer Qt kit" OFF)
option(ANANARCHIVER_USE_SYSTEM_LIBARCHIVE "Use a developer-provided libarchive" OFF)
option(ANANARCHIVER_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(ANANARCHIVER_LIBARCHIVE_USE_CNG "Use native Windows crypto (disable to validate Mbed TLS PSA)" ON)
set(ANANARCHIVER_DEPENDENCY_PREFIX "${CMAKE_BINARY_DIR}/dependencies/install" CACHE PATH
    "Prefix containing dependencies built from third_party")
include(cmake/DependencyProvenance.cmake)
if(BUILD_TESTING)
    add_test(NAME dependency_tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/tests/dependencies" -p "test_*.py")
    set_tests_properties(dependency_tools PROPERTIES TIMEOUT 900)
endif()

if(ANANARCHIVER_SUPERBUILD)
    include(cmake/Superbuild.cmake)
    return()
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Dependencies.cmake)
if(ANANARCHIVER_BUILD_QT)
    ananarchiver_find_qt()
endif()
ananarchiver_configure_dependency_provenance()

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
add_dependencies(ananarchiver_libarchive ananarchiver_verify_dependencies)
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
add_dependencies(ananarchiver_build_info ananarchiver_application)

if(ANANARCHIVER_BUILD_C_API)
    # The whole static dependency chain is linked into a shared FFI library.
    set_target_properties(ananarchiver_core ananarchiver_libarchive ananarchiver_application
        PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(ananarchiver_c_api SHARED
        src/c_api/ananarchiver.cpp
        src/c_api/engine.cpp
        src/c_api/results.cpp)
    target_link_libraries(ananarchiver_c_api PRIVATE ananarchiver_application)
    target_include_directories(ananarchiver_c_api PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
    target_compile_definitions(ananarchiver_c_api PRIVATE ANANARCHIVER_C_API_BUILD)
    set_target_properties(ananarchiver_c_api PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
        VERSION 1.0.0 SOVERSION 1)
    # Static backend/codec objects must not accidentally become a second public
    # ABI when linked into the shared library on ELF or Mach-O platforms.
    if(APPLE)
        target_link_options(ananarchiver_c_api PRIVATE
            "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/c_api.exports.macos")
        set_property(TARGET ananarchiver_c_api APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/c_api.exports.macos")
    elseif(UNIX)
        target_link_options(ananarchiver_c_api PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/c_api.exports.map")
        set_property(TARGET ananarchiver_c_api APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/c_api.exports.map")
    endif()
    ananarchiver_project_options(ananarchiver_c_api)
    add_dependencies(ananarchiver_build_info ananarchiver_c_api)
    install(TARGETS ananarchiver_c_api
        RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
    install(FILES include/ananarchiver/c_api/ananarchiver.h DESTINATION include/ananarchiver/c_api)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
if(ANANARCHIVER_BUILD_QT)
    qt_standard_project_setup(REQUIRES 6.10)
    add_subdirectory(frontend/qt)
    add_dependencies(ananarchiver_build_info AnAnArchiver)
endif()
