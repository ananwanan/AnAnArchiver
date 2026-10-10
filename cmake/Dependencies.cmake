include_guard(GLOBAL)

function(ananarchiver_find_bundled_library output)
    find_library(${output} NAMES ${ARGN}
        PATHS "${ANANARCHIVER_DEPENDENCY_PREFIX}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    set(${output} "${${output}}" PARENT_SCOPE)
endfunction()

if(ANANARCHIVER_USE_SYSTEM_LIBARCHIVE)
    find_package(LibArchive REQUIRED)
    set(ANANARCHIVER_LIBARCHIVE_VERSION "${LibArchive_VERSION}")
    set(ANANARCHIVER_LIBARCHIVE_REVISION "system-package")
    set(ANANARCHIVER_LIBARCHIVE_PATCH_SHA256 "")
    set(ANANARCHIVER_LIBARCHIVE_PATCH_COMMIT "")
    add_library(AnAnArchiver::Archive ALIAS LibArchive::LibArchive)
else()
    ananarchiver_check_installed_provenance()
    if(NOT EXISTS "${ANANARCHIVER_DEPENDENCY_PREFIX}/include/archive.h")
        message(FATAL_ERROR "Bundled dependencies are missing. Configure with ANANARCHIVER_SUPERBUILD=ON first.")
    endif()
    ananarchiver_find_bundled_library(archive_library archive archive_static)
    ananarchiver_find_bundled_library(zlib_library zlibstatic z)
    ananarchiver_find_bundled_library(bzip2_library bz2)
    ananarchiver_find_bundled_library(lzma_library lzma liblzma)
    ananarchiver_find_bundled_library(zstd_library zstd libzstd_static zstd_static)
    ananarchiver_find_bundled_library(lz4_library lz4 liblz4)
    ananarchiver_find_bundled_library(expat_library expat expatd libexpat libexpatd libexpatMD libexpatdMD)
    find_package(MbedTLS CONFIG REQUIRED
        PATHS "${ANANARCHIVER_DEPENDENCY_PREFIX}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(TARGET MbedTLS::mbedcrypto)
        set(archive_crypto_target MbedTLS::mbedcrypto)
    else()
        find_package(TF-PSA-Crypto CONFIG REQUIRED
            PATHS "${ANANARCHIVER_DEPENDENCY_PREFIX}"
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        set(archive_crypto_target TF-PSA-Crypto::tfpsacrypto)
    endif()
    add_library(AnAnArchiver::Archive STATIC IMPORTED GLOBAL)
    set_target_properties(AnAnArchiver::Archive PROPERTIES
        IMPORTED_LOCATION "${archive_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${ANANARCHIVER_DEPENDENCY_PREFIX}/include"
        INTERFACE_COMPILE_DEFINITIONS "LIBARCHIVE_STATIC"
        INTERFACE_LINK_LIBRARIES "${zlib_library};${bzip2_library};${lzma_library};${zstd_library};${lz4_library};${expat_library};${archive_crypto_target};Threads::Threads")
    if(WIN32)
        set_property(TARGET AnAnArchiver::Archive APPEND PROPERTY INTERFACE_LINK_LIBRARIES bcrypt)
    endif()
endif()

macro(ananarchiver_find_qt)
    if(ANANARCHIVER_USE_SYSTEM_QT)
        if(NOT CMAKE_PREFIX_PATH AND NOT DEFINED ENV{QTDIR})
            message(FATAL_ERROR "Set QTDIR or CMAKE_PREFIX_PATH for the developer Qt kit")
        endif()
        if(DEFINED ENV{QTDIR})
            list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
        endif()
        find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2)
    else()
        # Qt component/tool packages perform nested find_package calls. Keep those
        # on this kit too, including on machines with Qt Creator package registry entries.
        set(CMAKE_PREFIX_PATH "${ANANARCHIVER_DEPENDENCY_PREFIX}/qt")
        set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
        set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
        set(QT_HOST_PATH "${ANANARCHIVER_DEPENDENCY_PREFIX}/qt")
        set(QT_HOST_PATH_CMAKE_DIR "${ANANARCHIVER_DEPENDENCY_PREFIX}/qt/lib/cmake")
        set(Vulkan_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/vulkan-headers/include"
            CACHE PATH "Resolved Vulkan headers for the bundled Qt kit" FORCE)
        file(GLOB qt_package_directories LIST_DIRECTORIES TRUE
            "${ANANARCHIVER_DEPENDENCY_PREFIX}/qt/lib/cmake/Qt6*")
        foreach(qt_package_directory IN LISTS qt_package_directories)
            get_filename_component(qt_package_name "${qt_package_directory}" NAME)
            if(EXISTS "${qt_package_directory}/${qt_package_name}Config.cmake")
                set(${qt_package_name}_DIR "${qt_package_directory}" CACHE PATH "Resolved Qt kit component" FORCE)
            endif()
        endforeach()
        find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2
            PATHS "${ANANARCHIVER_DEPENDENCY_PREFIX}/qt" NO_DEFAULT_PATH)
    endif()
    if(ANANARCHIVER_RESOLVED_QT_VERSION AND NOT Qt6_VERSION VERSION_EQUAL ANANARCHIVER_RESOLVED_QT_VERSION)
        message(FATAL_ERROR "Qt kit ${Qt6_VERSION} does not match resolved Qt ${ANANARCHIVER_RESOLVED_QT_VERSION}. Select the matching kit or resolve sources for a new build.")
    endif()
endmacro()
