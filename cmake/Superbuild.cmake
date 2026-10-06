include(ExternalProject)

if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Use a single-configuration generator (Ninja or Unix Makefiles) for the dependency superbuild")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
set(ANANARCHIVER_DEPENDENCY_JOBS 4 CACHE STRING "Parallel jobs per dependency build")
set(dependency_build_root "${CMAKE_BINARY_DIR}/dependencies/build")
set(ANANARCHIVER_QT_BUILD_DIRECTORY "${CMAKE_BINARY_DIR}/qt" CACHE PATH
    "Short Qt source build directory, to avoid generated MSVC object path limits")
set(dependency_arguments
    "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX:PATH=${ANANARCHIVER_DEPENDENCY_PREFIX}"
    "-DCMAKE_INSTALL_LIBDIR:STRING=lib"
    "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}"
    "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}"
    "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
    "-DBUILD_SHARED_LIBS:BOOL=OFF"
    "-DBUILD_TESTING:BOOL=OFF"
    "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5")
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND dependency_arguments "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "|" mac_architectures "${CMAKE_OSX_ARCHITECTURES}")
    list(APPEND dependency_arguments "-DCMAKE_OSX_ARCHITECTURES:STRING=${mac_architectures}")
endif()

function(ananarchiver_require_source name relative_path)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/${name}/${relative_path}")
        message(FATAL_ERROR "Missing ${name} source. Run cmake -P scripts/init-dependencies.cmake (add -DANANARCHIVER_INIT_QT=ON for source Qt)")
    endif()
endfunction()

function(ananarchiver_dependency name source)
    cmake_parse_arguments(dependency "" "" "DEPENDS" ${ARGN})
    set(dependency_binary_directory "${dependency_build_root}/${name}")
    if(name STREQUAL "qt")
        set(dependency_binary_directory "${ANANARCHIVER_QT_BUILD_DIRECTORY}")
    endif()
    ExternalProject_Add(dependency_${name}
        SOURCE_DIR "${source}"
        BINARY_DIR "${dependency_binary_directory}"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        STEP_TARGETS configure
        BUILD_ALWAYS ON
        LIST_SEPARATOR "|"
        CMAKE_ARGS ${dependency_arguments} ${dependency_UNPARSED_ARGUMENTS}
        DEPENDS ${dependency_DEPENDS}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ANANARCHIVER_DEPENDENCY_JOBS}"
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
        LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
        LOG_OUTPUT_ON_FAILURE ON)
endfunction()

set(archive_dependencies "")
if(NOT ANANARCHIVER_USE_SYSTEM_LIBARCHIVE)
    foreach(dependency IN ITEMS zlib bzip2 xz zstd lz4 expat mbedtls libarchive)
        ananarchiver_require_source(${dependency} .git)
    endforeach()
    # The two small build adapters reference upstream files without copying parser/codec code.
    ananarchiver_dependency(zlib "${PROJECT_SOURCE_DIR}/cmake/vendor/zlib"
        "-DANANARCHIVER_VENDOR_SOURCE:PATH=${PROJECT_SOURCE_DIR}/third_party/zlib")
    ananarchiver_dependency(bzip2 "${PROJECT_SOURCE_DIR}/cmake/vendor/bzip2"
        "-DANANARCHIVER_VENDOR_SOURCE:PATH=${PROJECT_SOURCE_DIR}/third_party/bzip2")
    ananarchiver_dependency(xz "${PROJECT_SOURCE_DIR}/third_party/xz"
        -DXZ_NLS=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF
        -DXZ_TOOL_LZMAINFO=OFF -DXZ_DOC=OFF -DXZ_EXTERNAL_SHA256=OFF)
    ananarchiver_dependency(zstd "${PROJECT_SOURCE_DIR}/third_party/zstd/build/cmake"
        -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON)
    ananarchiver_dependency(lz4 "${PROJECT_SOURCE_DIR}/third_party/lz4/build/cmake"
        -DLZ4_BUILD_CLI=OFF -DLZ4_BUNDLED_MODE=OFF -DBUILD_STATIC_LIBS=ON)
    ananarchiver_dependency(expat "${PROJECT_SOURCE_DIR}/third_party/expat/expat"
        -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF
        -DEXPAT_BUILD_DOCS=OFF -DEXPAT_SHARED_LIBS=OFF)
    ananarchiver_require_source(mbedtls framework/CMakeLists.txt)
    ananarchiver_dependency(mbedtls "${PROJECT_SOURCE_DIR}/third_party/mbedtls"
        -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF)
    set(codec_dependencies dependency_zlib dependency_bzip2 dependency_xz dependency_zstd
        dependency_lz4 dependency_expat dependency_mbedtls)
    # Searches inside this build's prefix only; no optional codec can come from the host.
    ananarchiver_dependency(libarchive "${PROJECT_SOURCE_DIR}/third_party/libarchive"
        "-DCMAKE_PREFIX_PATH:PATH=${ANANARCHIVER_DEPENDENCY_PREFIX}"
        "-DCMAKE_FIND_ROOT_PATH:PATH=${ANANARCHIVER_DEPENDENCY_PREFIX}"
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
        -DENABLE_TEST=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF
        -DENABLE_WERROR=OFF -DENABLE_INSTALL=ON -DENABLE_LIBXML2=OFF -DENABLE_BSDXML=OFF
        -DENABLE_EXPAT=ON -DENABLE_WIN32_XMLLITE=OFF -DENABLE_OPENSSL=OFF -DENABLE_NETTLE=OFF
        -DENABLE_MBEDTLS=ON -DENABLE_CNG=ON -DENABLE_LIBB2=OFF -DENABLE_LZO=OFF
        -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_ACL=OFF
        -DENABLE_XATTR=OFF -DENABLE_ICONV=ON -DZLIB_USE_STATIC_LIBS=ON
        -DEXPAT_USE_STATIC_LIBS=ON "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} -DXML_STATIC"
        DEPENDS ${codec_dependencies})
    list(APPEND archive_dependencies dependency_libarchive)
endif()

set(qt_dependencies "")
if(ANANARCHIVER_BUILD_QT AND NOT ANANARCHIVER_USE_SYSTEM_QT)
    ananarchiver_require_source(vulkan-headers include/vulkan/vulkan.h)
    set(Vulkan_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/vulkan-headers/include" CACHE PATH
        "Vulkan API headers used to compile Qt (default: pinned project submodule)")
    foreach(module IN ITEMS qtbase qtshadertools qtdeclarative qtsvg)
        ananarchiver_require_source(qt "${module}/CMakeLists.txt")
    endforeach()
    ananarchiver_dependency(qt "${PROJECT_SOURCE_DIR}/third_party/qt"
        "-DCMAKE_INSTALL_PREFIX:PATH=${ANANARCHIVER_DEPENDENCY_PREFIX}/qt"
        "-DQT_BUILD_SUBMODULES:STRING=qtbase|qtshadertools|qtdeclarative|qtsvg"
        "-DVulkan_INCLUDE_DIR:PATH=${Vulkan_INCLUDE_DIR}"
        -DBUILD_SHARED_LIBS=ON -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_BENCHMARKS=OFF -DQT_BUILD_TESTS_BY_DEFAULT=OFF -DFEATURE_openssl=OFF
        -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF -DFEATURE_system_freetype=OFF
        -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_libb2=OFF -DFEATURE_system_doubleconversion=OFF
        -DFEATURE_system_libtiff=OFF -DFEATURE_system_libpng=OFF -DFEATURE_system_libjpeg=OFF
        -DFEATURE_icu=OFF -DFEATURE_dbus=OFF -DFEATURE_cups=OFF
        -DFEATURE_quickcontrols2_basic=ON -DFEATURE_quickcontrols2_fusion=OFF
        -DFEATURE_quickcontrols2_imagine=OFF -DFEATURE_quickcontrols2_material=OFF
        -DFEATURE_quickcontrols2_universal=OFF -DFEATURE_quickcontrols2_fluentwinui3=OFF
        -DFEATURE_quickcontrols2_windows=OFF -DFEATURE_quickcontrols2_macos=OFF
        -DFEATURE_quickcontrols2_ios=OFF)
    list(APPEND qt_dependencies dependency_qt)
endif()

add_custom_target(dependencies DEPENDS ${archive_dependencies} ${qt_dependencies})
set(application_arguments ${dependency_arguments}
    -DANANARCHIVER_SUPERBUILD=OFF
    "-DANANARCHIVER_BUILD_QT:BOOL=${ANANARCHIVER_BUILD_QT}"
    "-DANANARCHIVER_USE_SYSTEM_QT:BOOL=${ANANARCHIVER_USE_SYSTEM_QT}"
    "-DANANARCHIVER_USE_SYSTEM_LIBARCHIVE:BOOL=${ANANARCHIVER_USE_SYSTEM_LIBARCHIVE}"
    "-DANANARCHIVER_ENABLE_SANITIZERS:BOOL=${ANANARCHIVER_ENABLE_SANITIZERS}"
    "-DANANARCHIVER_DEPENDENCY_PREFIX:PATH=${ANANARCHIVER_DEPENDENCY_PREFIX}"
    "-DBUILD_TESTING:BOOL=${BUILD_TESTING}")
if(CMAKE_PREFIX_PATH)
    string(REPLACE ";" "|" developer_prefix_path "${CMAKE_PREFIX_PATH}")
    list(APPEND application_arguments "-DCMAKE_PREFIX_PATH:STRING=${developer_prefix_path}")
endif()
ExternalProject_Add(ananarchiver
    SOURCE_DIR "${PROJECT_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/application"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" LIST_SEPARATOR "|"
    CMAKE_ARGS ${application_arguments}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ANANARCHIVER_DEPENDENCY_JOBS}"
    BUILD_ALWAYS ON INSTALL_COMMAND ""
    DEPENDS ${archive_dependencies} ${qt_dependencies})
if(BUILD_TESTING)
    add_test(NAME application_suite COMMAND "${CMAKE_CTEST_COMMAND}"
        --test-dir "${CMAKE_BINARY_DIR}/application" --output-on-failure)
endif()
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" --install \"${CMAKE_BINARY_DIR}/application\" --prefix \"\${CMAKE_INSTALL_PREFIX}\" COMMAND_ERROR_IS_FATAL ANY)")
