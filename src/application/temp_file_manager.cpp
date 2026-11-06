#include "../platform/export_store_lock.h"
#include <ananarchiver/application/temp_file_manager.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <random>
#include <utility>

namespace ananarchiver::application {

TempFileManager::TempFileManager(std::filesystem::path temporaryParent, bool durable)
    : temporaryParent_(std::move(temporaryParent)), durable_(durable) {
    if (durable_) {
        std::filesystem::create_directories(temporaryParent_);
        storeLock_ = lockExportStore(temporaryParent_ / "store.lock");
        for (int i = 0; i < 4; ++i) {
            auto path = temporaryParent_ / ("export-" + std::to_string(i));
            if (std::filesystem::exists(path))
                roots_.emplace(
                    path, Root{std::chrono::steady_clock::time_point::max(), 0, false, true, true});
        }
    }
    worker_ = std::jthread([this](std::stop_token stop) { clean(stop); });
}

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
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                             "Four exports are still retained for desktop copies or awaiting "
                             "cleanup; wait before preparing another export",
                             {},
                             {}});
        ++creating_;
    }
    // Reserve before doing worker IO, without making GUI hold/retain calls
    // wait on it. Exceptions release the reservation and provisional directory.
    auto releaseSlot = [](TempFileManager* manager) {
        std::lock_guard lock(manager->mutex_);
        --manager->creating_;
    };
    std::unique_ptr<TempFileManager, decltype(releaseSlot)> reservation(this, releaseSlot);
    const auto parent =
        temporaryParent_.empty() ? std::filesystem::temp_directory_path() : temporaryParent_;
    struct Provisional {
        std::filesystem::path path;
        bool committed{};
        ~Provisional() {
            if (!path.empty() && !committed) {
                std::error_code error;
                // The directory is still empty: no task can see it before
                // registration. Avoid allocating a long-path copy on unwind.
                std::filesystem::remove(path, error);
                if (error)
                    std::fputs("LiteArc provisional export cleanup failed; a private directory may "
                               "remain\n",
                               stderr);
            }
        }
    } provisional;
    auto& path = provisional.path;
    std::error_code error;
    for (int i = 0; i < (durable_ ? 4 : 32); ++i) {
        const auto name = durable_
                              ? "export-" + std::to_string(i)
                              : "AnAnArchiver-export-" + std::to_string(std::random_device{}());
        auto candidate = parent / name;
        if (std::filesystem::create_directory(candidate, error)) {
            path = std::move(candidate);
            break;
        }
        if (error && error != std::errc::file_exists)
            break;
        error.clear();
    }
    if (path.empty())
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                         "No private export slot available; release completed desktop copies first",
                         error.message(), parent});
    std::filesystem::permissions(path, std::filesystem::perms::owner_all, error);
    if (error) {
        std::filesystem::remove(path);
        return std::unexpected(ArchiveError{ArchiveErrorCode::PermissionDenied,
                                            "Could not protect private export directory",
                                            error.message(), path});
    }
    {
        std::lock_guard lock(mutex_);
        roots_.emplace(path, Root{std::chrono::steady_clock::now() + std::chrono::minutes{10}});
    }
    provisional.committed = true;
    changed_.notify_all();
    return path;
}

bool TempFileManager::handoff(const std::filesystem::path& root) {
    std::lock_guard lock(mutex_);
    const auto found = roots_.find(root);
    if (found == roots_.end() || found->second.cleaning)
        return false;
    found->second.handedOff = true;
    found->second.expires = std::chrono::steady_clock::time_point::max();
    return true;
}

bool TempFileManager::writable(const std::filesystem::path& root) {
    std::lock_guard lock(mutex_);
    const auto found = roots_.find(root);
    return found != roots_.end() && !found->second.cleaning && !found->second.handedOff &&
           !found->second.restored && found->second.attempts == 0;
}

std::vector<std::filesystem::path> TempFileManager::retainedRoots() {
    std::lock_guard lock(mutex_);
    std::vector<std::filesystem::path> paths;
    for (const auto& [path, root] : roots_)
        if (root.handedOff || root.attempts >= 3)
            paths.push_back(path);
    return paths;
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
    found->second.handedOff = false;
    found->second.attempts = 0;
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
            if (!item->second.handedOff && (stop.stop_requested() || item->second.expires <= now)) {
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
            std::error_code removalError;
            std::filesystem::remove_all(exportIoPath(root), removalError);
            const bool removed = !removalError;
            lock.lock();
            selected = roots_.find(root);
            if (removed) {
                roots_.erase(selected);
            } else if (stop.stop_requested()) {
                std::clog << "LiteArc could not remove private export directory: " << root << ": "
                          << removalError.message() << '\n';
                roots_.erase(selected);
            } else if (++selected->second.attempts < 3) {
                selected->second.cleaning = false;
                selected->second.expires =
                    std::chrono::steady_clock::now() + std::chrono::seconds{1};
            } else {
                selected->second.cleaning = false;
                // Keep ownership for one final shutdown attempt and report the
                // precise remaining path. Never loop indefinitely on a lock.
                selected->second.expires = std::chrono::steady_clock::time_point::max();
                std::erase_if(cleanupErrors_,
                              [&root](const ArchiveError& error) { return error.path == root; });
                if (cleanupErrors_.size() == 4)
                    cleanupErrors_.erase(cleanupErrors_.begin());
                cleanupErrors_.push_back({ArchiveErrorCode::CleanupFailed,
                                          "Could not clean a private export directory after three "
                                          "attempts; close programs using its files",
                                          removalError.message(), root});
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

} // namespace ananarchiver::application
