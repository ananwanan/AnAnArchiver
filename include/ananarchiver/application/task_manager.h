#pragma once

#include <ananarchiver/archive_reader.h>
#include <ananarchiver/compression.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ananarchiver::application {

using TaskId = std::uint64_t;
enum class TaskType { OpenArchive, ExtractArchive, CompressArchive };
enum class TaskState { Pending, Running, Cancelling, Cancelled, Succeeded, Failed };

struct ConflictResolution {
    OverwritePolicy policy{OverwritePolicy::Skip};
    bool applyToAll{};
};

struct TaskSnapshot {
    TaskId id{};
    TaskType type{TaskType::OpenArchive};
    TaskState state{TaskState::Pending};
    std::string currentFile;
    // Indexing reports metadata sizes; extraction reports actual streamed bytes.
    std::uint64_t processedBytes{};
    std::uint64_t processedEntries{};
    bool byteTotalsKnown{true};
    std::optional<std::uint64_t> totalBytes;
    std::optional<std::uint64_t> totalEntries;
    std::optional<ArchiveInfo> archive;
    std::optional<ArchiveError> error;
    std::optional<ExtractionConflict> conflict;
    std::optional<CompressionPhase> compressionPhase;
    std::uint64_t conflictId{};
    std::chrono::milliseconds elapsed{};
    double bytesPerSecond{};
    std::optional<std::chrono::milliseconds> estimatedRemaining;
};

class TaskContext {
  public:
    CancellationToken cancellation() const;
    void setArchiveInfo(ArchiveInfo info);
    void report(std::string currentFile, std::uint64_t bytes, std::uint64_t entries,
                bool allSizesKnown = true);
    void report(const ExtractionProgress& progress);
    void report(const CompressionProgress& progress);
    // The worker waits without holding manager locks. Stop requests wake it,
    // including during application shutdown. The GUI only polls/responds.
    OverwritePolicy requestConflict(const ExtractionConflict& conflict);
    // Backpressure limits queued metadata to eight batches. Cancellation wakes
    // a producer even when the UI has stopped consuming entries.
    bool publishEntries(std::vector<ArchiveEntry> entries);

  private:
    struct SharedState;
    explicit TaskContext(std::shared_ptr<SharedState> state);
    std::shared_ptr<SharedState> state_;
    friend class TaskManager;
};

using TaskWork = std::function<Result<void>(TaskContext&)>;

class TaskManager {
  public:
    TaskManager();
    ~TaskManager();
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    Result<TaskId> start(TaskType type, TaskWork work);
    std::optional<TaskSnapshot> snapshot(TaskId id) const;
    std::vector<ArchiveEntry> takeEntries(TaskId id, std::size_t maxBatches = 4);
    bool cancel(TaskId id);
    bool resolveConflict(TaskId id, std::uint64_t conflictId, ConflictResolution resolution);
    // Only terminal tasks with drained entries can be released. Joins take
    // place after completion, outside manager locks.
    bool release(TaskId id);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ananarchiver::application
