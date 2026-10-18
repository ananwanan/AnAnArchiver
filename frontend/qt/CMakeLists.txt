include(GNUInstallDirs)
option(ANANARCHIVER_DEPLOY_RUNTIME "Deploy the selected Qt kit, QML imports and plugins at install time" OFF)

add_library(ananarchiver_qt STATIC
    app/app.cpp app/app.h
    app/build_info.cpp app/build_info.h
    controllers/archive_controller.cpp controllers/archive_controller.h
    controllers/compression_controller.cpp controllers/compression_controller.h
    controllers/desktop_controller.cpp controllers/desktop_controller.h
    controllers/task_controller.cpp controllers/task_controller.h
    controllers/drag_controller.cpp controllers/drag_controller.h
    models/archive_entry_model.cpp models/archive_entry_model.h
    models/archive_folder_model.cpp models/archive_folder_model.h
    models/file_tree_model.cpp models/file_tree_model.h
    platform/path_conversion.cpp platform/path_conversion.h
    platform/packaged_runtime.cpp platform/packaged_runtime.h
    platform/frontend_translator.cpp platform/frontend_translator.h
    platform/temp_file_manager.cpp platform/temp_file_manager.h)
target_include_directories(ananarchiver_qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
ananarchiver_project_options(ananarchiver_qt)
target_link_libraries(ananarchiver_qt PUBLIC
    ananarchiver_application Qt6::Core Qt6::Gui Qt6::Qml)
set_target_properties(ananarchiver_qt PROPERTIES AUTOMOC ON)

set(ananarchiver_timestamp_header "${CMAKE_CURRENT_BINARY_DIR}/generated/ananarchiver_build_timestamp.h")
add_custom_target(ananarchiver_build_timestamp
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT_FILE=${ananarchiver_timestamp_header}"
        -P "${PROJECT_SOURCE_DIR}/cmake/WriteBuildTimestamp.cmake"
    BYPRODUCTS "${ananarchiver_timestamp_header}"
    COMMENT "Recording application build time (UTC)"
    VERBATIM)
add_dependencies(ananarchiver_qt ananarchiver_build_timestamp)
target_include_directories(ananarchiver_qt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Embed the verified sources selected by this configuration. The final build
# provenance is recorded after linking, so use its prepared input records here.
set(ananarchiver_dependency_header "${CMAKE_CURRENT_BINARY_DIR}/generated/ananarchiver_dependency_info.h")
add_custom_target(ananarchiver_frontend_build_info
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/write_frontend_build_info.py"
        --build-dir "${CMAKE_BINARY_DIR}" --output "${ananarchiver_dependency_header}"
    BYPRODUCTS "${ananarchiver_dependency_header}"
    COMMENT "Embedding verified dependency versions and revisions"
    VERBATIM)
add_dependencies(ananarchiver_frontend_build_info ananarchiver_verify_dependencies)
add_dependencies(ananarchiver_qt ananarchiver_frontend_build_info)
set_source_files_properties(app/build_info.cpp PROPERTIES
    OBJECT_DEPENDS "${ananarchiver_timestamp_header};${ananarchiver_dependency_header}")

qt_add_executable(AnAnArchiver main.cpp)
ananarchiver_project_options(AnAnArchiver)
target_compile_definitions(AnAnArchiver PRIVATE ANANARCHIVER_PROJECT_VERSION="${PROJECT_VERSION}")
if(WIN32)
    enable_language(RC)
    target_sources(AnAnArchiver PRIVATE resources/app_icon.rc)
    if(MINGW)
        target_sources(AnAnArchiver PRIVATE resources/windows.rc)
    else()
        target_sources(AnAnArchiver PRIVATE resources/AnAnArchiver.manifest)
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
set_source_files_properties(qml/dialogs/OpenSourceDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/OpenSourceDialog.qml)
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
        qml/dialogs/OpenSourceDialog.qml
        qml/pages/WelcomePage.qml
        qml/components/AppIcon.qml
        qml/components/SortHeader.qml)
set_target_properties(AnAnArchiver PROPERTIES
    OUTPUT_NAME LiteArc
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME LiteArc
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ananwanan.AnAnArchiver
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE)
if(APPLE)
    target_sources(AnAnArchiver PRIVATE resources/icons/ananarchiver.icns)
    set_source_files_properties(resources/icons/ananarchiver.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(AnAnArchiver PROPERTIES
        MACOSX_BUNDLE_ICON_FILE ananarchiver.icns
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in")
elseif(UNIX AND NOT ANDROID)
    # Keep the installed executable relocatable, including multiarch lib layouts.
    set_target_properties(AnAnArchiver PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
if(ANDROID)
    set_target_properties(AnAnArchiver PROPERTIES
        QT_ANDROID_PACKAGE_NAME com.ananwanan.ananarchiver
        QT_ANDROID_APP_NAME LiteArc
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_COMPILE_SDK_VERSION 36
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE 10000)
endif()

file(GLOB ANANARCHIVER_ICON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/icons/*.svg")
qt_add_resources(AnAnArchiver "desktop_icons"
    PREFIX "/qt/qml/AnAnArchiver"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES ${ANANARCHIVER_ICON_FILES} qml/icons/LICENSE.txt)

qt_add_resources(AnAnArchiver "app_branding"
    PREFIX "/branding"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES resources/icons/ananarchiver.png)

install(TARGETS AnAnArchiver
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE AND NOT ANDROID)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/com.ananwanan.AnAnArchiver.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icons/ananarchiver.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
        RENAME com.ananwanan.AnAnArchiver.png)
endif()

if(ANANARCHIVER_DEPLOY_RUNTIME AND NOT ANDROID)
    # Qt's own deployment API discovers this target's QML imports and uses the
    # selected kit's deployment tools. Replace Qt's PATH-based fallback cache
    # with a tool resolved exclusively inside this kit (important for MSVC vs
    # MinGW installations on the same host).
    if(APPLE)
        find_program(ananarchiver_macdeployqt NAMES macdeployqt
            PATHS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" NO_DEFAULT_PATH NO_CACHE REQUIRED)
        set(MACDEPLOYQT_EXECUTABLE "${ananarchiver_macdeployqt}" CACHE FILEPATH "Selected Qt kit deployment tool" FORCE)
    elseif(WIN32)
        find_program(ananarchiver_windeployqt NAMES windeployqt
            PATHS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" NO_DEFAULT_PATH NO_CACHE REQUIRED)
        set(WINDEPLOYQT_EXECUTABLE "${ananarchiver_windeployqt}" CACHE FILEPATH "Selected Qt kit deployment tool" FORCE)
    endif()
    set(ananarchiver_deploy_plugin_options "")
    if(NOT APPLE)
        list(APPEND ananarchiver_deploy_plugin_options INCLUDE_PLUGINS qsvg qsvgicon)
        if(UNIX)
            # Linux package verification uses a headless startup smoke test;
            # Gui's default QPA list need not include this testing platform.
            list(APPEND ananarchiver_deploy_plugin_options qoffscreen)
        endif()
        # Avoid deriving package subdirectories from a distribution Qt kit's
        # lib/qt6 or multiarch paths. The generated qt.conf records this layout.
        install(CODE "set(QT_DEPLOY_PLUGINS_DIR plugins)\nset(QT_DEPLOY_QML_DIR qml)\nset(QT_DEPLOY_TRANSLATIONS_DIR translations)")
    endif()
    qt6_generate_deploy_qml_app_script(TARGET AnAnArchiver
        OUTPUT_SCRIPT ananarchiver_deploy_script
        NO_TRANSLATIONS
        ${ananarchiver_deploy_plugin_options})
    install(SCRIPT ${ananarchiver_deploy_script})
endif()

# Packaging scripts consume metadata from this exact configuration rather than
# selecting a different Qt installation or guessing a version from a filename.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/packaging-metadata.txt"
    CONTENT "version=${PROJECT_VERSION}\nqt_version=${Qt6_VERSION}\nqt_prefix=${QT6_INSTALL_PREFIX}\nsource=${PROJECT_SOURCE_DIR}\n")

if(BUILD_TESTING)
    if(WIN32)
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
    add_executable(ananarchiver_qt_shell_tests tests/qt_shell_tests.cpp)
    ananarchiver_project_options(ananarchiver_qt_shell_tests)
    target_link_libraries(ananarchiver_qt_shell_tests PRIVATE
        ananarchiver_qt AnAnArchiver::Archive Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(ananarchiver_qt_shell_tests PRIVATE
        ANANARCHIVER_TEST_MAIN_QML="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
    add_test(NAME qt_shell COMMAND ananarchiver_qt_shell_tests)
    set_tests_properties(qt_shell PROPERTIES
        TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
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
    qt_add_resources(ananarchiver_qt_desktop_flow_tests "flow_app_branding"
        PREFIX "/branding"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        FILES resources/icons/ananarchiver.png)
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
        set_tests_properties(qt_browser qt_desktop qt_drag qt_desktop_flow qt_adapters qt_home_smoke qt_filename_rendering qt_extraction qt_compression qt_shell PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
