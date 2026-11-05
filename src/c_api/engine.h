#pragma once

#include <ananarchiver/application/archive_service.h>
#include <ananarchiver/application/browser_service.h>
#include <ananarchiver/c_api/ananarchiver.h>

#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

// The C engine owns services, IDs and the cleanup thread. No global registry or
// detached worker is needed; every engine has an independent bounded lifetime.
struct aa_engine {
    struct Slot {
        aa_task_id id{};
        bool retiring{};
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::array<Slot, 16> slots{};
    bool closing{};
    bool stopped{};
    bool browserCleanup{};
    std::unique_ptr<ananarchiver::application::TaskManager> tasks;
    std::unique_ptr<ananarchiver::application::ArchiveService> archives;
    std::unique_ptr<ananarchiver::application::BrowserService> browser;
    std::jthread cleaner;

    aa_engine();
    ~aa_engine();
    bool contains(aa_task_id id) const;
    void track(aa_task_id id);
    void shutdown();

  private:
    void clean();
};
