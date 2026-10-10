include_guard(GLOBAL)

# The selected backend still uses the Mbed TLS 3 crypto API. This upstream
# compatibility patch implements PSA for Mbed TLS 4; keep upstream checkout clean
# and record the exact patch applied to the generated build source.
function(ananarchiver_prepare_libarchive output)
    set(patch "${PROJECT_SOURCE_DIR}/cmake/patches/libarchive-mbedtls4.patch")
    file(SHA256 "${patch}" patch_hash)
    file(STRINGS "${patch}" patch_header LIMIT_COUNT 1)
    if(NOT patch_header MATCHES "^From ([0-9a-f]+) ")
        message(FATAL_ERROR "Backend compatibility patch has no upstream commit provenance")
    endif()
    set(ANANARCHIVER_LIBARCHIVE_PATCH_COMMIT "${CMAKE_MATCH_1}" CACHE INTERNAL "Backend patch commit" FORCE)
    string(SUBSTRING "${patch_hash}" 0 12 patch_id)
    set(ANANARCHIVER_LIBARCHIVE_BUILD_ID "${ANANARCHIVER_LIBARCHIVE_REVISION}-${patch_id}" PARENT_SCOPE)
    set(source "${CMAKE_BINARY_DIR}/dependencies/sources/libarchive-${ANANARCHIVER_LIBARCHIVE_REVISION}-${patch_id}")
    if(NOT EXISTS "${source}/.prepared")
        file(MAKE_DIRECTORY "${source}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/third_party/libarchive"
            archive --format=tar "--output=${source}/upstream.tar" "${ANANARCHIVER_LIBARCHIVE_REVISION}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf upstream.tar
            WORKING_DIRECTORY "${source}" COMMAND_ERROR_IS_FATAL ANY)
        file(REMOVE "${source}/upstream.tar")
        execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${source}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" apply --check "${patch}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" apply "${patch}"
            COMMAND_ERROR_IS_FATAL ANY)
        file(WRITE "${source}/.prepared" "${ANANARCHIVER_LIBARCHIVE_REVISION}\n${patch_hash}\n")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    set(ANANARCHIVER_LIBARCHIVE_PATCH_SHA256 "${patch_hash}" CACHE INTERNAL "Applied backend compatibility patch" FORCE)
    set(${output} "${source}" PARENT_SCOPE)
endfunction()
