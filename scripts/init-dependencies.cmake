cmake_minimum_required(VERSION 3.25)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_program(GIT_EXECUTABLE git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${project_root}"
    submodule update --init --depth 1
    third_party/libarchive third_party/zlib third_party/bzip2 third_party/xz
    third_party/zstd third_party/lz4 third_party/expat third_party/mbedtls
    WORKING_DIRECTORY "${project_root}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${project_root}/third_party/mbedtls"
    submodule update --init --depth 1 framework COMMAND_ERROR_IS_FATAL ANY)
if(ANANARCHIVER_INIT_QT)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${project_root}"
        submodule update --init --depth 1 third_party/qt third_party/vulkan-headers
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${project_root}/third_party/qt"
        submodule update --init --depth 1 qtbase qtshadertools qtdeclarative qtsvg
        COMMAND_ERROR_IS_FATAL ANY)
endif()
