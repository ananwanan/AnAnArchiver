cmake_minimum_required(VERSION 3.16)

project(
  AnAnArchiver
  VERSION 1.0.0
  LANGUAGES CXX)

# 设置 C++ 标准为 23
set(CMAKE_CXX_STANDARD 23)
# 确保编译器如果不支持 C++23 就报错，而不是隐式降级到旧版本
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# （可选）禁用编译器特有扩展（如 gnu++23），强制使用标准 C++23
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

qt_add_executable(appAnAnArchiver src/main.cpp)

# 为这个库指定头文件目录
target_include_directories(appAnAnArchiver
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include   # 对外公开的头文件
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src       # 内部私有的头文件
)

qt_add_qml_module(
  appAnAnArchiver
  URI AnAnArchiver
  # UI
  QML_FILES ui/Main.qml
  QML_FILES ui/pages/HomePage.qml
  QML_FILES ui/components/Ribbon.qml
  QML_FILES ui/components/Toolbar.qml
  QML_FILES ui/components/FileTable.qml
  QML_FILES ui/components/StatusBar.qml
  QML_FILES ui/components/dialogs/AboutDialog.qml
  # Source
  SOURCES src/app/app.h src/app/app.cpp
  SOURCES src/context/app_context.h src/context/app_context.cpp
  SOURCES src/models/file_tree_item.h src/models/file_tree_item.cpp
  SOURCES src/models/file_tree_model.h src/models/file_tree_model.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1. If
# you are developing for iOS or macOS you should consider setting an explicit,
# fixed bundle identifier manually though.
set_target_properties(
  appAnAnArchiver
  PROPERTIES # MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appAnAnArchiver
             MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
             MACOSX_BUNDLE_SHORT_VERSION_STRING
             ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
             MACOSX_BUNDLE TRUE
             WIN32_EXECUTABLE TRUE)

target_link_libraries(appAnAnArchiver PRIVATE Qt6::Quick)
target_link_libraries(appAnAnArchiver PRIVATE Qt6::Widgets)

install(
  TARGETS appAnAnArchiver
  BUNDLE DESTINATION .
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
