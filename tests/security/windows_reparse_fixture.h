#pragma once

#ifdef _WIN32

#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace extraction_tests {

// Directory junctions use mount-point reparse records and do not need the
// symbolic-link privilege. This also covers the common Windows junction
// escape when symlink-creation tests cannot run under the current account.
inline bool createDirectoryJunction(const std::filesystem::path& link,
                                   const std::filesystem::path& target,
                                   DWORD& error) {
    const auto printName = std::filesystem::absolute(target).native();
    const auto substituteName = L"\\??\\" + printName;
    const auto substituteBytes = substituteName.size() * sizeof(wchar_t);
    const auto printBytes = printName.size() * sizeof(wchar_t);
    const auto namesBytes = substituteBytes + printBytes + 2 * sizeof(wchar_t);
    const auto totalBytes = 16 + namesBytes;
    if (totalBytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE) {
        error = ERROR_FILENAME_EXCED_RANGE;
        return false;
    }
    if (!CreateDirectoryW(link.c_str(), nullptr)) {
        error = GetLastError();
        return false;
    }
    const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        RemoveDirectoryW(link.c_str());
        return false;
    }
    std::vector<std::byte> record(totalBytes);
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(record.data(), &tag, sizeof(tag));
    const auto setShort = [&](std::size_t offset, std::size_t value) {
        const auto narrowed = static_cast<WORD>(value);
        std::memcpy(record.data() + offset, &narrowed, sizeof(narrowed));
    };
    setShort(4, 8 + namesBytes);
    setShort(8, 0);
    setShort(10, substituteBytes);
    setShort(12, substituteBytes + sizeof(wchar_t));
    setShort(14, printBytes);
    std::memcpy(record.data() + 16, substituteName.data(), substituteBytes);
    std::memcpy(record.data() + 16 + substituteBytes + sizeof(wchar_t), printName.data(), printBytes);
    DWORD returned = 0;
    const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, record.data(),
                                         static_cast<DWORD>(record.size()), nullptr, 0,
                                         &returned, nullptr) != 0;
    error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!success) {
        RemoveDirectoryW(link.c_str());
    }
    return success;
}

} // namespace extraction_tests

#endif
