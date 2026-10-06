#include <ananarchiver/application/task_manager.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace ananarchiver::application {
namespace {
bool terminal(TaskState state) {
    return state == TaskState::Cancelled || state == TaskState::Succeeded ||
           state == TaskState::Failed;
}
} // namespace

struct TaskContext::SharedState {
    mutable std::mutex mutex;
    std::condition_variable_any space;
    std::stop_source stop;
    TaskSnapshot snapshot;
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point finished{};
    std::deque<std::vector<ArchiveEntry>> batches;
};

struct TaskManager::Impl {
    struct Task {
        std::shared_ptr<TaskContext::SharedState> state;
        std::jthread worker;
    };
    mutable std::mutex mutex;
    std::unordered_map<TaskId, std::unique_ptr<Task>> tasks;
    TaskId nextId{1};
};

TaskContext::TaskContext(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}
CancellationToken TaskContext::cancellation() const { return state_->stop.get_token(); }

void TaskContext::setArchiveInfo(ArchiveInfo info) {
    std::lock_guard lock(state_->mutex);
    state_->snapshot.archive = std::move(info);
}

void TaskContext::report(std::string file, std::uint64_t bytes, std::uint64_t entries,
                         bool allSizesKnown) {
    std::lock_guard lock(state_->mutex);
    state_->snapshot.currentFile = std::move(file);
    state_->snapshot.processedBytes = bytes;
    state_->snapshot.processedEntries = entries;
    state_->snapshot.byteTotalsKnown = allSizesKnown;
}

bool TaskContext::publishEntries(std::vector<ArchiveEntry> entries) {
    if (entries.empty())
        return !cancellation().stop_requested();
    if (entries.size() > 512) {
        throw std::length_error("Task metadata batch exceeds 512 entries");
    }
    std::unique_lock lock(state_->mutex);
    if (!state_->space.wait(lock, cancellation(), [&] { return state_->batches.size() < 8; })) {
        return false;
    }
    if (cancellation().stop_requested())
        return false;
    state_->batches.push_back(std::move(entries));
    return true;
}

TaskManager::TaskManager() : impl_(std::make_unique<Impl>()) {}
TaskManager::~TaskManager() {
    // Request all stops before joining any worker: producers may be waiting
    // for metadata queue space and must be woken during shutdown.
    for (auto& [id, task] : impl_->tasks) {
        (void)id;
        task->state->stop.request_stop();
        task->state->space.notify_all();
    }
    impl_->tasks.clear();
}

Result<TaskId> TaskManager::start(TaskType type, TaskWork work) {
    if (!work)
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::BackendError, "Task has no work function", {}, {}});
    std::lock_guard lock(impl_->mutex);
    std::size_t active = 0;
    for (const auto& [id, task] : impl_->tasks) {
        (void)id;
        std::lock_guard stateLock(task->state->mutex);
        if (!terminal(task->state->snapshot.state))
            ++active;
    }
    if (active >= 2 || impl_->tasks.size() >= 16) {
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                         "Task limit reached; cancel or release an existing task",
                         {},
                         {}});
    }
    const auto id = impl_->nextId++;
    auto task = std::make_unique<Impl::Task>();
    task->state = std::make_shared<TaskContext::SharedState>();
    task->state->snapshot.id = id;
    task->state->snapshot.type = type;
    // Register before launching: an allocation failure in the map must never
    // destroy a live producer that is blocked waiting for its consumer.
    const auto state = task->state;
    auto* registered = task.get();
    impl_->tasks.emplace(id, std::move(task));
    try {
        registered->worker = std::jthread([state, work = std::move(work)] {
            TaskContext context(state);
            {
                std::lock_guard stateLock(state->mutex);
                state->snapshot.state =
                    state->stop.stop_requested() || state->snapshot.state == TaskState::Cancelling
                        ? TaskState::Cancelling
                        : TaskState::Running;
            }
            Result<void> result;
            try {
                result = work(context);
            } catch (const std::bad_alloc&) {
                result = std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                                      "Archive task exceeded available memory",
                                                      {},
                                                      {}});
            } catch (const std::exception& e) {
                result = std::unexpected(ArchiveError{ArchiveErrorCode::BackendError,
                                                      "Archive task failed with an exception",
                                                      e.what(),
                                                      {}});
            } catch (...) {
                result =
                    std::unexpected(ArchiveError{ArchiveErrorCode::Unknown,
                                                 "Archive task failed with an unknown exception",
                                                 {},
                                                 {}});
            }
            std::lock_guard stateLock(state->mutex);
            state->finished = std::chrono::steady_clock::now();
            if (state->snapshot.state == TaskState::Cancelling ||
                (!result && result.error().code == ArchiveErrorCode::Cancelled) ||
                state->stop.stop_requested()) {
                state->snapshot.state = TaskState::Cancelled;
                state->snapshot.error =
                    ArchiveError{ArchiveErrorCode::Cancelled, "Task cancelled", {}, {}};
                state->batches.clear();
            } else if (!result) {
                state->snapshot.state = TaskState::Failed;
                state->snapshot.error = std::move(result.error());
                state->batches.clear();
            } else {
                state->snapshot.state = TaskState::Succeeded;
                if (state->snapshot.byteTotalsKnown)
                    state->snapshot.totalBytes = state->snapshot.processedBytes;
                state->snapshot.totalEntries = state->snapshot.processedEntries;
            }
        });
    } catch (const std::system_error& error) {
        impl_->tasks.erase(id);
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Unable to start archive task worker",
                                            error.what(),
                                            {}});
    } catch (const std::bad_alloc&) {
        impl_->tasks.erase(id);
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Not enough memory to start archive task worker",
                                            {},
                                            {}});
    }
    return id;
}

std::optional<TaskSnapshot> TaskManager::snapshot(TaskId id) const {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->tasks.find(id);
    if (it == impl_->tasks.end())
        return std::nullopt;
    const auto& state = *it->second->state;
    std::lock_guard stateLock(state.mutex);
    auto snapshot = state.snapshot;
    const auto end = terminal(snapshot.state) ? state.finished : std::chrono::steady_clock::now();
    snapshot.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - state.started);
    // Enumeration does not decompress these bytes, so throughput/ETA remain
    // unknown. Do not claim the metadata size sum is IO throughput.
    return snapshot;
}

std::vector<ArchiveEntry> TaskManager::takeEntries(TaskId id, std::size_t maxBatches) {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->tasks.find(id);
    if (it == impl_->tasks.end())
        return {};
    const auto& state = it->second->state;
    std::lock_guard stateLock(state->mutex);
    std::vector<ArchiveEntry> result;
    maxBatches = std::min(maxBatches, state->batches.size());
    result.reserve(maxBatches * 512);
    while (maxBatches-- > 0) {
        for (auto& entry : state->batches.front())
            result.push_back(std::move(entry));
        state->batches.pop_front();
    }
    state->space.notify_all();
    return result;
}

bool TaskManager::cancel(TaskId id) {
    std::shared_ptr<TaskContext::SharedState> state;
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->tasks.find(id);
        if (it == impl_->tasks.end())
            return false;
        state = it->second->state;
        std::lock_guard stateLock(state->mutex);
        if (terminal(state->snapshot.state))
            return false;
        state->snapshot.state = TaskState::Cancelling;
    }
    // stop callbacks run synchronously and may inspect this manager. Never
    // invoke them while holding the manager or task state locks.
    state->stop.request_stop();
    state->space.notify_all();
    return true;
}

bool TaskManager::release(TaskId id) {
    std::unique_ptr<Impl::Task> released;
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->tasks.find(id);
        if (it == impl_->tasks.end())
            return false;
        {
            std::lock_guard stateLock(it->second->state->mutex);
            if (!terminal(it->second->state->snapshot.state) || !it->second->state->batches.empty())
                return false;
        }
        released = std::move(it->second);
        impl_->tasks.erase(it);
    }
    return true;
}

} // namespace ananarchiver::application
