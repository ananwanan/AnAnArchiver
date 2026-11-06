#include "../export_store_lock.h"
#include <system_error>
#include <windows.h>
namespace ananarchiver::application {
std::filesystem::path exportIoPath(const std::filesystem::path& path) {
    auto absolute = std::filesystem::absolute(path);
    absolute.make_preferred();
    const auto& value = absolute.native();
    if (value.starts_with(L"\\\\?\\"))
        return absolute;
    if (value.starts_with(L"\\\\"))
        return std::filesystem::path{L"\\\\?\\UNC\\" + value.substr(2)};
    return std::filesystem::path{L"\\\\?\\" + value};
}
namespace {
class Lock final : public ExportStoreLock {
  public:
    HANDLE handle;
    explicit Lock(HANDLE value) : handle(value) {}
    ~Lock() override {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
} // namespace
std::unique_ptr<ExportStoreLock> lockExportStore(const std::filesystem::path& path) {
    auto lock = std::make_unique<Lock>(INVALID_HANDLE_VALUE);
    lock->handle = CreateFileW(exportIoPath(path).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock->handle == INVALID_HANDLE_VALUE)
        throw std::filesystem::filesystem_error(
            "Private exports are owned by another process or inaccessible", path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    return lock;
}
} // namespace ananarchiver::application
