#include "temp_file_manager.h"
#include "path_conversion.h"

#include <QDir>
#include <QDebug>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>
#include <utility>

namespace ananarchiver::qt {

TempFileManager::TempFileManager(std::filesystem::path temporaryParent)
    : temporaryParent_(std::move(temporaryParent)),
      worker_([this](std::stop_token stop) { clean(stop); }) {}

TempFileManager::~TempFileManager() {
    worker_.request_stop();
    changed_.notify_all();
    worker_.join();
}

Result<std::filesystem::path> TempFileManager::create() {
    {
        std::lock_guard lock(mutex_);
        // Four roots bound cumulative retained/locked output to at most four
        // per-export byte budgets. Repeated dragging of one root reuses it.
        if (roots_.size() + creating_ >= 4)
            return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                "Four exports are still retained for desktop copies or awaiting cleanup; wait before preparing another export", {}, {}});
        ++creating_;
    }
    // Reserve before doing worker IO, without making GUI hold/retain calls
    // wait on it. Exception paths release the slot and QTemporaryDir cleans
    // itself until successful registration transfers ownership.
    auto releaseSlot = [](TempFileManager* manager) {
        std::lock_guard lock(manager->mutex_);
        --manager->creating_;
    };
    std::unique_ptr<TempFileManager, decltype(releaseSlot)> reservation(this, releaseSlot);
    const auto parent = temporaryParent_.empty() ? QDir::tempPath() : fromPath(temporaryParent_);
    QTemporaryDir temporary(parent + QStringLiteral("/AnAnArchiver-export-XXXXXX"));
    if (!temporary.isValid())
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
            "Could not create a private export directory", temporary.errorString().toStdString(),
            temporaryParent_});
    auto path = toPath(temporary.path());
    {
        std::lock_guard lock(mutex_);
        // The allocation must succeed before disabling QTemporaryDir cleanup.
        roots_.emplace(path, Root{std::chrono::steady_clock::time_point::max()});
        temporary.setAutoRemove(false);
    }
    changed_.notify_all();
    return path;
}

void TempFileManager::discard(const std::filesystem::path& root) {
    retain(root, std::chrono::seconds{0});
}

bool TempFileManager::hold(const std::filesystem::path& root) {
    std::lock_guard lock(mutex_);
    const auto found = roots_.find(root);
    if (found == roots_.end() || found->second.cleaning)
        return false;
    found->second.expires = std::chrono::steady_clock::time_point::max();
    changed_.notify_all();
    return true;
}

void TempFileManager::retain(const std::filesystem::path& root, std::chrono::seconds lifetime) {
    std::lock_guard lock(mutex_);
    const auto found = roots_.find(root);
    if (found == roots_.end() || found->second.cleaning)
        return;
    found->second.expires = std::chrono::steady_clock::now() + lifetime;
    changed_.notify_all();
}

std::vector<ArchiveError> TempFileManager::takeCleanupErrors() {
    std::lock_guard lock(mutex_);
    return std::exchange(cleanupErrors_, {});
}

void TempFileManager::clean(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    while (true) {
        auto selected = roots_.end();
        const auto now = std::chrono::steady_clock::now();
        auto next = std::chrono::steady_clock::time_point::max();
        for (auto item = roots_.begin(); item != roots_.end(); ++item) {
            if (stop.stop_requested() || item->second.expires <= now) {
                selected = item;
                break;
            }
            next = std::min(next, item->second.expires);
        }
        if (selected != roots_.end()) {
            // This exact path was created and registered by this manager.
            // Recursive deletion removes symlinks rather than their target.
            const auto root = selected->first;
            selected->second.cleaning = true;
            lock.unlock();
            // Qt's native filesystem adapter supports Windows long paths.
            // QTemporaryDir created this exact private root; QDir's recursive
            // removal does not follow symlinks outside it.
            const bool removed = QDir(fromPath(root)).removeRecursively();
            lock.lock();
            selected = roots_.find(root);
            if (removed) {
                roots_.erase(selected);
            } else if (stop.stop_requested()) {
                qWarning().noquote() << "AnAnArchiver could not remove private export directory:" << fromPath(root);
                roots_.erase(selected);
            } else if (++selected->second.attempts < 3) {
                selected->second.cleaning = false;
                selected->second.expires = std::chrono::steady_clock::now() + std::chrono::seconds{1};
            } else {
                selected->second.cleaning = false;
                // Keep ownership for one final shutdown attempt and report the
                // precise remaining path. Never loop indefinitely on a lock.
                selected->second.expires = std::chrono::steady_clock::time_point::max();
                cleanupErrors_.push_back({ArchiveErrorCode::CleanupFailed,
                    "Could not clean a private export directory after three attempts; close programs using its files", {}, root});
            }
            continue;
        }
        if (stop.stop_requested())
            return;
        if (next == std::chrono::steady_clock::time_point::max())
            changed_.wait(lock);
        else
            changed_.wait_until(lock, next);
    }
}

} // namespace ananarchiver::qt
