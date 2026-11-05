# Both desktop CMake runners call the repository's existing build pipeline.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# flutter/CMakeLists reads this in its own directory scope. Import the same
# selected SDK into the runner scope rather than relying on a PATH lookup.
function(litearc_selected_flutter_root output)
  include("${FLUTTER_MANAGED_DIR}/ephemeral/generated_config.cmake")
  set(${output} "${FLUTTER_ROOT}" PARENT_SCOPE)
endfunction()
litearc_selected_flutter_root(litearc_flutter_root)
add_custom_target(litearc_native ALL
  COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_LIST_DIR}/build_native.py"
    --output "${CMAKE_CURRENT_BINARY_DIR}/native-stage"
    --architecture "${CMAKE_SYSTEM_PROCESSOR}"
    --flutter-root "${litearc_flutter_root}"
  VERBATIM)
add_dependencies(${BINARY_NAME} litearc_native)
if(WIN32)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-stage/"
    DESTINATION "${CMAKE_INSTALL_PREFIX}" COMPONENT Runtime)
else()
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-stage/"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" COMPONENT Runtime)
endif()
