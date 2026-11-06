#pragma once

#include <ananarchiver/archive_error.h>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ananarchiver::application {
class ExportStoreLock;

// Owns only exclusively-created export directories. Creation and deletion run
// on workers; a retained directory remains available while the desktop copies
// its file URLs after the native drag has returned.
class TempFileManager final {
  public:
    explicit TempFileManager(std::filesystem::path temporaryParent = {}, bool durable = false);
    ~TempFileManager();
    TempFileManager(const TempFileManager&) = delete;
    TempFileManager& operator=(const TempFileManager&) = delete;
    Result<std::filesystem::path> create(); // Call from a worker.
    bool handoff(const std::filesystem::path& root);
    std::vector<std::filesystem::path> retainedRoots();
    void discard(const std::filesystem::path& root);
    bool hold(const std::filesystem::path& root);
    bool writable(const std::filesystem::path& root);
    void retain(const std::filesystem::path& root, std::chrono::seconds lifetime);
    std::vector<ArchiveError> takeCleanupErrors();

  private:
    void clean(std::stop_token stop);
    std::filesystem::path temporaryParent_;
    bool durable_{};
    std::unique_ptr<ExportStoreLock> storeLock_;
    std::mutex mutex_;
    std::condition_variable changed_;
    struct Root {
        std::chrono::steady_clock::time_point expires;
        int attempts{};
        bool cleaning{};
        bool handedOff{};
        bool restored{};
    };
    std::map<std::filesystem::path, Root> roots_;
    std::vector<ArchiveError> cleanupErrors_;
    std::size_t creating_{};
    std::jthread worker_;
};

} // namespace ananarchiver::application
