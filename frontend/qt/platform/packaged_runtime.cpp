#include "packaged_runtime.h"

#ifdef _WIN32
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
std::wstring nativePath(const std::filesystem::path& path) {
    auto preferred = path;
    preferred.make_preferred();
    auto value = preferred.native();
    if (value.starts_with(L"\\\\?\\")) return value;
    if (value.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
}
bool fileExists(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(nativePath(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(512);
    while (buffer.size() <= 32768) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size()) return std::filesystem::path{std::wstring(buffer.data(), length)}.parent_path();
        buffer.resize(buffer.size() * 2);
    }
    return {};
}
}
#endif

namespace ananarchiver::qt {
bool configurePackagedRuntime() {
#ifdef _WIN32
    const auto directory = executableDirectory();
    if (directory.empty()) { std::cerr << "Could not locate the application executable\n"; return false; }
    if (!fileExists(directory / L"qt.conf") || !fileExists(directory / L"Qt6Core.dll")) return true;
    auto plugins = directory / L"plugins";
    if (!fileExists(plugins / L"platforms/qwindows.dll")) {
        plugins = directory.parent_path() / L"plugins";
        if (!fileExists(plugins / L"platforms/qwindows.dll")) return true;
    }
    // All required DLLs are app-local or system libraries. Do not search a
    // developer's PATH/CWD for an incompatible Qt or a dependency of a plugin.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        std::cerr << "Could not configure the packaged DLL search directories\n";
        return false;
    }
    for (const auto* name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH",
                             "QML2_IMPORT_PATH", "QTDIR", "QT_STYLE_OVERRIDE"}) qunsetenv(name);
    qputenv("QT_QPA_PLATFORM", "windows");
    if (directory.native().size() + 16 >= 248) {
        // Some graphics drivers reject a long executable path. Use Qt's
        // software rendering for this 2D UI in that deployment. The paired Qt
        // platform patch avoids the GL-only screen probe for raster windows.
        qputenv("QT_QUICK_BACKEND", "software");
    }
    QCoreApplication::setLibraryPaths({QDir::fromNativeSeparators(QString::fromStdWString(nativePath(plugins)))});
    const auto qml = plugins.parent_path() / L"qml";
    if (fileExists(qml / L"QtQuick/qmldir")) {
        // Initialize with the package's imports even if Qt's library-info cache
        // was queried before QGuiApplication had an application directory.
        qputenv("QML_IMPORT_PATH", QDir::fromNativeSeparators(QString::fromStdWString(nativePath(qml))).toUtf8());
    }
#endif
    return true;
}
}
