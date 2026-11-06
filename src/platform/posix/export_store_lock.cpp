#include "../export_store_lock.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <system_error>
#include <unistd.h>
namespace ananarchiver::application {
std::filesystem::path exportIoPath(const std::filesystem::path& path) { return path; }
namespace {
class Lock final : public ExportStoreLock {
  public:
    int fd;
    explicit Lock(int value) : fd(value) {}
    ~Lock() override {
        if (fd >= 0)
            close(fd);
    }
};
} // namespace
std::unique_ptr<ExportStoreLock> lockExportStore(const std::filesystem::path& path) {
    auto lock = std::make_unique<Lock>(-1);
    lock->fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock->fd < 0)
        throw std::filesystem::filesystem_error("Could not open private export lock", path,
                                                std::error_code(errno, std::generic_category()));
    if (flock(lock->fd, LOCK_EX | LOCK_NB) != 0) {
        const auto error = std::error_code(errno, std::generic_category());
        throw std::filesystem::filesystem_error("Private exports are owned by another process",
                                                path, error);
    }
    return lock;
}
} // namespace ananarchiver::application
