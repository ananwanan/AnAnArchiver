include_guard(GLOBAL)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(ANANARCHIVER_DEPENDENCY_MANIFEST "${PROJECT_SOURCE_DIR}/out/dependency-resolution.json"
    CACHE FILEPATH "Resolved dependency snapshot produced by init-dependencies.cmake")

function(ananarchiver_json_quote output value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    set(${output} "\"${value}\"" PARENT_SCOPE)
endfunction()

function(ananarchiver_configure_traceability)
    set(source_qt false)
    if(ANANARCHIVER_BUILD_QT AND NOT ANANARCHIVER_USE_SYSTEM_QT)
        set(source_qt true)
    endif()
    set(system_backend false)
    if(ANANARCHIVER_USE_SYSTEM_LIBARCHIVE)
        set(system_backend true)
    endif()
    set(superbuild false)
    if(ANANARCHIVER_SUPERBUILD)
        set(superbuild true)
    endif()
    foreach(variable IN ITEMS PROJECT_SOURCE_DIR CMAKE_BINARY_DIR ANANARCHIVER_DEPENDENCY_PREFIX
            ANANARCHIVER_DEPENDENCY_MANIFEST PROJECT_VERSION CMAKE_VERSION CMAKE_GENERATOR
            CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION
            CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR CMAKE_OSX_ARCHITECTURES ANDROID_ABI
            CMAKE_BUILD_TYPE CMAKE_C_FLAGS CMAKE_CXX_FLAGS ANANARCHIVER_QT_BUILD_DIRECTORY
            CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
            CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_MSVC_RUNTIME_LIBRARY)
        ananarchiver_json_quote(json_${variable} "${${variable}}")
    endforeach()
    set(external_libraries "[]")
    if(ANANARCHIVER_USE_SYSTEM_LIBARCHIVE AND NOT ANANARCHIVER_SUPERBUILD)
        ananarchiver_json_quote(version "${LibArchive_VERSION}")
        ananarchiver_json_quote(location "${LibArchive_LIBRARIES}")
        string(JSON external_libraries SET "${external_libraries}" 0
            "{\"name\":\"libarchive\",\"selection\":\"system\",\"version\":${version},\"location\":${location},\"revision\":null}")
    endif()
    if(ANANARCHIVER_BUILD_QT AND ANANARCHIVER_USE_SYSTEM_QT AND NOT ANANARCHIVER_SUPERBUILD)
        ananarchiver_json_quote(version "${Qt6_VERSION}")
        ananarchiver_json_quote(location "${Qt6_DIR}")
        string(JSON length LENGTH "${external_libraries}")
        string(JSON external_libraries SET "${external_libraries}" ${length}
            "{\"name\":\"Qt\",\"selection\":\"developer-kit\",\"version\":${version},\"location\":${location},\"revision\":null}")
    endif()
    set(trace_config "${CMAKE_BINARY_DIR}/build-trace-config.json")
    file(WRITE "${trace_config}" "{
  \"root\":${json_PROJECT_SOURCE_DIR}, \"binary\":${json_CMAKE_BINARY_DIR},
  \"prefix\":${json_ANANARCHIVER_DEPENDENCY_PREFIX}, \"manifest\":${json_ANANARCHIVER_DEPENDENCY_MANIFEST},
  \"qt_build_binary\":${json_ANANARCHIVER_QT_BUILD_DIRECTORY},
  \"superbuild\":${superbuild}, \"source_qt\":${source_qt}, \"system_backend\":${system_backend},
  \"application_version\":${json_PROJECT_VERSION}, \"cmake_version\":${json_CMAKE_VERSION},
  \"generator\":${json_CMAKE_GENERATOR}, \"external_libraries\":${external_libraries},
  \"toolchain\":{
    \"c_compiler\":{\"id\":${json_CMAKE_C_COMPILER_ID},\"version\":${json_CMAKE_C_COMPILER_VERSION}},
    \"cxx_compiler\":{\"id\":${json_CMAKE_CXX_COMPILER_ID},\"version\":${json_CMAKE_CXX_COMPILER_VERSION}},
    \"system\":${json_CMAKE_SYSTEM_NAME}, \"processor\":${json_CMAKE_SYSTEM_PROCESSOR},
    \"osx_architectures\":${json_CMAKE_OSX_ARCHITECTURES}, \"android_abi\":${json_ANDROID_ABI},
    \"build_type\":${json_CMAKE_BUILD_TYPE}, \"c_flags\":${json_CMAKE_C_FLAGS}, \"cxx_flags\":${json_CMAKE_CXX_FLAGS},
    \"c_flags_release\":${json_CMAKE_C_FLAGS_RELEASE}, \"cxx_flags_release\":${json_CMAKE_CXX_FLAGS_RELEASE},
    \"c_flags_debug\":${json_CMAKE_C_FLAGS_DEBUG}, \"cxx_flags_debug\":${json_CMAKE_CXX_FLAGS_DEBUG},
    \"c_flags_relwithdebinfo\":${json_CMAKE_C_FLAGS_RELWITHDEBINFO}, \"cxx_flags_relwithdebinfo\":${json_CMAKE_CXX_FLAGS_RELWITHDEBINFO},
    \"c_flags_minsizerel\":${json_CMAKE_C_FLAGS_MINSIZEREL}, \"cxx_flags_minsizerel\":${json_CMAKE_CXX_FLAGS_MINSIZEREL},
    \"msvc_runtime\":${json_CMAKE_MSVC_RUNTIME_LIBRARY}
  }
}\n")
    set(trace_command "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build_traceability.py"
        --config "${trace_config}")
    execute_process(COMMAND ${trace_command} --operation prepare COMMAND_ERROR_IS_FATAL ANY)
    add_custom_target(ananarchiver_verify_dependencies
        COMMAND ${trace_command} --operation verify VERBATIM)
    add_custom_target(ananarchiver_build_info ALL
        COMMAND ${trace_command} --operation record
        DEPENDS ananarchiver_verify_dependencies VERBATIM)
    set(ANANARCHIVER_TRACE_COMMAND "${trace_command}" PARENT_SCOPE)
    install(FILES "${CMAKE_BINARY_DIR}/build-info.json" DESTINATION share/ananarchiver)
endfunction()
