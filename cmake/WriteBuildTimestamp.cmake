if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

# Run at build time, rather than configure time, so incremental builds report
# the timestamp of the binary they produce. CMake honors SOURCE_DATE_EPOCH for
# reproducible builds.
string(TIMESTAMP build_timestamp "%Y-%m-%dT%H:%M:%SZ" UTC)
set(header_content "#pragma once\n#define ANANARCHIVER_BUILD_TIMESTAMP_UTC \"${build_timestamp}\"\n")
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" previous_content)
    if(previous_content STREQUAL header_content)
        return()
    endif()
endif()
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${header_content}")
