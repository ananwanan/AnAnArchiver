#include <ananarchiver/application/archive_service.h>
#include <ananarchiver/application/directory_service.h>
#include <ananarchiver/libarchive_reader.h>

#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ananarchiver;
using namespace ananarchiver::application;
using namespace std::chrono_literals;

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Fixture {
    std::filesystem::path root;
    Fixture() {
        static std::atomic<unsigned> sequence{0};
        root = std::filesystem::temp_directory_path() /
               ("ananarchiver-application-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence++));
        std::filesystem::create_directories(root);
    }
    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

struct ReaderState {
    std::atomic<std::uint64_t> enumerated{0};
    std::atomic<unsigned> destroyed{0};
    std::uint64_t count{10'000};
    bool fail{};
    bool unknownSizes{};
};

class SyntheticReader final : public ArchiveReader {
  public:
    explicit SyntheticReader(std::shared_ptr<ReaderState> state) : state_(std::move(state)) {}
    ~SyntheticReader() override { ++state_->destroyed; }
    Result<ArchiveInfo> open(const std::filesystem::path& path,
                             CancellationToken cancellation) override {
        if (cancellation.stop_requested())
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, path});
        if (state_->fail)
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidArchive, "Invalid fixture",
                                                "fixture backend detail", path});
        ArchiveInfo info;
        info.path = path;
        info.formatName = "Synthetic test reader";
        return info;
    }
    Result<void> enumerate(const EntryCallback& callback, CancellationToken cancellation) override {
        for (std::uint64_t i = 0; i < state_->count; ++i) {
            ArchiveEntry entry;
            entry.path = "nested/entry-" + std::to_string(i);
            entry.size = i;
            entry.sizeKnown = !state_->unknownSizes;
            ++state_->enumerated;
            if (cancellation.stop_requested() || !callback(entry))
                return std::unexpected(
                    ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
        }
        return {};
    }

  private:
    std::shared_ptr<ReaderState> state_;
};

TaskSnapshot awaitTerminal(TaskManager& tasks, TaskId id,
                           std::vector<ArchiveEntry>* entries = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (entries) {
            auto batch = tasks.takeEntries(id);
            for (auto& entry : batch)
                entries->push_back(std::move(entry));
        }
        const auto state = tasks.snapshot(id);
        check(state.has_value(), "task disappeared");
        if (state->state == TaskState::Succeeded || state->state == TaskState::Failed ||
            state->state == TaskState::Cancelled) {
            if (entries) {
                for (;;) {
                    auto batch = tasks.takeEntries(id);
                    if (batch.empty())
                        break;
                    for (auto& entry : batch)
                        entries->push_back(std::move(entry));
                }
            }
            return *state;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("task failed to finish within deadline");
}

void streamingBatches() {
    TaskManager tasks;
    auto state = std::make_shared<ReaderState>();
    ArchiveService service(tasks, [state] { return std::make_unique<SyntheticReader>(state); });
    const auto id = service.open("stream.fixture");
    check(id.has_value(), "task submission failed");
    std::vector<ArchiveEntry> entries;
    const auto snapshot = awaitTerminal(tasks, *id, &entries);
    check(snapshot.state == TaskState::Succeeded, "index task failed");
    check(entries.size() == state->count, "metadata was lost across batch boundaries");
    check(entries.front().path == "nested/entry-0" && entries.back().path == "nested/entry-9999",
          "entry order changed");
    check(snapshot.processedEntries == 10'000 && snapshot.processedBytes == 49'995'000,
          "task counters incorrect");
    check(snapshot.totalEntries == 10'000 && snapshot.archive.has_value(), "task result missing");
    check(!snapshot.estimatedRemaining && snapshot.bytesPerSecond == 0,
          "index fabricated IO throughput");
    check(tasks.release(*id) && !tasks.snapshot(*id), "completed task not reclaimed");
    check(state->destroyed == 1, "reader leaked");
}

void boundedQueueAndCancellation() {
    TaskManager tasks;
    auto state = std::make_shared<ReaderState>();
    state->count = 1'000'000;
    ArchiveService service(tasks, [state] { return std::make_unique<SyntheticReader>(state); });
    const auto id = service.open("large.fixture");
    check(id.has_value(), "task submission failed");
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (state->enumerated < 4608 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    check(state->enumerated == 4608, "queue did not apply bounded backpressure");
    std::this_thread::sleep_for(10ms);
    check(state->enumerated == 4608, "producer continued while queue full");
    check(!tasks.release(*id), "running task was released");
    check(tasks.cancel(*id), "cancellation rejected");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Cancelled, "blocked producer was not cancelled");
    check(tasks.takeEntries(*id).empty(), "cancel left stale metadata queued");
    check(tasks.release(*id), "cancelled task not reclaimable");
    check(state->destroyed == 1, "cancel leaked reader");
}

void unknownSizeRemainsUnknown() {
    TaskManager tasks;
    auto state = std::make_shared<ReaderState>();
    state->count = 1;
    state->unknownSizes = true;
    ArchiveService service(tasks, [state] { return std::make_unique<SyntheticReader>(state); });
    const auto id = service.open("unknown.fixture");
    check(id.has_value(), "task submission failed");
    std::vector<ArchiveEntry> entries;
    const auto snapshot = awaitTerminal(tasks, *id, &entries);
    check(snapshot.state == TaskState::Succeeded && entries.size() == 1 &&
              !entries.front().sizeKnown,
          "unknown size entry did not survive batching");
    check(!snapshot.byteTotalsKnown && !snapshot.totalBytes,
          "unknown size was reported as zero total bytes");
    check(tasks.release(*id), "unknown size task not reclaimed");
}

void shutdownWakesProducer() {
    auto state = std::make_shared<ReaderState>();
    const auto start = std::chrono::steady_clock::now();
    {
        TaskManager tasks;
        ArchiveService service(tasks, [state] { return std::make_unique<SyntheticReader>(state); });
        check(service.open("shutdown.fixture").has_value(), "task submission failed");
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (state->enumerated < 4608 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        check(state->enumerated == 4608, "producer did not fill queue");
    }
    check(std::chrono::steady_clock::now() - start < 2s, "shutdown hung on full metadata queue");
    check(state->destroyed == 1, "shutdown leaked reader");
}

void structuredFailure() {
    TaskManager tasks;
    auto state = std::make_shared<ReaderState>();
    state->fail = true;
    ArchiveService service(tasks, [state] { return std::make_unique<SyntheticReader>(state); });
    const auto id = service.open("bad.fixture");
    check(id.has_value(), "task submission failed");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Failed && snapshot.error.has_value(),
          "missing failed status");
    check(snapshot.error->code == ArchiveErrorCode::InvalidArchive &&
              snapshot.error->path == "bad.fixture",
          "error context lost");
    check(snapshot.error->backendMessage == "fixture backend detail", "backend detail lost");
    check(!tasks.cancel(*id) && tasks.release(*id), "terminal state mishandled");
}

void exceptionBoundary() {
    TaskManager tasks;
    const auto id = tasks.start(TaskType::OpenArchive, [](TaskContext&) -> Result<void> {
        throw std::runtime_error("test exception");
    });
    check(id.has_value(), "task submission failed");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Failed &&
              snapshot.error->code == ArchiveErrorCode::BackendError,
          "worker exception escaped boundary");
    check(tasks.release(*id), "failed task could not be released");
    const auto memoryFailure = tasks.start(
        TaskType::OpenArchive, [](TaskContext&) -> Result<void> { throw std::bad_alloc(); });
    check(memoryFailure.has_value(), "memory failure task submission failed");
    const auto memorySnapshot = awaitTerminal(tasks, *memoryFailure);
    check(memorySnapshot.state == TaskState::Failed &&
              memorySnapshot.error->code == ArchiveErrorCode::ResourceLimitExceeded,
          "allocation failure did not report resource limit");
    check(tasks.release(*memoryFailure), "memory failure task could not be released");
}

void taskLimits() {
    TaskManager tasks;
    auto work = [](TaskContext& context) -> Result<void> {
        while (!context.cancellation().stop_requested())
            std::this_thread::sleep_for(1ms);
        return {};
    };
    const auto first = tasks.start(TaskType::OpenArchive, work);
    const auto second = tasks.start(TaskType::OpenArchive, work);
    check(first && second, "two independent tasks could not start");
    const auto third = tasks.start(TaskType::OpenArchive, work);
    check(!third && third.error().code == ArchiveErrorCode::ResourceLimitExceeded,
          "unbounded worker creation");
    tasks.cancel(*first);
    tasks.cancel(*second);
    check(awaitTerminal(tasks, *first).state == TaskState::Cancelled, "first task did not cancel");
    check(awaitTerminal(tasks, *second).state == TaskState::Cancelled,
          "second task did not cancel");
    check(tasks.release(*first) && tasks.release(*second), "task release failed");
    check(!tasks.start(TaskType::OpenArchive, {}).has_value(), "empty task accepted");
}

void cancellationCallbackCanInspectManager() {
    TaskManager tasks;
    std::atomic<bool> ready{false};
    std::atomic<bool> inspected{false};
    std::atomic<TaskId> target{0};
    const auto id = tasks.start(TaskType::OpenArchive, [&](TaskContext& context) -> Result<void> {
        std::stop_callback onStop(context.cancellation(),
                                  [&] { inspected = tasks.snapshot(target).has_value(); });
        ready = true;
        while (!context.cancellation().stop_requested())
            std::this_thread::sleep_for(1ms);
        return {};
    });
    check(id.has_value(), "task submission failed");
    target = *id;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    check(ready, "stop callback was not registered");
    check(tasks.cancel(*id), "cancel rejected");
    check(inspected, "stop callback could not inspect task manager");
    check(awaitTerminal(tasks, *id).state == TaskState::Cancelled && tasks.release(*id),
          "task with stop callback was not cleaned up");
}

void acceptedCancellationWinsCompletionRace() {
    TaskManager tasks;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const auto id = tasks.start(TaskType::OpenArchive, [](TaskContext&) -> Result<void> {
            std::this_thread::yield();
            return {};
        });
        check(id.has_value(), "race task submission failed");
        const bool accepted = tasks.cancel(*id);
        const auto snapshot = awaitTerminal(tasks, *id);
        check(!accepted || snapshot.state == TaskState::Cancelled,
              "accepted cancellation was replaced by success");
        check(tasks.release(*id), "race task was not reclaimed");
    }
}

void immediateDirectoryChildren() {
    Fixture fixture;
    std::filesystem::create_directories(fixture.root / "nested" / "deeper");
    std::ofstream(fixture.root / "nested" / "deeper" / "hidden.txt") << "hidden";
    std::ofstream(fixture.root / "file.txt") << "hello";
    const auto unicodeName = std::filesystem::path(std::u8string(u8"安安🐈.txt"));
    std::ofstream(fixture.root / unicodeName) << "unicode";
    const auto result = DirectoryService::scanChildren(fixture.root);
    check(result && result->size() == 3, "directory scan recursed or lost children");
    check(result->front().directory && result->front().name == "nested",
          "directories not sorted first");
    bool unicodeFound = false;
    for (const auto& entry : *result) {
        check(entry.path.parent_path() == fixture.root, "scan returned non-child");
        if (entry.path.filename() == unicodeName) {
            unicodeFound = true;
            check(entry.name == reinterpret_cast<const char*>(u8"安安🐈.txt"),
                  "unicode name was lossy");
            check(entry.size == 7 && entry.modified.has_value(), "metadata incorrect");
        }
    }
    check(unicodeFound, "unicode child missing");
    const auto nested = DirectoryService::scanChildren(fixture.root / "nested");
    check(nested && nested->size() == 1 && nested->front().name == "deeper",
          "lazy expansion result incorrect");
}

void directoryErrorsAndCancellation() {
    Fixture fixture;
    const auto missing = DirectoryService::scanChildren(fixture.root / "missing");
    check(!missing && missing.error().code == ArchiveErrorCode::FileNotFound,
          "directory failure silently ignored");
    std::ofstream(fixture.root / "file.txt") << "data";
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = DirectoryService::scanChildren(fixture.root, stop.get_token());
    check(!cancelled && cancelled.error().code == ArchiveErrorCode::Cancelled,
          "directory cancellation ignored");
}

TaskSnapshot awaitConflict(TaskManager& tasks, TaskId id) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = tasks.snapshot(id);
        check(snapshot.has_value(), "conflict task disappeared");
        if (snapshot->conflict)
            return *snapshot;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("worker did not publish conflict");
}

void conflictRoundTripAndApplyAll() {
    TaskManager tasks;
    std::atomic<unsigned> resolved{};
    const auto id = tasks.start(TaskType::ExtractArchive, [&](TaskContext& context) -> Result<void> {
        ExtractionConflict conflict;
        conflict.entry.path = "duplicate.txt";
        conflict.destination = "output/duplicate.txt";
        for (unsigned i = 0; i < 3; ++i) {
            if (context.requestConflict(conflict) != OverwritePolicy::Rename)
                return std::unexpected(ArchiveError{ArchiveErrorCode::OutputExists,
                    "Unexpected conflict decision", {}, {}});
            ++resolved;
        }
        return {};
    });
    check(id.has_value(), "conflict task start failed");
    const auto pending = awaitConflict(tasks, *id);
    check(!tasks.resolveConflict(*id, pending.conflictId + 1, {OverwritePolicy::Skip, false}),
          "stale conflict answer accepted");
    check(!tasks.resolveConflict(*id, pending.conflictId, {OverwritePolicy::Ask, false}),
          "unresolved Ask answer accepted");
    check(tasks.resolveConflict(*id, pending.conflictId, {OverwritePolicy::Rename, true}),
          "conflict answer rejected");
    const auto terminal = awaitTerminal(tasks, *id);
    check(terminal.state == TaskState::Succeeded && resolved == 3 && !terminal.conflict,
          "apply-to-all did not cover remaining conflicts");
    check(!tasks.resolveConflict(*id, pending.conflictId, {OverwritePolicy::Overwrite, false}),
          "terminal task accepted an answer");
    check(tasks.release(*id), "conflict task not released");
}

void cancellationAndShutdownWakeConflict() {
    auto work = [](TaskContext& context) -> Result<void> {
        const auto decision = context.requestConflict({{}, "existing.txt"});
        check(decision == OverwritePolicy::Ask && context.cancellation().stop_requested(),
              "conflict wait did not observe cancellation");
        return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
    };
    TaskManager tasks;
    const auto id = tasks.start(TaskType::ExtractArchive, work);
    check(id.has_value(), "conflict cancel task start failed");
    awaitConflict(tasks, *id);
    check(tasks.cancel(*id), "waiting conflict cancellation rejected");
    check(awaitTerminal(tasks, *id).state == TaskState::Cancelled && tasks.release(*id),
          "waiting conflict task did not finish cancellation");
    const auto start = std::chrono::steady_clock::now();
    {
        TaskManager shutdown;
        const auto other = shutdown.start(TaskType::ExtractArchive, work);
        check(other.has_value(), "shutdown conflict task start failed");
        awaitConflict(shutdown, *other);
    }
    check(std::chrono::steady_clock::now() - start < 2s,
          "shutdown blocked on unanswered conflict");
}

class ExtractionReader final : public ArchiveReader {
public:
    Result<ArchiveInfo> open(const std::filesystem::path& path, CancellationToken) override {
        ArchiveInfo info;
        info.path = path;
        info.formatName = "Extraction fixture";
        return info;
    }
    Result<void> enumerate(const EntryCallback&, CancellationToken) override { return {}; }
    Result<void> extract(const ExtractionRequest& request, const ProgressCallback& progress,
                         CancellationToken cancellation) override {
        check(request.selectedIndices && request.selectedIndices->size() == 1 &&
              request.selectedIndices->front() == 7, "selected identity lost in service");
        check(request.onConflict && request.onConflict({{}, request.destination / "file.txt"}) ==
              OverwritePolicy::Skip, "service did not route overwrite callback");
        for (std::uint64_t i = 1; i <= 3; ++i) {
            if (cancellation.stop_requested() ||
                !progress({"file.txt", i * 4096, i, request.totalBytes, request.totalEntries}))
                return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
            std::this_thread::sleep_for(2ms);
        }
        return {};
    }
};

void extractionServiceProgress() {
    TaskManager tasks;
    ArchiveService service(tasks, [] { return std::make_unique<ExtractionReader>(); });
    ExtractionRequest request;
    request.destination = "output";
    request.selectedIndices = std::vector<std::uint64_t>{7};
    request.totalBytes = 16384;
    request.totalEntries = 3;
    const auto id = service.extract("stream.fixture", std::move(request));
    check(id.has_value(), "extraction service start failed");
    const auto conflict = awaitConflict(tasks, *id);
    check(tasks.resolveConflict(*id, conflict.conflictId, {OverwritePolicy::Skip, false}),
          "extraction service conflict response failed");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Succeeded && snapshot.type == TaskType::ExtractArchive,
          "extraction service failed");
    check(snapshot.processedBytes == 12288 && snapshot.processedEntries == 3 &&
          snapshot.totalBytes == 16384, "actual extraction bytes confused with estimated total");
    check(snapshot.bytesPerSecond > 0 && snapshot.estimatedRemaining.has_value(),
          "extraction speed/ETA missing");
    check(tasks.takeEntries(*id).empty() && tasks.release(*id),
          "extraction generated metadata queues");
}

void cancellationDoesNotHideRecoveryFailure() {
    TaskManager tasks;
    std::atomic<bool> running{};
    const auto id = tasks.start(TaskType::ExtractArchive,
        [&](TaskContext& context) -> Result<void> {
            running = true;
            while (!context.cancellation().stop_requested())
                std::this_thread::sleep_for(1ms);
            return std::unexpected(ArchiveError{ArchiveErrorCode::CleanupFailed,
                "Cancellation could not restore an original file",
                "Recovery backup was retained", "output/recovery/backup"});
        });
    check(id.has_value(), "recovery failure task start failed");
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!running && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    check(running && tasks.cancel(*id), "recovery failure task not cancelled");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Failed && snapshot.error &&
          snapshot.error->code == ArchiveErrorCode::CleanupFailed &&
          snapshot.error->backendMessage == "Recovery backup was retained" &&
          snapshot.error->path == std::filesystem::path{"output/recovery/backup"},
          "cancellation hid recovery failure or its remaining-file location");
    check(tasks.release(*id), "recovery failure task not released");
}

struct WriterState {
    std::atomic<unsigned> destroyed{};
    std::atomic<bool> scanning{};
    std::atomic<bool> writing{};
    std::atomic<bool> proceed{};
    std::thread::id caller;
    bool waitForCancel{};
    bool commitAfterStop{};
};

class SyntheticWriter final : public ArchiveWriter {
public:
    explicit SyntheticWriter(std::shared_ptr<WriterState> state) : state_(std::move(state)) {}
    ~SyntheticWriter() override { ++state_->destroyed; }
    std::vector<ArchiveCapabilities> capabilities() const override {
        ArchiveCapabilities caps;
        caps.name = "Fixture ZIP";
        caps.extension = ".zip";
        caps.methods = {{CompressionMethod::Deflate, "Deflate", 0, 9, 6}};
        return {caps};
    }
    Result<void> create(const CompressionRequest& request,
                        const CompressionProgressCallback& progress,
                        CancellationToken cancellation) override {
        check(std::this_thread::get_id() != state_->caller, "writer ran on the submitting thread");
        check(request.destination == "created.zip" && request.inputPaths.size() == 1 &&
                  request.inputPaths.front() == "source", "compression request lost inputs/output");
        progress({CompressionPhase::Scanning, "source", 999999, 4, {}, {}});
        state_->scanning = true;
        while (!state_->proceed && !cancellation.stop_requested())
            std::this_thread::sleep_for(1ms);
        if (state_->waitForCancel) {
            while (!cancellation.stop_requested())
                std::this_thread::sleep_for(1ms);
            if (state_->commitAfterStop)
                return {}; // Models a stop racing an already completed atomic install.
            return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled,
                "Creation cancelled", {}, request.destination});
        }
        for (std::uint64_t i = 1; i <= 3; ++i) {
            if (!progress({CompressionPhase::Writing, "source/file", i * 4096, i, 12288, 3}))
                return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled,
                    "Creation cancelled", {}, request.destination});
            state_->writing = true;
            std::this_thread::sleep_for(2ms);
        }
        progress({CompressionPhase::Finalizing, "", 12288, 3, 12288, 3});
        return {};
    }
private:
    std::shared_ptr<WriterState> state_;
};

void awaitScan(const std::shared_ptr<WriterState>& state) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!state->scanning && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    check(state->scanning, "compression worker never reached scanning");
}

CompressionRequest syntheticCompressionRequest() {
    CompressionRequest request;
    request.destination = "created.zip";
    request.inputPaths = {"source"};
    return request;
}

void compressionServicePhasesAndProgress() {
    TaskManager tasks;
    auto state = std::make_shared<WriterState>();
    state->caller = std::this_thread::get_id();
    ArchiveService service(tasks, {}, [state] { return std::make_unique<SyntheticWriter>(state); });
    const auto capabilities = service.compressionCapabilities();
    check(capabilities.size() == 1 && capabilities.front().methods.front().maxLevel == 9,
          "application lost writer capabilities");
    const auto id = service.compress(syntheticCompressionRequest());
    check(id.has_value(), "compression service submission failed");
    awaitScan(state);
    const auto scanning = tasks.snapshot(*id);
    check(scanning && scanning->type == TaskType::CompressArchive &&
              scanning->compressionPhase == CompressionPhase::Scanning &&
              scanning->processedEntries == 4 && scanning->processedBytes == 0 &&
              !scanning->totalBytes && scanning->bytesPerSecond == 0 &&
              !scanning->estimatedRemaining, "scan metadata fabricated byte progress/throughput");
    state->proceed = true;
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Succeeded && snapshot.processedBytes == 12288 &&
              snapshot.totalBytes == 12288 && snapshot.processedEntries == 3 &&
              snapshot.compressionPhase == CompressionPhase::Finalizing &&
              snapshot.bytesPerSecond > 0, "compression progress or completion missing");
    check(tasks.takeEntries(*id).empty() && tasks.release(*id),
          "compression leaked metadata queue or task");
    check(state->destroyed == 2, "capability or task writer leaked");
}

void compressionCancellationAndShutdown() {
    auto state = std::make_shared<WriterState>();
    state->caller = std::this_thread::get_id();
    state->waitForCancel = true;
    {
        TaskManager tasks;
        ArchiveService service(tasks, {}, [state] { return std::make_unique<SyntheticWriter>(state); });
        const auto id = service.compress(syntheticCompressionRequest());
        check(id.has_value(), "compression cancel submission failed");
        awaitScan(state);
        check(tasks.cancel(*id), "compression cancellation rejected");
        check(awaitTerminal(tasks, *id).state == TaskState::Cancelled && tasks.release(*id),
              "compression worker did not cancel/release");
    }
    state->scanning = false;
    const auto start = std::chrono::steady_clock::now();
    {
        TaskManager tasks;
        ArchiveService service(tasks, {}, [state] { return std::make_unique<SyntheticWriter>(state); });
        check(service.compress(syntheticCompressionRequest()).has_value(), "shutdown submit failed");
        awaitScan(state);
    }
    check(std::chrono::steady_clock::now() - start < 2s && state->destroyed == 2,
          "compression shutdown hung or leaked writer");
}

void committedCompressionWinsLateStop() {
    TaskManager tasks;
    auto state = std::make_shared<WriterState>();
    state->caller = std::this_thread::get_id();
    state->waitForCancel = true;
    state->commitAfterStop = true;
    ArchiveService service(tasks, {}, [state] { return std::make_unique<SyntheticWriter>(state); });
    const auto id = service.compress(syntheticCompressionRequest());
    check(id.has_value(), "commit race submission failed");
    awaitScan(state);
    check(tasks.cancel(*id), "commit race stop request rejected");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Succeeded && !snapshot.error && tasks.release(*id),
          "installed archive was incorrectly reported as cancelled");
}

void compressionFactoryFailure() {
    TaskManager tasks;
    ArchiveService service(tasks, {}, [] { return std::unique_ptr<ArchiveWriter>{}; });
    check(service.compressionCapabilities().empty(), "null writer fabricated capabilities");
    const auto id = service.compress(syntheticCompressionRequest());
    check(id.has_value(), "null writer submission failed");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Failed && snapshot.error &&
              snapshot.error->code == ArchiveErrorCode::BackendError &&
              snapshot.error->path == "created.zip" && tasks.release(*id),
          "null writer did not produce structured task error");
}

void realCompressionService() {
    Fixture fixture;
    const auto source = fixture.root / std::filesystem::path{std::u8string{u8"安安🐈.txt"}};
    std::ofstream(source, std::ios::binary) << "application round trip";
    TaskManager tasks;
    ArchiveService service(tasks);
    CompressionRequest request;
    request.inputPaths = {source};
    request.destination = fixture.root / "created.zip";
    const auto destination = request.destination;
    const auto id = service.compress(std::move(request));
    check(id.has_value(), "real compression task submission failed");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Succeeded && snapshot.totalBytes == 22 &&
              snapshot.processedBytes == 22 && snapshot.processedEntries == 1,
          "real compression task failed or counters incorrect");
    auto reader = makeLibArchiveReader();
    check(reader->open(destination).has_value(), "created archive could not open");
    std::vector<ArchiveEntry> entries;
    check(reader->enumerate([&](const ArchiveEntry& entry) {
        entries.push_back(entry); return true;
    }).has_value(), "created archive could not enumerate");
    check(entries.size() == 1 && entries.front().size == 22,
          "created application archive lost metadata");
    ExtractionRequest extraction;
    extraction.destination = fixture.root / "unpacked";
    check(reader->extract(extraction).has_value(), "application archive failed extraction");
    std::ifstream restored(extraction.destination / source.filename(), std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>{restored}, {}};
    check(contents == "application round trip" && tasks.release(*id),
          "application create/read/extract roundtrip corrupted contents");
}
} // namespace

void contentProbeDoesNotEnumerate() {
    auto state = std::make_shared<ReaderState>();
    TaskManager tasks;
    ArchiveService service(tasks, [state] { return std::make_unique<SyntheticReader>(state); });
    const auto id = service.probe("content-with-unrelated-extension.xxx");
    check(id.has_value(), "probe task was not submitted");
    const auto snapshot = awaitTerminal(tasks, *id);
    check(snapshot.state == TaskState::Succeeded && snapshot.archive.has_value(), "content probe failed");
    check(state->enumerated == 0 && snapshot.processedEntries == 0, "content probe enumerated payloads");
    check(tasks.release(*id), "probe task was not released");
}

int main() {
    std::cout << std::unitbuf;
    const std::pair<const char*, void (*)()> tests[] = {
        {"content probe without metadata enumeration", contentProbeDoesNotEnumerate},
        {"streaming metadata batches", streamingBatches},
        {"unknown stream size", unknownSizeRemainsUnknown},
        {"bounded queue cancellation", boundedQueueAndCancellation},
        {"shutdown wakes blocked producer", shutdownWakesProducer},
        {"structured archive failure", structuredFailure},
        {"worker exception boundary", exceptionBoundary},
        {"managed task concurrency limit", taskLimits},
        {"reentrant cancellation inspection", cancellationCallbackCanInspectManager},
        {"cancellation versus completion race", acceptedCancellationWinsCompletionRace},
        {"lazy unicode directory children", immediateDirectoryChildren},
        {"directory error and cancellation", directoryErrorsAndCancellation},
        {"overwrite conflict and apply to all", conflictRoundTripAndApplyAll},
        {"cancel and shutdown wake conflict", cancellationAndShutdownWakeConflict},
        {"extraction service actual bytes", extractionServiceProgress},
        {"cancellation retains recovery failure", cancellationDoesNotHideRecoveryFailure},
        {"compression phases and real byte progress", compressionServicePhasesAndProgress},
        {"compression cancellation and shutdown", compressionCancellationAndShutdown},
        {"committed compression versus late stop", committedCompressionWinsLateStop},
        {"structured compression factory failure", compressionFactoryFailure},
        {"real application compression round trip", realCompressionService},
    };
    unsigned failed = 0;
    for (const auto& [name, run] : tests) {
        try {
            run();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << std::size(tests) << " tests, " << failed << " failures\n";
    return failed ? 1 : 0;
}
