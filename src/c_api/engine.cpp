#include "engine.h"

#include <algorithm>

using namespace ananarchiver::application;

aa_engine::aa_engine()
    : tasks(std::make_unique<TaskManager>()), archives(std::make_unique<ArchiveService>(*tasks)),
      browser(std::make_unique<BrowserService>(*tasks)), cleaner([this] { clean(); }) {}

aa_engine::~aa_engine() {
    shutdown();
    cleaner.join();
}

bool aa_engine::contains(aa_task_id id) const {
    return id != 0 && std::ranges::any_of(slots, [id](const Slot& slot) {
               return slot.id == id && !slot.retiring;
           });
}

void aa_engine::track(aa_task_id id) {
    // TaskManager has the same sixteen-task bound and retains retiring tasks
    // until clean() reclaims them. Caller holds mutex; a free slot must exist.
    for (auto& slot : slots) {
        if (slot.id == 0) {
            slot = {id, false};
            return;
        }
    }
    std::terminate(); // internal invariant, never dependent on caller input
}

void aa_engine::shutdown() {
    {
        std::lock_guard lock(mutex);
        closing = true;
    }
    changed.notify_all();
}

void aa_engine::clean() {
    std::unique_lock lock(mutex);
    for (;;) {
        changed.wait(lock, [this] {
            return closing || browserCleanup || std::ranges::any_of(slots, [](const Slot& slot) {
                       return slot.id != 0 && slot.retiring;
                   });
        });
        if (closing) {
            // New calls observe closing under mutex before accessing services.
            // TaskManager requests all stops before joining, including workers
            // blocked on metadata backpressure or an overwrite answer.
            lock.unlock();
            archives.reset();
            tasks.reset();
            browser.reset();
            lock.lock();
            slots = {};
            stopped = true;
            return;
        }
        if (browserCleanup) {
            browserCleanup = false;
            lock.unlock();
            browser->reclaim();
            lock.lock();
        }
        for (auto& slot : slots) {
            if (slot.id == 0 || !slot.retiring)
                continue;
            const auto id = slot.id;
            lock.unlock();
            tasks->cancel(id);
            tasks->discardEntries(id);
            const auto released = tasks->release(id);
            lock.lock();
            if (released)
                slot = {};
        }
        // A retiring operation may still be unwinding. Wait without retaining
        // the engine lock; shutdown wakes this wait immediately.
        changed.wait_for(lock, std::chrono::milliseconds{5}, [this] { return closing; });
    }
}
