include_guard(GLOBAL)

# Older source manifests require this upstream compatibility patch. Once the
# selected backend includes its commit, use the upstream source without applying
# it again. Replay of an older manifest must retain the same patch provenance.
function(ananarchiver_backend_patch_info hash_output commit_output)
    set(patch "${PROJECT_SOURCE_DIR}/cmake/patches/libarchive-mbedtls4.patch")
    file(STRINGS "${patch}" patch_header LIMIT_COUNT 1)
    if(NOT patch_header MATCHES "^From ([0-9a-f]+) ")
        message(FATAL_ERROR "Backend compatibility patch has no upstream commit provenance")
    endif()
    set(patch_commit "${CMAKE_MATCH_1}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/third_party/libarchive"
        merge-base --is-ancestor "${patch_commit}" "${ANANARCHIVER_LIBARCHIVE_REVISION}"
        RESULT_VARIABLE integrated OUTPUT_QUIET ERROR_QUIET)
    # Upstream split/reworked the original combined compatibility commit before
    # merging it. Inspect the selected revision's public PSA adapter as well;
    # its commit ID therefore need not equal the original patch header.
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/third_party/libarchive"
        show "${ANANARCHIVER_LIBARCHIVE_REVISION}:libarchive/archive_mbedtls_private.h"
        RESULT_VARIABLE psa_result OUTPUT_VARIABLE psa_header ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/third_party/libarchive"
        show "${ANANARCHIVER_LIBARCHIVE_REVISION}:libarchive/archive_write_set_format_zip.c"
        RESULT_VARIABLE zip_result OUTPUT_VARIABLE zip_source ERROR_QUIET)
    if(psa_result EQUAL 0 AND psa_header MATCHES "psa/crypto.h" AND
            psa_header MATCHES "__archive_psa_begin" AND zip_result EQUAL 0 AND
            zip_source MATCHES "archive_write_zip_end_compressor")
        set(integrated 0)
    endif()
    if(integrated EQUAL 0)
        set(patch_hash "")
        set(patch_commit "")
    else()
        file(SHA256 "${patch}" patch_hash)
    endif()
    set(${hash_output} "${patch_hash}" PARENT_SCOPE)
    set(${commit_output} "${patch_commit}" PARENT_SCOPE)
endfunction()

function(ananarchiver_prepare_libarchive output)
    set(patch "${PROJECT_SOURCE_DIR}/cmake/patches/libarchive-mbedtls4.patch")
    ananarchiver_backend_patch_info(patch_hash patch_commit)
    set(ANANARCHIVER_LIBARCHIVE_PATCH_COMMIT "${patch_commit}" CACHE INTERNAL "Backend patch commit" FORCE)
    if(patch_hash)
        string(SUBSTRING "${patch_hash}" 0 12 patch_id)
    else()
        set(patch_id upstream)
    endif()
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
        if(patch_hash)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" apply --check "${patch}"
                COMMAND_ERROR_IS_FATAL ANY)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" apply "${patch}"
                COMMAND_ERROR_IS_FATAL ANY)
        endif()
        file(WRITE "${source}/.prepared" "${ANANARCHIVER_LIBARCHIVE_REVISION}\n${patch_hash}\n")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    set(ANANARCHIVER_LIBARCHIVE_PATCH_SHA256 "${patch_hash}" CACHE INTERNAL "Applied backend compatibility patch" FORCE)
    set(${output} "${source}" PARENT_SCOPE)
endfunction()
