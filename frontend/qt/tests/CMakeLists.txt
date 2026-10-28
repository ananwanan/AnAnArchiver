get_filename_component(ananarchiver_qt_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

function(ananarchiver_add_qt_test target name source timeout)
    cmake_parse_arguments(PARSE_ARGV 4 test "QML;ARCHIVE;SOFTWARE" "" "")
    add_executable(${target} "${source}")
    ananarchiver_project_options(${target})
    target_link_libraries(${target} PRIVATE ananarchiver_qt)
    if(test_ARCHIVE)
        target_link_libraries(${target} PRIVATE AnAnArchiver::Archive)
    endif()
    if(test_QML)
        target_link_libraries(${target} PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
        target_compile_definitions(${target} PRIVATE
            ANANARCHIVER_TEST_MAIN_QML="${ananarchiver_qt_source_dir}/qml/Main.qml")
    endif()
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
    if(test_QML OR test_SOFTWARE)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
    if(WIN32)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

ananarchiver_add_qt_test(ananarchiver_qt_shell_tests qt_shell qt_shell_tests.cpp 90 QML ARCHIVE)
ananarchiver_add_qt_test(ananarchiver_qt_browser_tests qt_browser qt_browser_tests.cpp 60 ARCHIVE)
ananarchiver_add_qt_test(ananarchiver_qt_desktop_tests qt_desktop qt_desktop_tests.cpp 30)
ananarchiver_add_qt_test(ananarchiver_qt_drag_tests qt_drag qt_drag_tests.cpp 90 ARCHIVE SOFTWARE)
ananarchiver_add_qt_test(ananarchiver_qt_desktop_flow_tests qt_desktop_flow qt_desktop_flow_tests.cpp 90 QML ARCHIVE)
ananarchiver_add_qt_test(ananarchiver_qt_tests qt_adapters qt_adapter_tests.cpp 30)
ananarchiver_add_qt_test(ananarchiver_qt_rendering_tests qt_filename_rendering qt_filename_rendering_tests.cpp 20 QML)
ananarchiver_add_qt_test(ananarchiver_qt_extraction_tests qt_extraction qt_extraction_tests.cpp 60 QML ARCHIVE)
ananarchiver_add_qt_test(ananarchiver_qt_compression_tests qt_compression qt_compression_tests.cpp 90 QML)

target_link_libraries(ananarchiver_qt_rendering_tests PRIVATE Qt6::Network)
qt_add_resources(ananarchiver_qt_desktop_flow_tests "flow_app_branding"
    PREFIX "/branding"
    BASE "${ananarchiver_qt_source_dir}/resources/icons"
    FILES "${ananarchiver_qt_source_dir}/resources/icons/ananarchiver.png")

add_test(NAME qt_home_smoke COMMAND AnAnArchiver --smoke-test 500)
set_tests_properties(qt_home_smoke PROPERTIES
    TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
if(WIN32)
    set_tests_properties(qt_home_smoke PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    find_program(ananarchiver_pwsh NAMES pwsh NO_CACHE)
    if(ananarchiver_pwsh)
        add_test(NAME package_regression_coverage
            COMMAND "${ananarchiver_pwsh}" -NoProfile -File
                "${PROJECT_SOURCE_DIR}/tests/integration/Test-PackageCoverage.ps1"
                -BuildDirectory "${CMAKE_BINARY_DIR}")
        set_tests_properties(package_regression_coverage PROPERTIES TIMEOUT 30)
    endif()
endif()
add_test(NAME platform_packaging_metadata COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/packaging/linux/test_metadata.py")
set_tests_properties(platform_packaging_metadata PROPERTIES TIMEOUT 30)
