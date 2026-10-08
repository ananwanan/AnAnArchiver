add_library(ananarchiver_qt STATIC
    app/app.cpp app/app.h
    controllers/archive_controller.cpp controllers/archive_controller.h
    controllers/compression_controller.cpp controllers/compression_controller.h
    controllers/desktop_controller.cpp controllers/desktop_controller.h
    controllers/task_controller.cpp controllers/task_controller.h
    controllers/drag_controller.cpp controllers/drag_controller.h
    models/archive_entry_model.cpp models/archive_entry_model.h
    models/file_tree_model.cpp models/file_tree_model.h
    platform/path_conversion.cpp platform/path_conversion.h
    platform/frontend_translator.cpp platform/frontend_translator.h
    platform/temp_file_manager.cpp platform/temp_file_manager.h)
target_include_directories(ananarchiver_qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
ananarchiver_project_options(ananarchiver_qt)
target_link_libraries(ananarchiver_qt PUBLIC
    ananarchiver_application Qt6::Core Qt6::Gui Qt6::Qml)
set_target_properties(ananarchiver_qt PROPERTIES AUTOMOC ON)

qt_add_executable(AnAnArchiver main.cpp)
ananarchiver_project_options(AnAnArchiver)
if(WIN32)
    target_sources(AnAnArchiver PRIVATE resources/AnAnArchiver.manifest)
    if(MINGW)
        enable_language(RC)
        target_sources(AnAnArchiver PRIVATE resources/windows.rc)
    endif()
endif()
target_link_libraries(AnAnArchiver PRIVATE
    ananarchiver_qt Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/pages/HomePage.qml PROPERTIES QT_RESOURCE_ALIAS pages/HomePage.qml)
set_source_files_properties(qml/components/Toolbar.qml PROPERTIES QT_RESOURCE_ALIAS components/Toolbar.qml)
set_source_files_properties(qml/components/FileTable.qml PROPERTIES QT_RESOURCE_ALIAS components/FileTable.qml)
set_source_files_properties(qml/components/StatusBar.qml PROPERTIES QT_RESOURCE_ALIAS components/StatusBar.qml)
set_source_files_properties(qml/components/PlainToolTip.qml PROPERTIES QT_RESOURCE_ALIAS components/PlainToolTip.qml)
set_source_files_properties(qml/dialogs/CompressionDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/CompressionDialog.qml)
set_source_files_properties(qml/dialogs/TaskCenter.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/TaskCenter.qml)
set_source_files_properties(qml/dialogs/ErrorDetailsDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/ErrorDetailsDialog.qml)
set_source_files_properties(qml/pages/WelcomePage.qml PROPERTIES QT_RESOURCE_ALIAS pages/WelcomePage.qml)
set_source_files_properties(qml/components/AppIcon.qml PROPERTIES QT_RESOURCE_ALIAS components/AppIcon.qml)
set_source_files_properties(qml/components/SortHeader.qml PROPERTIES QT_RESOURCE_ALIAS components/SortHeader.qml)
qt_add_qml_module(AnAnArchiver
    URI AnAnArchiver
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/pages/HomePage.qml
        qml/components/Toolbar.qml
        qml/components/FileTable.qml
        qml/components/StatusBar.qml
        qml/components/PlainToolTip.qml
        qml/dialogs/CompressionDialog.qml
        qml/dialogs/TaskCenter.qml
        qml/dialogs/ErrorDetailsDialog.qml
        qml/pages/WelcomePage.qml
        qml/components/AppIcon.qml
        qml/components/SortHeader.qml)
set_target_properties(AnAnArchiver PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ananwanan.AnAnArchiver
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE)

file(GLOB ANANARCHIVER_ICON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/icons/*.svg")
qt_add_resources(AnAnArchiver "desktop_icons"
    PREFIX "/qt/qml/AnAnArchiver"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES ${ANANARCHIVER_ICON_FILES} qml/icons/LICENSE.txt)

install(TARGETS AnAnArchiver
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_executable(ananarchiver_qt_browser_tests tests/qt_browser_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_browser_tests)
    target_link_libraries(ananarchiver_qt_browser_tests PRIVATE ananarchiver_qt AnAnArchiver::Archive)
    add_test(NAME qt_browser COMMAND ananarchiver_qt_browser_tests)
    set_tests_properties(qt_browser PROPERTIES TIMEOUT 60)
    add_executable(ananarchiver_qt_desktop_tests tests/qt_desktop_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_desktop_tests)
    target_link_libraries(ananarchiver_qt_desktop_tests PRIVATE ananarchiver_qt)
    add_test(NAME qt_desktop COMMAND ananarchiver_qt_desktop_tests)
    set_tests_properties(qt_desktop PROPERTIES TIMEOUT 30)
    add_executable(ananarchiver_qt_drag_tests tests/qt_drag_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_drag_tests)
    target_link_libraries(ananarchiver_qt_drag_tests PRIVATE ananarchiver_qt AnAnArchiver::Archive)
    add_test(NAME qt_drag COMMAND ananarchiver_qt_drag_tests)
    set_tests_properties(qt_drag PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ananarchiver_qt_desktop_flow_tests tests/qt_desktop_flow_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_desktop_flow_tests)
    target_link_libraries(ananarchiver_qt_desktop_flow_tests PRIVATE ananarchiver_qt AnAnArchiver::Archive Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(ananarchiver_qt_desktop_flow_tests PRIVATE ANANARCHIVER_TEST_MAIN_QML="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
    add_test(NAME qt_desktop_flow COMMAND ananarchiver_qt_desktop_flow_tests)
    set_tests_properties(qt_desktop_flow PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ananarchiver_qt_tests tests/qt_adapter_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_tests)
    target_link_libraries(ananarchiver_qt_tests PRIVATE ananarchiver_qt)
    add_test(NAME qt_adapters COMMAND ananarchiver_qt_tests)
    set_tests_properties(qt_adapters PROPERTIES TIMEOUT 30)
    add_test(NAME qt_home_smoke COMMAND AnAnArchiver --smoke-test 500)
    set_tests_properties(qt_home_smoke PROPERTIES
        TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ananarchiver_qt_rendering_tests tests/qt_filename_rendering_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_rendering_tests)
    target_link_libraries(ananarchiver_qt_rendering_tests PRIVATE
        ananarchiver_qt Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Network)
    target_compile_definitions(ananarchiver_qt_rendering_tests PRIVATE
        ANANARCHIVER_TEST_MAIN_QML="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
    add_test(NAME qt_filename_rendering COMMAND ananarchiver_qt_rendering_tests)
    set_tests_properties(qt_filename_rendering PROPERTIES
        TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ananarchiver_qt_extraction_tests tests/qt_extraction_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_extraction_tests)
    target_link_libraries(ananarchiver_qt_extraction_tests PRIVATE
        ananarchiver_qt AnAnArchiver::Archive Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(ananarchiver_qt_extraction_tests PRIVATE
        ANANARCHIVER_TEST_MAIN_QML="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
    add_test(NAME qt_extraction COMMAND ananarchiver_qt_extraction_tests)
    set_tests_properties(qt_extraction PROPERTIES
        TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ananarchiver_qt_compression_tests tests/qt_compression_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_compression_tests)
    target_link_libraries(ananarchiver_qt_compression_tests PRIVATE
        ananarchiver_qt Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(ananarchiver_qt_compression_tests PRIVATE
        ANANARCHIVER_TEST_MAIN_QML="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
    add_test(NAME qt_compression COMMAND ananarchiver_qt_compression_tests)
    set_tests_properties(qt_compression PROPERTIES
        TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(WIN32)
        set_tests_properties(qt_browser qt_desktop qt_drag qt_desktop_flow qt_adapters qt_home_smoke qt_filename_rendering qt_extraction qt_compression PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
