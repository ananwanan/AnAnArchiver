include_guard(GLOBAL)

# Keep the resolved Qt checkout clean. The private export is identified by both
# the resolved module manifest and the precise local loader patch applied to it.
function(ananarchiver_prepare_qt output)
    set(upstream "${PROJECT_SOURCE_DIR}/third_party/qt")
    if(NOT WIN32)
        set(${output} "${upstream}" PARENT_SCOPE)
        set(ANANARCHIVER_QT_PATCH_SHA256 "" PARENT_SCOPE)
        set(ANANARCHIVER_QTBASE_REVISION "" PARENT_SCOPE)
        return()
    endif()
    set(patch "${PROJECT_SOURCE_DIR}/cmake/patches/qt-windows-longpaths.patch")
    file(SHA256 "${patch}" patch_hash)
    file(READ "${ANANARCHIVER_BUILD_SOURCE_MANIFEST}" manifest)
    string(JSON count LENGTH "${manifest}" dependencies)
    set(qt_manifest "[]")
    set(qt_count 0)
    set(base_revision "")
    math(EXPR last "${count} - 1")
    foreach(index RANGE 0 ${last})
        string(JSON entry GET "${manifest}" dependencies ${index})
        string(JSON path GET "${entry}" path)
        if(path MATCHES "^third_party/qt(/|$)")
            string(JSON qt_manifest SET "${qt_manifest}" ${qt_count} "${entry}")
            math(EXPR qt_count "${qt_count} + 1")
            if(path STREQUAL "third_party/qt/qtbase")
                string(JSON base_revision GET "${entry}" revision)
            endif()
        endif()
    endforeach()
    if(NOT base_revision)
        message(FATAL_ERROR "The source Qt build needs its coupled qtbase revision in provenance")
    endif()
    set(patch_paths src/corelib/plugin/qlibrary_win.cpp src/plugins/platforms/windows/qwindowswindow.cpp)
    set(needed_patch_options "")
    foreach(path IN LISTS patch_paths)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${upstream}/qtbase"
            apply --reverse --check "--include=${path}" "${patch}" RESULT_VARIABLE already_fixed
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT already_fixed EQUAL 0)
            list(APPEND needed_patch_options "--include=${path}")
        endif()
    endforeach()
    if(NOT needed_patch_options)
        # A future latest release may include this fix; do not reapply it.
        set(${output} "${upstream}" PARENT_SCOPE)
        set(ANANARCHIVER_QT_PATCH_SHA256 "" PARENT_SCOPE)
        set(ANANARCHIVER_QTBASE_REVISION "${base_revision}" PARENT_SCOPE)
        return()
    endif()
    string(SHA256 source_id "${qt_manifest}\n${patch_hash}")
    string(SUBSTRING "${source_id}" 0 16 source_id)
    set(source "${CMAKE_BINARY_DIR}/dependencies/sources/qt-${source_id}")
    if(NOT EXISTS "${source}/.prepared")
        file(MAKE_DIRECTORY "${source}")
        # Every child module is already coupled/verified by the resolver. Avoid
        # bringing Git administration files into the generated source tree.
        file(COPY "${upstream}/" DESTINATION "${source}" PATTERN ".git" EXCLUDE)
        # Preserve each module's immutable source revision for Qt's own SBOM.
        # Private repositories share only read-only upstream objects; their HEAD,
        # index and patched worktree stay inside this generated source directory.
        math(EXPR qt_last "${qt_count} - 1")
        foreach(index RANGE 0 ${qt_last})
            string(JSON entry GET "${qt_manifest}" ${index})
            string(JSON path GET "${entry}" path)
            string(JSON revision GET "${entry}" revision)
            string(JSON url GET "${entry}" url)
            string(REGEX REPLACE "^third_party/qt/?" "" module_path "${path}")
            set(module_source "${source}/${module_path}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}/${path}"
                rev-parse --path-format=absolute --git-common-dir
                OUTPUT_VARIABLE git_directory OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
            execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${module_source}"
                COMMAND_ERROR_IS_FATAL ANY)
            # Git's alternates parser treats a CR as part of a directory name.
            # Avoid host-specific line ending conversion for this single path.
            string(STRIP "${git_directory}" git_directory)
            file(WRITE "${module_source}/.git/objects/info/alternates" "${git_directory}/objects")
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${module_source}"
                update-ref HEAD "${revision}" COMMAND_ERROR_IS_FATAL ANY)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${module_source}"
                config remote.origin.url "${url}" COMMAND_ERROR_IS_FATAL ANY)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${module_source}"
                reset --mixed --quiet HEAD COMMAND_ERROR_IS_FATAL ANY)
        endforeach()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}/qtbase"
            apply --check ${needed_patch_options} "${patch}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}/qtbase"
            apply ${needed_patch_options} "${patch}" COMMAND_ERROR_IS_FATAL ANY)
        file(WRITE "${source}/.prepared" "${base_revision}\n${patch_hash}\n")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    set(ANANARCHIVER_QT_PATCH_SHA256 "${patch_hash}" PARENT_SCOPE)
    set(ANANARCHIVER_QTBASE_REVISION "${base_revision}" PARENT_SCOPE)
    set(${output} "${source}" PARENT_SCOPE)
endfunction()
