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
ananarchiver_configure_traceability()
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
foreach(variable IN ITEMS CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_MSVC_RUNTIME_LIBRARY)
    if(DEFINED ${variable})
        list(APPEND dependency_arguments "-D${variable}:STRING=${${variable}}")
    endif()
endforeach()
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND dependency_arguments "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(ANDROID)
    # ExternalProject configures a new CMake process. Preserve the selected
    # Android kit and host tools instead of falling back to host libraries.
    foreach(variable IN ITEMS ANDROID_ABI ANDROID_PLATFORM ANDROID_STL
            ANDROID_SDK_ROOT ANDROID_NDK_ROOT QT_HOST_PATH QT_HOST_PATH_CMAKE_DIR)
        if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
            list(APPEND dependency_arguments "-D${variable}:STRING=${${variable}}")
        endif()
    endforeach()
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
        DEPENDS ananarchiver_verify_dependencies ${dependency_DEPENDS}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ANANARCHIVER_DEPENDENCY_JOBS}"
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
        LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
        LOG_OUTPUT_ON_FAILURE ON)
    add_dependencies(dependency_${name} ananarchiver_verify_dependencies)
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
    execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import jinja2, jsonschema"
        RESULT_VARIABLE mbedtls_python_status ERROR_VARIABLE mbedtls_python_error)
    if(NOT mbedtls_python_status EQUAL 0)
        message(FATAL_ERROR "Mbed TLS source generation requires Jinja2 and jsonschema in ${Python3_EXECUTABLE}. Run python -m pip install --upgrade jinja2 jsonschema. ${mbedtls_python_error}")
    endif()
    find_package(Git REQUIRED)
    get_filename_component(git_binary_directory "${GIT_EXECUTABLE}" DIRECTORY)
    find_program(ANANARCHIVER_PERL_EXECUTABLE NAMES perl
        HINTS "${git_binary_directory}/../usr/bin" "${git_binary_directory}/../../usr/bin" REQUIRED)
    set(mbedtls_pthread OFF)
    if(NOT WIN32)
        set(mbedtls_pthread ON)
    endif()
    ananarchiver_dependency(mbedtls "${PROJECT_SOURCE_DIR}/third_party/mbedtls"
        -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
        "-DLINK_WITH_PTHREAD:BOOL=${mbedtls_pthread}"
        "-DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}"
        "-DPERL_EXECUTABLE:FILEPATH=${ANANARCHIVER_PERL_EXECUTABLE}"
        "-DTF_PSA_CRYPTO_USER_CONFIG_FILE:FILEPATH=${PROJECT_SOURCE_DIR}/cmake/vendor/mbedtls/crypto_config.h")
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
        "Vulkan API headers used to compile Qt (default: resolved project submodule)")
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

add_custom_target(ananarchiver_record_dependencies
    COMMAND ${ANANARCHIVER_TRACE_COMMAND} --operation record-prefix
    DEPENDS ${archive_dependencies} ${qt_dependencies} VERBATIM)
add_custom_target(dependencies DEPENDS ananarchiver_record_dependencies)
add_dependencies(ananarchiver_build_info ananarchiver_record_dependencies)
set(application_arguments ${dependency_arguments}
    -DANANARCHIVER_SUPERBUILD=OFF
    "-DANANARCHIVER_BUILD_QT:BOOL=${ANANARCHIVER_BUILD_QT}"
    "-DANANARCHIVER_USE_SYSTEM_QT:BOOL=${ANANARCHIVER_USE_SYSTEM_QT}"
    "-DANANARCHIVER_USE_SYSTEM_LIBARCHIVE:BOOL=${ANANARCHIVER_USE_SYSTEM_LIBARCHIVE}"
    "-DANANARCHIVER_ENABLE_SANITIZERS:BOOL=${ANANARCHIVER_ENABLE_SANITIZERS}"
    "-DANANARCHIVER_DEPENDENCY_PREFIX:PATH=${ANANARCHIVER_DEPENDENCY_PREFIX}"
    "-DANANARCHIVER_DEPENDENCY_MANIFEST:FILEPATH=${ANANARCHIVER_DEPENDENCY_MANIFEST}"
    "-DBUILD_TESTING:BOOL=${BUILD_TESTING}")
if(CMAKE_PREFIX_PATH)
    string(REPLACE ";" "|" developer_prefix_path "${CMAKE_PREFIX_PATH}")
    list(APPEND application_arguments "-DCMAKE_PREFIX_PATH:STRING=${developer_prefix_path}")
endif()
if(ANDROID AND DEFINED QT_ANDROID_DEPLOYMENT_TYPE)
    list(APPEND application_arguments
        "-DQT_ANDROID_DEPLOYMENT_TYPE:STRING=${QT_ANDROID_DEPLOYMENT_TYPE}")
endif()
ExternalProject_Add(ananarchiver
    SOURCE_DIR "${PROJECT_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/application"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" LIST_SEPARATOR "|"
    CMAKE_ARGS ${application_arguments}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ANANARCHIVER_DEPENDENCY_JOBS}"
    BUILD_ALWAYS ON INSTALL_COMMAND ""
    DEPENDS ananarchiver_record_dependencies)
add_dependencies(ananarchiver_build_info ananarchiver)
if(BUILD_TESTING)
    add_test(NAME application_suite COMMAND "${CMAKE_CTEST_COMMAND}"
        --test-dir "${CMAKE_BINARY_DIR}/application" --output-on-failure)
endif()
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" --install \"${CMAKE_BINARY_DIR}/application\" --prefix \"\${CMAKE_INSTALL_PREFIX}\" COMMAND_ERROR_IS_FATAL ANY)")
