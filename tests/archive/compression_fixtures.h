#pragma once

#include "ananarchiver/archive_error.h"
#include "ananarchiver/unicode.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace compression_tests {

using namespace ananarchiver;

inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline std::string describeErrorPath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::string value;
    try { value = pathToUtf8(path); }
    catch (const std::invalid_argument&) { return "; path=<invalid Unicode>"; }
    constexpr std::size_t maximum = 1024;
    if (value.size() > maximum) {
        value.resize(maximum);
        while (!isValidUtf8(value) && !value.empty()) value.pop_back();
        value += "...";
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\0') {
            value.replace(index, 1, "\\0");
            ++index;
        }
    }
    return "; path=" + value;
}

template <class T>
void succeeded(const Result<T>& result, const std::string& message) {
    check(result.has_value(), message + (result ? "" : ": " + result.error().message +
                                                       "; " + result.error().backendMessage +
                                                       describeErrorPath(result.error().path)));
}

inline void failedWith(const Result<void>& result, ArchiveErrorCode code,
                       const std::string& message) {
    check(!result, message + ": operation unexpectedly succeeded");
    check(result.error().code == code,
          message + ": " + result.error().message + "; " + result.error().backendMessage +
          describeErrorPath(result.error().path));
}

inline std::filesystem::path nativeFixturePath(std::filesystem::path path) {
#ifdef _WIN32
    // An extended absolute path must never pass through MinGW absolute(),
    // which can prepend the current drive/root again to a device pathname.
    if (path.native().starts_with(L"\\\\?\\")) return path;
    path = std::filesystem::absolute(path);
    path.make_preferred();
    const auto& absolute = path.native();
    if (absolute.starts_with(L"\\\\?\\")) return path;
    if (absolute.starts_with(L"\\\\"))
        return std::filesystem::path{L"\\\\?\\UNC\\" + absolute.substr(2)};
    return std::filesystem::path{L"\\\\?\\" + absolute};
#else
    return path;
#endif
}

struct FixtureTreeEntry {
    std::filesystem::path path;
    bool directory = false;
};

inline std::vector<FixtureTreeEntry> fixtureTreeEntries(const std::filesystem::path& root) {
    std::vector<FixtureTreeEntry> entries;
    const auto nativeRoot = nativeFixturePath(root);
#ifdef _WIN32
    // MinGW's recursive_directory_iterator can enumerate the current working
    // directory when its requested directory exceeds MAX_PATH. Use the native
    // extended-path API for independent expected-tree and payload verification.
    std::vector<std::filesystem::path> pending{nativeRoot};
    while (!pending.empty()) {
        auto directory = std::move(pending.back());
        pending.pop_back();
        WIN32_FIND_DATAW child{};
        struct FindHandle {
            HANDLE handle = INVALID_HANDLE_VALUE;
            ~FindHandle() { if (handle != INVALID_HANDLE_VALUE) FindClose(handle); }
        } search{FindFirstFileW((directory / L"*").c_str(), &child)};
        if (search.handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            check(error == ERROR_FILE_NOT_FOUND, "Enumerate owned native fixture: Windows error " + std::to_string(error));
            continue;
        }
        do {
            if (std::wstring_view{child.cFileName} == L"." || std::wstring_view{child.cFileName} == L"..") continue;
            check((child.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                  "Round-trip fixture tree does not follow reparse points");
            auto path = directory / child.cFileName;
            const bool isDirectory = (child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isDirectory) pending.push_back(path);
            entries.push_back(FixtureTreeEntry{std::move(path), isDirectory});
        } while (FindNextFileW(search.handle, &child));
        const auto error = GetLastError();
        check(error == ERROR_NO_MORE_FILES, "Complete owned native fixture enumeration: Windows error " + std::to_string(error));
    }
#else
    for (const auto& child : std::filesystem::recursive_directory_iterator{nativeRoot})
        entries.push_back(FixtureTreeEntry{child.path(), child.is_directory()});
#endif
    return entries;
}

inline std::filesystem::path logicalFixturePath(const std::filesystem::path& path) {
#ifdef _WIN32
    auto value = path.native();
    if (value.starts_with(L"\\\\?\\UNC\\")) value = L"\\\\" + value.substr(8);
    else if (value.starts_with(L"\\\\?\\")) value = value.substr(4);
    // Keep the drive/UNC root intact. MinGW's lexical path parser otherwise
    // treats the device prefix differently from native directory iterators.
    return std::filesystem::path{value};
#else
    return path;
#endif
}

inline std::filesystem::path relativeFixturePath(const std::filesystem::path& path,
                                                const std::filesystem::path& root) {
    const auto logicalPath = logicalFixturePath(path);
    const auto logicalRoot = logicalFixturePath(root);
    auto current = logicalPath.begin();
    for (const auto& component : logicalRoot) {
        check(current != logicalPath.end() && *current == component,
              "Fixture child remains inside its exact logical root");
        ++current;
    }
    std::filesystem::path relative;
    for (; current != logicalPath.end(); ++current) relative /= *current;
    return relative;
}

class Tests {
public:
    void run(const std::string& name, const std::function<void()>& callback) {
        try {
            callback();
            ++passed_;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }

    [[nodiscard]] int finish() const {
        std::cout << passed_ << " compression tests passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};

#ifdef _WIN32
inline DWORD removeOwnedFixtureTree(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return DeleteFileW(path.c_str()) ? ERROR_SUCCESS : GetLastError();
    // Never follow junctions or directory symlinks while cleaning test-owned
    // fixtures. This also avoids MinGW remove_all descending into link loops.
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return RemoveDirectoryW(path.c_str()) ? ERROR_SUCCESS : GetLastError();
    WIN32_FIND_DATAW child{};
    const HANDLE search = FindFirstFileW((path / L"*").c_str(), &child);
    if (search != INVALID_HANDLE_VALUE) {
        DWORD failure = ERROR_SUCCESS;
        do {
            if (std::wstring_view{child.cFileName} == L"." || std::wstring_view{child.cFileName} == L"..") continue;
            failure = removeOwnedFixtureTree(path / child.cFileName);
            if (failure != ERROR_SUCCESS) break;
        } while (FindNextFileW(search, &child));
        const DWORD lastError = GetLastError();
        FindClose(search);
        if (failure != ERROR_SUCCESS) return failure;
        if (lastError != ERROR_NO_MORE_FILES) return lastError;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return GetLastError();
    }
    return RemoveDirectoryW(path.c_str()) ? ERROR_SUCCESS : GetLastError();
}
#endif

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("ananarchiver-compression-tests-" + std::to_string(stamp));
        check(std::filesystem::create_directory(path), "Create unique compression fixture directory");
    }

    ~TemporaryDirectory() {
        // This freshly and exclusively created directory is wholly test-owned.
#ifdef _WIN32
        const DWORD error = removeOwnedFixtureTree(nativeFixturePath(path));
        if (error != ERROR_SUCCESS)
            std::cerr << "Could not clean compression fixture directory: Windows error " << error << '\n';
#else
        std::error_code error;
        std::filesystem::remove_all(nativeFixturePath(path), error);
        if (error) std::cerr << "Could not clean compression fixture directory: " << error.message() << '\n';
#endif
    }
};

inline void writeFile(const std::filesystem::path& path, std::string_view data) {
    std::filesystem::create_directories(nativeFixturePath(path.parent_path()));
    std::ofstream output{nativeFixturePath(path), std::ios::binary};
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(output), "Write generated fixture: " + pathToUtf8(path));
}

inline std::string readSmallFile(const std::filesystem::path& path) {
    std::ifstream input{nativeFixturePath(path), std::ios::binary};
    check(input.is_open(), "Read small fixture: " + pathToUtf8(path));
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

inline void generateFile(const std::filesystem::path& path, std::uint64_t size) {
    std::filesystem::create_directories(nativeFixturePath(path.parent_path()));
    std::ofstream output{nativeFixturePath(path), std::ios::binary};
    check(output.is_open(), "Open generated streaming fixture");
    std::array<char, 64 * 1024> buffer{};
    std::uint32_t state = 0x12345678U;
    for (std::uint64_t offset = 0; offset < size;) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - offset));
        for (std::size_t i = 0; i < count; ++i) {
            state = state * 1664525U + 1013904223U;
            buffer[i] = static_cast<char>(state >> 24U);
        }
        output.write(buffer.data(), static_cast<std::streamsize>(count));
        check(static_cast<bool>(output), "Write streaming fixture chunk");
        offset += count;
    }
}

inline bool equalFiles(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto nativeLeft = nativeFixturePath(left);
    const auto nativeRight = nativeFixturePath(right);
#ifdef _WIN32
    auto size = [](const std::filesystem::path& path) {
        WIN32_FILE_ATTRIBUTE_DATA information{};
        check(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &information) != 0,
              "Inspect native fixture size: " + pathToUtf8(path));
        return (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) | information.nFileSizeLow;
    };
    if (size(nativeLeft) != size(nativeRight)) return false;
#else
    if (std::filesystem::file_size(nativeLeft) != std::filesystem::file_size(nativeRight)) return false;
#endif
    std::ifstream first{nativeLeft, std::ios::binary};
    std::ifstream second{nativeRight, std::ios::binary};
    check(first.is_open() && second.is_open(), "Open both files for streaming byte comparison");
    std::array<char, 64 * 1024> firstBuffer{};
    std::array<char, 64 * 1024> secondBuffer{};
    while (first) {
        first.read(firstBuffer.data(), static_cast<std::streamsize>(firstBuffer.size()));
        second.read(secondBuffer.data(), static_cast<std::streamsize>(secondBuffer.size()));
        if (first.gcount() != second.gcount() ||
            !std::equal(firstBuffer.begin(), firstBuffer.begin() + first.gcount(), secondBuffer.begin())) return false;
    }
    check(!first.bad() && !second.bad(), "Streaming comparison completed without read errors");
    return true;
}

inline std::vector<std::string> childrenIn(const std::filesystem::path& path) {
    std::vector<std::string> names;
    if (!std::filesystem::exists(nativeFixturePath(path))) return names;
    for (const auto& child : std::filesystem::directory_iterator{nativeFixturePath(path)})
        names.push_back(pathToUtf8(child.path().filename()));
    std::ranges::sort(names);
    return names;
}

inline std::vector<std::string> treeIn(const std::filesystem::path& path) {
    std::vector<std::string> names;
    const auto root = nativeFixturePath(path);
    if (!std::filesystem::exists(root)) return names;
    for (const auto& child : fixtureTreeEntries(root)) {
        const auto relative = relativeFixturePath(child.path, root).generic_u8string();
        std::string name{relative.begin(), relative.end()};
        if (child.directory) name += '/';
        names.push_back(std::move(name));
    }
    std::ranges::sort(names);
    return names;
}

} // namespace compression_tests
