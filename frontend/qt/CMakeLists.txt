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
    controllers/storage_access_controller.cpp controllers/storage_access_controller.h
    models/archive_entry_model.cpp models/archive_entry_model.h
    models/archive_folder_model.cpp models/archive_folder_model.h
    models/file_tree_model.cpp models/file_tree_model.h
    platform/archive_error_details.cpp platform/archive_error_details.h
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
set(ananarchiver_qml_files
    qml/Main.qml
    qml/pages/HomePage.qml
    qml/pages/WelcomePage.qml
    qml/components/AppIcon.qml
    qml/components/FileTable.qml
    qml/components/FocusScrollView.qml
    qml/components/PlainToolTip.qml
    qml/components/SortHeader.qml
    qml/components/StatusBar.qml
    qml/components/Toolbar.qml
    qml/dialogs/CompressionDialog.qml
    qml/dialogs/ErrorDetailsDialog.qml
    qml/dialogs/OpenSourceDialog.qml
    qml/dialogs/StorageAccessDialog.qml
    qml/dialogs/TaskCenter.qml)
foreach(qml_file IN LISTS ananarchiver_qml_files)
    string(REGEX REPLACE "^qml/" "" qml_alias "${qml_file}")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
qt_add_qml_module(AnAnArchiver
    URI AnAnArchiver
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${ananarchiver_qml_files})
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
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
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
    add_subdirectory(tests)
endif()
