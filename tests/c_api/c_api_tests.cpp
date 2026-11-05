#include <ananarchiver/c_api/ananarchiver.h>

#include "../security/extraction_fixtures.h"

#include <atomic>
#include <thread>

using namespace extraction_tests;
using namespace std::chrono_literals;

namespace {
aa_string input(std::string_view text) { return {text.data(), text.size()}; }
std::string output(aa_string text) {
    return text.size ? std::string(text.data, static_cast<size_t>(text.size)) : std::string{};
}
struct Engine {
    aa_engine* handle{};
    Engine() {
        check(aa_engine_create(AA_ABI_VERSION, &handle, nullptr) == AA_OK, "create engine");
    }
    ~Engine() { aa_engine_destroy(handle); }
};
using Snapshot = std::unique_ptr<const aa_task_snapshot, decltype(&aa_snapshot_release)>;
using Batch = std::unique_ptr<const aa_entry_batch, decltype(&aa_entry_batch_release)>;
Snapshot poll(aa_engine* engine, aa_task_id id) {
    const aa_task_snapshot* result = nullptr;
    check(aa_task_poll(engine, id, &result, nullptr) == AA_OK && result, "poll task");
    return {result, aa_snapshot_release};
}
Batch take(aa_engine* engine, aa_task_id id) {
    const aa_entry_batch* result = nullptr;
    check(aa_task_take_entries(engine, id, &result, nullptr) == AA_OK && result, "take batch");
    check(result->count <= AA_MAX_BATCH_ENTRIES, "unbounded ABI batch");
    return {result, aa_entry_batch_release};
}
bool terminal(uint32_t state) {
    return state == AA_TASK_SUCCEEDED || state == AA_TASK_FAILED || state == AA_TASK_CANCELLED;
}
Snapshot await(aa_engine* engine, aa_task_id id,
               std::vector<std::pair<uint64_t, std::string>>* rows = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (;;) {
        if (rows) {
            auto batch = take(engine, id);
            for (uint32_t i = 0; i < batch->count; ++i)
                rows->emplace_back(batch->entries[i].index, output(batch->entries[i].path));
        }
        auto snapshot = poll(engine, id);
        if (terminal(snapshot->state)) {
            if (rows) {
                for (;;) {
                    auto batch = take(engine, id);
                    if (!batch->count)
                        break;
                    for (uint32_t i = 0; i < batch->count; ++i)
                        rows->emplace_back(batch->entries[i].index, output(batch->entries[i].path));
                }
            }
            return snapshot;
        }
        check(std::chrono::steady_clock::now() < deadline, "task deadline");
        std::this_thread::sleep_for(1ms);
    }
}
aa_task_id open(aa_engine* engine, const std::string& path, bool probe = false) {
    aa_open_request request{};
    request.struct_size = sizeof(request);
    request.path = input(path);
    request.probe_only = probe;
    aa_task_id id = 0;
    check(aa_archive_open(engine, &request, &id, nullptr) == AA_OK && id, "submit open");
    return id;
}
aa_task_id extract(aa_engine* engine, aa_extract_request request) {
    request.struct_size = sizeof(request);
    aa_task_id id = 0;
    check(aa_archive_extract(engine, &request, &id, nullptr) == AA_OK && id, "submit extraction");
    return id;
}

void checkedBoundary() {
    Engine engine;
    aa_task_id id = 99;
    const aa_error* error = nullptr;
    aa_open_request request{};
    check(aa_archive_open(engine.handle, &request, &id, &error) == AA_INVALID_ARGUMENT,
          "reject short struct");
    check(id == 0 && error && error->message.size, "structured argument error");
    aa_error_release(error);
    request.struct_size = sizeof(request);
    const std::string badUtf8("\xC0\xAF", 2);
    request.path = input(badUtf8);
    check(aa_archive_open(engine.handle, &request, &id, &error) == AA_INVALID_ARGUMENT,
          "reject malformed Unicode");
    check(error && error->code == AA_ERROR_INVALID_PATH_ENCODING, "encoding error code");
    aa_error_release(error);
    const std::string nul("some\0path", 9);
    request.path = input(nul);
    check(aa_archive_open(engine.handle, &request, &id, nullptr) == AA_INVALID_ARGUMENT,
          "reject NUL path");
    request.path = {nullptr, 1};
    check(aa_archive_open(engine.handle, &request, &id, nullptr) == AA_INVALID_ARGUMENT,
          "reject null nonempty string");
    request.path = {nullptr, UINT64_MAX};
    check(aa_archive_open(engine.handle, &request, &id, nullptr) == AA_INVALID_ARGUMENT,
          "reject size overflow before dereference");
    const aa_task_snapshot* snapshot = reinterpret_cast<const aa_task_snapshot*>(1);
    check(aa_task_poll(engine.handle, UINT64_MAX, &snapshot, nullptr) == AA_NOT_FOUND && !snapshot,
          "stale ID and cleared output");
    check(aa_task_poll(engine.handle, 1, nullptr, nullptr) == AA_INVALID_ARGUMENT,
          "missing output");
    struct ExtendedOpen {
        aa_open_request base;
        uint64_t future_field;
    } extended{};
    extended.base.struct_size = sizeof(extended);
    extended.base.path = input("missing-fixture.zip");
    extended.future_field = UINT64_MAX;
    check(aa_archive_open(engine.handle, &extended.base, &id, nullptr) == AA_OK,
          "accept extensible request suffix");
    check(await(engine.handle, id)->state == AA_TASK_FAILED, "extended request reaches worker");
    check(aa_task_release(engine.handle, id) == AA_OK, "release extended request");
    aa_extract_request badSelection{};
    badSelection.struct_size = sizeof(badSelection);
    badSelection.selected_index_count = UINT64_MAX;
    check(aa_archive_extract(engine.handle, &badSelection, &id, nullptr) == AA_INVALID_ARGUMENT,
          "reject overflowing array before dereference");
    check(aa_engine_shutdown(engine.handle) == AA_OK, "request shutdown");
    request.path = input("missing.zip");
    check(aa_archive_open(engine.handle, &request, &id, nullptr) == AA_SHUTTING_DOWN && id == 0,
          "closed engine rejects new work");
}

void contentDetectionAndOwnership() {
    TemporaryDirectory fixture;
    const auto archive = fixture.path / pathFromUtf8("安安🐈.unknown");
    std::vector<FixtureEntry> entries;
    entries.emplace_back("同名.txt", "first");
    entries.emplace_back("同名.txt", "second");
    for (int i = 0; i < 1300; ++i)
        entries.emplace_back("folder/file-" + std::to_string(i), "payload");
    createFixture(archive, FixtureFormat::Tar, entries);
    Engine engine;
    const auto path = pathToUtf8(archive);
    const auto probeId = open(engine.handle, path, true);
    const auto probe = await(engine.handle, probeId);
    check(probe->state == AA_TASK_SUCCEEDED && (probe->flags & AA_HAS_ARCHIVE), "probe content");
    check(probe->processed_entries == 0 && take(engine.handle, probeId)->count == 0,
          "probe did not enumerate");
    check(aa_task_release(engine.handle, probeId) == AA_OK, "release probe");
    const auto id = open(engine.handle, path);
    std::vector<std::pair<uint64_t, std::string>> rows;
    auto snapshot = await(engine.handle, id, &rows);
    check(snapshot->state == AA_TASK_SUCCEEDED && rows.size() == entries.size(),
          "stream real metadata");
    check(rows[0].first == 0 && rows[1].first == 1 && rows[0].second == rows[1].second,
          "duplicate names lost original ordinals");
    check(snapshot->processed_entries == entries.size() && snapshot->bytes_per_second == 0,
          "index counters and honest throughput");
    check(aa_task_release(engine.handle, id) == AA_OK, "release open");
    check(aa_task_cancel(engine.handle, id) == AA_NOT_FOUND, "released ID invalid immediately");
    aa_engine_destroy(engine.handle);
    engine.handle = nullptr;
    check(snapshot->total_entries == entries.size() && !output(snapshot->format_name).empty(),
          "snapshot storage survives engine");
}

void errorSnapshots() {
    TemporaryDirectory fixture;
    Engine engine;
    const auto missing = pathToUtf8(fixture.path / "missing.zip");
    auto snapshot = await(engine.handle, open(engine.handle, missing));
    check(snapshot->state == AA_TASK_FAILED && snapshot->error &&
              snapshot->error->code == AA_ERROR_FILE_NOT_FOUND,
          "missing file error");
    check(output(snapshot->error->path) == missing, "error owns UTF-8 path");
    const auto invalidArchive = fixture.path / "invalid.zip";
    std::ofstream(invalidArchive) << "This is not an archive";
    auto failed = await(engine.handle, open(engine.handle, pathToUtf8(invalidArchive)));
    check(failed->state == AA_TASK_FAILED && failed->error, "invalid archive is not success");
    aa_engine_destroy(engine.handle);
    engine.handle = nullptr;
    check(output(snapshot->error->path) == missing, "nested errors survive engine destruction");
}

void blockedProducerCancellation() {
    TemporaryDirectory fixture;
    const auto archive = fixture.path / "many.tar";
    std::vector<FixtureEntry> entries;
    for (int i = 0; i < 6000; ++i)
        entries.emplace_back("file-" + std::to_string(i));
    createFixture(archive, FixtureFormat::Tar, entries);
    Engine engine;
    const auto id = open(engine.handle, pathToUtf8(archive));
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (poll(engine.handle, id)->processed_entries < 4608) {
        check(std::chrono::steady_clock::now() < deadline, "backpressure deadline");
        std::this_thread::sleep_for(1ms);
    }
    check(aa_task_cancel(engine.handle, id) == AA_OK, "cancel blocked metadata worker");
    auto cancelled = await(engine.handle, id);
    check(cancelled->state == AA_TASK_CANCELLED && take(engine.handle, id)->count == 0,
          "cancel drains stale metadata");
    const auto blockedId = open(engine.handle, pathToUtf8(archive));
    check(aa_task_release(engine.handle, blockedId) == AA_OK, "release active task asynchronously");
    check(aa_task_cancel(engine.handle, blockedId) == AA_NOT_FOUND, "released active ID is stale");
    open(engine.handle, pathToUtf8(archive));
    check(aa_engine_shutdown(engine.handle) == AA_OK, "shutdown blocked producer");
    uint32_t stopped = 0;
    const auto shutdownDeadline = std::chrono::steady_clock::now() + 5s;
    while (!stopped) {
        check(aa_engine_is_stopped(engine.handle, &stopped) == AA_OK, "poll shutdown");
        check(std::chrono::steady_clock::now() < shutdownDeadline, "shutdown deadline");
        std::this_thread::sleep_for(1ms);
    }
}

void extractionConflictAndSelection() {
    TemporaryDirectory fixture;
    const auto archive = fixture.path / "duplicate.tar";
    createFixture(archive, FixtureFormat::Tar,
                  {{"same.txt", "first"}, {"same.txt", "second"}, {"folder/child.txt", "nested"}});
    const auto archivePath = pathToUtf8(archive);
    const auto destinationPath = pathToUtf8(fixture.path / "out");
    Engine engine;
    const uint64_t selected = 1;
    aa_extract_request request{};
    request.path = input(archivePath);
    request.destination = input(destinationPath);
    request.selected_indices = &selected;
    request.selected_index_count = 1;
    auto snapshot = await(engine.handle, extract(engine.handle, request));
    check(snapshot->state == AA_TASK_SUCCEEDED, "extract selected duplicate ordinal");
    check(readFile(fixture.path / "out/same.txt") == "second", "correct duplicate extracted");

    const auto nonePath = pathToUtf8(fixture.path / "none");
    aa_extract_request none{};
    none.path = input(archivePath);
    none.destination = input(nonePath);
    check(await(engine.handle, extract(engine.handle, none))->state == AA_TASK_SUCCEEDED,
          "empty selection is valid");
    check(!std::filesystem::exists(fixture.path / "none/same.txt"),
          "empty selection never becomes extract-all");
    const auto directoryPath = pathToUtf8(fixture.path / "directory");
    const aa_string directories[] = {input("folder")};
    none.destination = input(directoryPath);
    none.selected_directories = directories;
    none.selected_directory_count = 1;
    check(await(engine.handle, extract(engine.handle, none))->state == AA_TASK_SUCCEEDED,
          "select implicit directory");
    check(readFile(fixture.path / "directory/folder/child.txt") == "nested" &&
              !std::filesystem::exists(fixture.path / "directory/same.txt"),
          "directory selection preserves boundaries");

    const auto id = extract(engine.handle, request);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    Snapshot conflict(nullptr, aa_snapshot_release);
    for (;;) {
        conflict = poll(engine.handle, id);
        if (conflict->flags & AA_HAS_CONFLICT)
            break;
        check(!terminal(conflict->state) && std::chrono::steady_clock::now() < deadline,
              "wait overwrite conflict");
        std::this_thread::sleep_for(1ms);
    }
    check(output(conflict->conflict_entry.path) == "same.txt", "conflict entry owned");
    check(aa_task_resolve_conflict(engine.handle, id, conflict->conflict_id + 1,
                                   AA_OVERWRITE_REPLACE, 0) == AA_BUSY,
          "reject stale conflict ID");
    check(aa_task_resolve_conflict(engine.handle, id, conflict->conflict_id, AA_OVERWRITE_ASK, 0) ==
              AA_INVALID_ARGUMENT,
          "reject nonfinal answer");
    check(aa_task_resolve_conflict(engine.handle, id, conflict->conflict_id, AA_OVERWRITE_SKIP,
                                   1) == AA_OK,
          "answer conflict");
    check(await(engine.handle, id)->state == AA_TASK_SUCCEEDED, "answered extraction finishes");
    check(aa_task_resolve_conflict(engine.handle, id, conflict->conflict_id, AA_OVERWRITE_SKIP,
                                   1) == AA_BUSY,
          "reject repeated answer");

    const auto waiting = extract(engine.handle, request);
    while (!(poll(engine.handle, waiting)->flags & AA_HAS_CONFLICT)) {
        check(std::chrono::steady_clock::now() < deadline, "second conflict deadline");
        std::this_thread::sleep_for(1ms);
    }
    check(aa_engine_shutdown(engine.handle) == AA_OK, "shutdown while worker awaits answer");
    aa_engine_destroy(engine.handle);
    engine.handle = nullptr;
    check(!output(conflict->conflict_destination).empty(), "conflict data survives shutdown");
}

void encryptedAndUnsafeExtraction() {
    TemporaryDirectory fixture;
    Engine engine;
    const auto encrypted = fixture.path / "encrypted.zip";
    createFixture(encrypted, FixtureFormat::Zip, {{"secret.txt", "secret"}}, true);
    const auto path = pathToUtf8(encrypted);
    const auto destination = pathToUtf8(fixture.path / "decrypted");
    aa_extract_request request{};
    request.path = input(path);
    request.destination = input(destination);
    request.extract_all = 1;
    auto missingPassword = await(engine.handle, extract(engine.handle, request));
    check(missingPassword->state == AA_TASK_FAILED && missingPassword->error &&
              missingPassword->error->code == AA_ERROR_PASSWORD_REQUIRED,
          "password required");
    request.password = input("wrong-test-password");
    auto wrongPassword = await(engine.handle, extract(engine.handle, request));
    check(wrongPassword->state == AA_TASK_FAILED && wrongPassword->error &&
              wrongPassword->error->code == AA_ERROR_WRONG_PASSWORD,
          "wrong password");
    check(output(wrongPassword->error->message).find("wrong-test-password") == std::string::npos &&
              output(wrongPassword->error->backend_message).find("wrong-test-password") ==
                  std::string::npos,
          "password absent from diagnostics");
    std::string password = "test-only-passphrase";
    request.password = input(password);
    const auto id = extract(engine.handle, request);
    password.assign(password.size(), 'x'); // submission must own its input now
    check(await(engine.handle, id)->state == AA_TASK_SUCCEEDED, "owned password works");
    check(readFile(fixture.path / "decrypted/secret.txt") == "secret", "real decrypted payload");
    const auto malicious = fixture.path / "unsafe.tar";
    createFixture(malicious, FixtureFormat::Tar, {{"../escape.txt", "unsafe"}});
    const auto maliciousPath = pathToUtf8(malicious);
    request.path = input(maliciousPath);
    request.password = {};
    auto unsafe = await(engine.handle, extract(engine.handle, request));
    check(unsafe->state == AA_TASK_FAILED && unsafe->error &&
              unsafe->error->code == AA_ERROR_UNSAFE_PATH,
          "path traversal blocked through ABI");
    check(!std::filesystem::exists(fixture.path / "escape.txt"), "no escaped output");
}

void creationRoundTrip() {
    TemporaryDirectory fixture;
    const auto source = fixture.path / pathFromUtf8("输入.txt");
    std::ofstream(source) << "real archive creation";
    const auto sourcePath = pathToUtf8(source);
    const auto archivePath = pathToUtf8(fixture.path / "created.zip");
    const aa_string inputs[] = {input(sourcePath)};
    Engine engine;
    const aa_capability_batch* capabilities = nullptr;
    check(aa_compression_capabilities(engine.handle, &capabilities, nullptr) == AA_OK,
          "writer capabilities");
    std::unique_ptr<const aa_capability_batch, decltype(&aa_capability_batch_release)> held(
        capabilities, aa_capability_batch_release);
    check(capabilities->count != 0 && capabilities->count < 64,
          "bounded actual writer capabilities");
    aa_compress_request request{};
    request.struct_size = sizeof(request);
    request.destination = input(archivePath);
    request.input_paths = inputs;
    request.input_path_count = 1;
    request.format = AA_FORMAT_ZIP;
    request.method = AA_METHOD_DEFLATE;
    request.level = 6;
    request.preserve_timestamps = 1;
    aa_task_id id = 0;
    check(aa_archive_compress(engine.handle, &request, &id, nullptr) == AA_OK, "submit writer");
    auto created = await(engine.handle, id);
    check(created->state == AA_TASK_SUCCEEDED && created->type == AA_TASK_COMPRESS,
          "real writer completion");
    check(created->processed_bytes == 21, "actual writer byte count");
    const auto destination = pathToUtf8(fixture.path / "roundtrip");
    aa_extract_request extraction{};
    extraction.path = input(archivePath);
    extraction.destination = input(destination);
    extraction.extract_all = 1;
    check(await(engine.handle, extract(engine.handle, extraction))->state == AA_TASK_SUCCEEDED,
          "extract created archive");
    check(readFile(fixture.path / "roundtrip" / source.filename()) == "real archive creation",
          "round trip payload");
}

void concurrentPollAndRelease() {
    Engine engine;
    std::atomic<bool> done{};
    const auto id = open(engine.handle, "a-missing-fixture.zip");
    std::jthread observer([&] {
        while (!done.load()) {
            const aa_task_snapshot* result = nullptr;
            const auto status = aa_task_poll(engine.handle, id, &result, nullptr);
            check(status == AA_OK || status == AA_NOT_FOUND, "concurrent poll status");
            aa_snapshot_release(result);
        }
    });
    check(aa_task_release(engine.handle, id) == AA_OK, "concurrent release");
    done = true;
    observer.join();
    for (int i = 0; i < 100; ++i) {
        aa_open_request request{};
        request.struct_size = sizeof(request);
        request.path = input("a-missing-fixture.zip");
        aa_task_id current = 0;
        const auto status = aa_archive_open(engine.handle, &request, &current, nullptr);
        check(status == AA_OK || status == AA_RESOURCE_LIMIT, "submission/reclamation race");
        if (status == AA_OK)
            check(aa_task_release(engine.handle, current) == AA_OK, "rapid release");
        std::this_thread::sleep_for(1ms);
    }
}
} // namespace

int main() {
    Tests tests;
    tests.run("checked C ABI inputs and versioned structs", checkedBoundary);
    tests.run("content detection, bounded metadata and result ownership",
              contentDetectionAndOwnership);
    tests.run("structured asynchronous errors", errorSnapshots);
    tests.run("backpressure cancellation and asynchronous shutdown", blockedProducerCancellation);
    tests.run("duplicate selection, conflict IDs and shutdown while waiting",
              extractionConflictAndSelection);
    tests.run("password ownership and extraction security", encryptedAndUnsafeExtraction);
    tests.run("capabilities and real creation/extraction round trip", creationRoundTrip);
    tests.run("concurrent polling and asynchronous reclamation", concurrentPollAndRelease);
    return tests.finish();
}
