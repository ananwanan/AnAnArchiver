add_library(ananarchiver_qt STATIC
    app/app.cpp app/app.h
    controllers/archive_controller.cpp controllers/archive_controller.h
    controllers/compression_controller.cpp controllers/compression_controller.h
    models/archive_entry_model.cpp models/archive_entry_model.h
    models/file_tree_model.cpp models/file_tree_model.h
    platform/path_conversion.cpp platform/path_conversion.h
    platform/frontend_translator.cpp platform/frontend_translator.h)
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
        qml/dialogs/CompressionDialog.qml)
set_target_properties(AnAnArchiver PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ananwanan.AnAnArchiver
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE)

install(TARGETS AnAnArchiver
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)

if(BUILD_TESTING)
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
        set_tests_properties(qt_adapters qt_home_smoke qt_filename_rendering qt_extraction qt_compression PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
