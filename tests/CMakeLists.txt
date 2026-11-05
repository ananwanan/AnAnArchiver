add_executable(ananarchiver_core_tests core/core_tests.cpp)
target_link_libraries(ananarchiver_core_tests PRIVATE ananarchiver_core)
ananarchiver_project_options(ananarchiver_core_tests)
add_test(NAME core COMMAND ananarchiver_core_tests)

add_executable(ananarchiver_archive_tests archive/archive_reader_tests.cpp)
target_link_libraries(ananarchiver_archive_tests PRIVATE ananarchiver_libarchive AnAnArchiver::Archive)
ananarchiver_project_options(ananarchiver_archive_tests)
add_test(NAME archive COMMAND ananarchiver_archive_tests)

add_executable(ananarchiver_extraction_tests archive/extraction_tests.cpp)
target_link_libraries(ananarchiver_extraction_tests PRIVATE ananarchiver_libarchive AnAnArchiver::Archive)
ananarchiver_project_options(ananarchiver_extraction_tests)
add_test(NAME extraction COMMAND ananarchiver_extraction_tests)

add_executable(ananarchiver_compression_tests archive/compression_tests.cpp)
target_link_libraries(ananarchiver_compression_tests PRIVATE ananarchiver_libarchive AnAnArchiver::Archive)
ananarchiver_project_options(ananarchiver_compression_tests)
add_test(NAME compression COMMAND ananarchiver_compression_tests)

add_executable(ananarchiver_application_tests application/application_tests.cpp)
target_link_libraries(ananarchiver_application_tests PRIVATE ananarchiver_application)
ananarchiver_project_options(ananarchiver_application_tests)
add_test(NAME application COMMAND ananarchiver_application_tests)
add_executable(ananarchiver_metadata_tests application/metadata_tests.cpp)
target_link_libraries(ananarchiver_metadata_tests PRIVATE ananarchiver_application)
ananarchiver_project_options(ananarchiver_metadata_tests)
add_test(NAME metadata COMMAND ananarchiver_metadata_tests)
set_tests_properties(metadata PROPERTIES TIMEOUT 60)
set_tests_properties(core archive extraction application PROPERTIES TIMEOUT 30)
set_tests_properties(compression PROPERTIES TIMEOUT 90)

if(ANANARCHIVER_BUILD_C_API)
    # Compile this consumer as C and link only the public shared ABI.
    add_executable(ananarchiver_c_consumer c_api/c_consumer.c)
    target_link_libraries(ananarchiver_c_consumer PRIVATE ananarchiver_c_api)
    set_target_properties(ananarchiver_c_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    if(MSVC)
        target_compile_options(ananarchiver_c_consumer PRIVATE /W4 /utf-8)
    else()
        target_compile_options(ananarchiver_c_consumer PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_executable(ananarchiver_c_api_tests c_api/c_api_tests.cpp)
    target_link_libraries(ananarchiver_c_api_tests PRIVATE
        ananarchiver_c_api ananarchiver_core AnAnArchiver::Archive)
    ananarchiver_project_options(ananarchiver_c_api_tests)
    add_executable(ananarchiver_browser_c_api_tests c_api/browser_c_api_tests.cpp)
    target_link_libraries(ananarchiver_browser_c_api_tests PRIVATE
        ananarchiver_c_api ananarchiver_core AnAnArchiver::Archive)
    ananarchiver_project_options(ananarchiver_browser_c_api_tests)
    if(WIN32)
        # Windows does not search the sibling library directory automatically.
        # Refresh on every dependent build, including DLL-only changes that do
        # not relink a consumer or change the import library.
        add_custom_target(ananarchiver_c_api_test_runtime
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:ananarchiver_c_api>"
                "$<TARGET_FILE_DIR:ananarchiver_c_consumer>"
            DEPENDS ananarchiver_c_api VERBATIM)
        add_dependencies(ananarchiver_c_consumer ananarchiver_c_api_test_runtime)
        add_dependencies(ananarchiver_c_api_tests ananarchiver_c_api_test_runtime)
        add_dependencies(ananarchiver_browser_c_api_tests ananarchiver_c_api_test_runtime)
    endif()
    add_test(NAME c_api_consumer COMMAND ananarchiver_c_consumer)
    add_test(NAME c_api COMMAND ananarchiver_c_api_tests)
    add_test(NAME browser_c_api COMMAND ananarchiver_browser_c_api_tests)
    set_tests_properties(browser_c_api PROPERTIES TIMEOUT 60)
    set_tests_properties(c_api_consumer c_api PROPERTIES TIMEOUT 60)
endif()
