cmake_minimum_required(VERSION 3.25)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT ANANARCHIVER_BUILD_DIRECTORY)
    set(ANANARCHIVER_BUILD_DIRECTORY "${project_root}/out/build/dependencies")
endif()
if(NOT ANANARCHIVER_BUILD_TYPE)
    set(ANANARCHIVER_BUILD_TYPE Release)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/init-dependencies.cmake")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${project_root}" -B "${ANANARCHIVER_BUILD_DIRECTORY}" -G Ninja
    "-DCMAKE_BUILD_TYPE=${ANANARCHIVER_BUILD_TYPE}"
    "-DANANARCHIVER_BUILD_QT=${ANANARCHIVER_INIT_QT}"
    -DANANARCHIVER_SUPERBUILD=ON COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${ANANARCHIVER_BUILD_DIRECTORY}" --target dependencies
    COMMAND_ERROR_IS_FATAL ANY)
