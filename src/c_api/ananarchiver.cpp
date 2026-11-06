#include "engine.h"
#include "results.h"

#include <ananarchiver/password.h>
#include <ananarchiver/unicode.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string_view>

using namespace ananarchiver;
using namespace ananarchiver::c_api;

namespace {
struct BoundaryFailure {
    aa_status status;
    ArchiveError error;
};

[[noreturn]] void invalid(const char* message) {
    throw BoundaryFailure{AA_INVALID_ARGUMENT,
                          {ArchiveErrorCode::UnsupportedOperation, message, {}, {}}};
}

template <class F> aa_status boundary(const aa_error** error, F&& operation) noexcept {
    if (error)
        *error = nullptr;
    try {
        return operation();
    } catch (const BoundaryFailure& failure) {
        try {
            if (error)
                *error = new ErrorResult(failure.error);
        } catch (...) {
            // The numeric status remains authoritative if diagnostics cannot
            // be allocated. No exception, including bad_alloc, crosses C ABI.
        }
        return failure.status;
    } catch (const std::filesystem::filesystem_error& failure) {
        try {
            if (error)
                *error = new ErrorResult({ArchiveErrorCode::IoError,
                                          "Private export filesystem operation failed",
                                          failure.code().message(), failure.path1()});
        } catch (...) { /* Numeric status remains authoritative on allocation failure. */
        }
        return AA_ARCHIVE_ERROR;
    } catch (const std::bad_alloc&) {
        return AA_RESOURCE_LIMIT;
    } catch (...) {
        return AA_INTERNAL_ERROR;
    }
}

template <class T> void requestSize(const T* request) {
    if (!request || request->struct_size < sizeof(T))
        invalid("Missing request or unsupported structure size");
}

std::string_view textInput(aa_string text) {
    if (text.size > AA_MAX_STRING_BYTES || (text.size != 0 && !text.data))
        invalid("UTF-8 input is missing or exceeds the byte limit");
    const auto result = text.size == 0
                            ? std::string_view{}
                            : std::string_view(text.data, static_cast<size_t>(text.size));
    if (result.find('\0') != std::string_view::npos)
        invalid("Embedded NUL bytes are not supported");
    if (!isValidUtf8(result))
        throw BoundaryFailure{
            AA_INVALID_ARGUMENT,
            {ArchiveErrorCode::InvalidPathEncoding, "Input is not valid UTF-8", {}, {}}};
    return result;
}

std::filesystem::path pathInput(aa_string text) {
    const auto utf8 = textInput(text);
    if (utf8.empty())
        invalid("A nonempty UTF-8 path is required");
    return pathFromUtf8(utf8);
}

template <class T> void arrayInput(const T* data, uint64_t count) {
    if (count > AA_MAX_REQUEST_ITEMS || (count != 0 && !data))
        invalid("Input array is missing or exceeds the item limit");
}

struct Secret {
    std::string text;
    explicit Secret(aa_string input) : text(textInput(input)) {}
    ~Secret() { scrubPassword(text); }
};

OverwritePolicy overwriteInput(uint32_t value) {
    switch (value) {
    case AA_OVERWRITE_ASK:
        return OverwritePolicy::Ask;
    case AA_OVERWRITE_REPLACE:
        return OverwritePolicy::Overwrite;
    case AA_OVERWRITE_SKIP:
        return OverwritePolicy::Skip;
    case AA_OVERWRITE_RENAME:
        return OverwritePolicy::Rename;
    case AA_OVERWRITE_IF_NEWER:
        return OverwritePolicy::OverwriteIfNewer;
    default:
        invalid("Unknown overwrite policy");
    }
}

LinkPolicy linkInput(uint32_t value) {
    switch (value) {
    case AA_LINK_SAFE:
        return LinkPolicy::Safe;
    case AA_LINK_PRESERVE:
        return LinkPolicy::Preserve;
    case AA_LINK_SKIP:
        return LinkPolicy::Skip;
    default:
        invalid("Unknown link policy");
    }
}

CompressionFormat formatInput(uint32_t value) {
    switch (value) {
    case AA_FORMAT_ZIP:
        return CompressionFormat::Zip;
    case AA_FORMAT_7Z:
        return CompressionFormat::SevenZip;
    case AA_FORMAT_TAR:
        return CompressionFormat::Tar;
    case AA_FORMAT_TAR_GZIP:
        return CompressionFormat::TarGzip;
    case AA_FORMAT_TAR_BZIP2:
        return CompressionFormat::TarBzip2;
    case AA_FORMAT_TAR_XZ:
        return CompressionFormat::TarXz;
    case AA_FORMAT_TAR_ZSTD:
        return CompressionFormat::TarZstd;
    case AA_FORMAT_TAR_LZ4:
        return CompressionFormat::TarLz4;
    default:
        invalid("Unknown compression format");
    }
}

CompressionMethod methodInput(uint32_t value) {
    switch (value) {
    case AA_METHOD_STORE:
        return CompressionMethod::Store;
    case AA_METHOD_DEFLATE:
        return CompressionMethod::Deflate;
    case AA_METHOD_LZMA2:
        return CompressionMethod::Lzma2;
    case AA_METHOD_GZIP:
        return CompressionMethod::Gzip;
    case AA_METHOD_BZIP2:
        return CompressionMethod::Bzip2;
    case AA_METHOD_XZ:
        return CompressionMethod::Xz;
    case AA_METHOD_ZSTD:
        return CompressionMethod::Zstd;
    case AA_METHOD_LZ4:
        return CompressionMethod::Lz4;
    default:
        invalid("Unknown compression method");
    }
}

template <class F> aa_status withEngine(aa_engine* engine, F&& operation) {
    if (!engine)
        invalid("Engine is required");
    std::lock_guard lock(engine->mutex);
    if (engine->closing)
        return AA_SHUTTING_DOWN;
    return operation();
}

aa_status submitted(aa_engine& engine, Result<application::TaskId> result, aa_task_id* task) {
    if (!result) {
        const auto status = result.error().code == ArchiveErrorCode::ResourceLimitExceeded
                                ? AA_RESOURCE_LIMIT
                                : AA_ARCHIVE_ERROR;
        throw BoundaryFailure{status, std::move(result.error())};
    }
    engine.track(*result);
    *task = *result;
    return AA_OK;
}

template <class F> aa_status submit(aa_engine& engine, aa_task_id* task, F&& operation) {
    // A completed TaskManager release can race reacquiring the engine lock in
    // the cleaner. Reserve capacity in both layers before launching work.
    if (std::ranges::none_of(engine.slots,
                             [](const aa_engine::Slot& slot) { return slot.id == 0; }))
        return AA_RESOURCE_LIMIT;
    return submitted(engine, operation(), task);
}
struct BrowserResult final : aa_browser_result {
    std::shared_ptr<const application::BrowserTaskResult> source;
    std::vector<aa_browser_row> values;
    explicit BrowserResult(std::shared_ptr<const application::BrowserTaskResult> result)
        : aa_browser_result{}, source(std::move(result)) {
        static_assert(sizeof(aa_browser_result) <= 128);
        static_assert(sizeof(aa_browser_row) <= application::MetadataIndex::rowWireBytes);
        struct_size = sizeof(aa_browser_result);
        archive = source->archive;
        query = source->query;
        selection = source->selection;
        entry_count = source->entryCount;
        folder_count = source->folderCount;
        index_bytes = source->indexBytes;
        offset = source->page.offset;
        total_rows = source->page.total;
        transfer_bytes = source->page.bytes;
        values.reserve(source->page.rows.size());
        for (const auto& row : source->page.rows) {
            aa_browser_row value{};
            value.struct_size = sizeof(value);
            value.is_folder = row.folder;
            value.identity = row.identity;
            value.name = view(row.name);
            // No per-field FFI calls; populate one C record from owned native metadata.
            value.entry = entryValue(row.entry);
            values.push_back(value);
        }
        count = static_cast<uint32_t>(values.size());
        rows = values.data();
    }
};
struct ExportResult final : aa_export_result {
    std::vector<std::string> text;
    std::vector<aa_string> values;
    std::vector<std::unique_ptr<ErrorResult>> ownedErrors;
    std::vector<const aa_error*> errorPointers;
    ExportResult() : aa_export_result{} { struct_size = sizeof(aa_export_result); }
    void finish() {
        for (const auto& path : text)
            values.push_back(view(path));
        count = static_cast<uint32_t>(values.size());
        paths = values.data();
        for (const auto& error : ownedErrors)
            errorPointers.push_back(error.get());
        error_count = static_cast<uint32_t>(errorPointers.size());
        errors = errorPointers.data();
    }
};
} // namespace

extern "C" {
aa_status AA_CALL aa_get_api_info(aa_api_info* info) noexcept {
    if (!info || info->struct_size < sizeof(aa_api_info))
        return AA_INVALID_ARGUMENT;
    *info = {sizeof(aa_api_info),
             AA_ABI_VERSION,
             AA_FEATURE_OPEN | AA_FEATURE_EXTRACT | AA_FEATURE_COMPRESS |
                 AA_FEATURE_METADATA_STREAM | AA_FEATURE_ASYNC_CLEANUP | AA_FEATURE_BROWSER |
                 AA_FEATURE_DESKTOP_EXPORT,
             AA_MAX_BATCH_ENTRIES,
             AA_MAX_STRING_BYTES,
             2,
             16};
    return AA_OK;
}

aa_status AA_CALL aa_engine_create(uint32_t version, aa_engine** engine,
                                   const aa_error** error) noexcept {
    if (engine)
        *engine = nullptr;
    return boundary(error, [&]() -> aa_status {
        if (!engine)
            invalid("Engine output is required");
        if (version != AA_ABI_VERSION)
            return AA_ABI_MISMATCH;
        *engine = new aa_engine;
        return AA_OK;
    });
}

aa_status AA_CALL aa_engine_shutdown(aa_engine* engine) noexcept {
    return boundary(nullptr, [&]() -> aa_status {
        if (!engine)
            invalid("Engine is required");
        engine->shutdown();
        return AA_OK;
    });
}
aa_status AA_CALL aa_engine_is_stopped(aa_engine* engine, uint32_t* stopped) noexcept {
    if (stopped)
        *stopped = 0;
    return boundary(nullptr, [&]() -> aa_status {
        if (!engine || !stopped)
            invalid("Engine and stopped output are required");
        std::lock_guard lock(engine->mutex);
        *stopped = engine->stopped;
        return AA_OK;
    });
}
void AA_CALL aa_engine_destroy(aa_engine* engine) noexcept { delete engine; }

aa_status AA_CALL aa_archive_open(aa_engine* engine, const aa_open_request* request,
                                  aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || request->probe_only > 1)
            invalid("Task output and a valid probe flag are required");
        auto path = pathInput(request->path);
        Secret password(request->password);
        if (request->probe_only && !password.text.empty())
            invalid("Content probing does not accept a password");
        return withEngine(engine, [&] {
            return submit(*engine, task, [&] {
                return request->probe_only ? engine->archives->probe(path)
                                           : engine->archives->open(path, std::move(password.text));
            });
        });
    });
}

aa_status AA_CALL aa_archive_extract(aa_engine* engine, const aa_extract_request* request,
                                     aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || request->extract_all > 1)
            invalid("Task output and a valid extract-all flag are required");
        arrayInput(request->selected_indices, request->selected_index_count);
        arrayInput(request->selected_directories, request->selected_directory_count);
        if (request->extract_all &&
            (request->selected_index_count || request->selected_directory_count))
            invalid("Extract-all cannot contain a selection");
        auto path = pathInput(request->path);
        ExtractionRequest extraction;
        extraction.destination = pathInput(request->destination);
        extraction.overwrite = overwriteInput(request->overwrite);
        extraction.links = linkInput(request->links);
        if (!request->extract_all) {
            extraction.selectedIndices.emplace();
            if (request->selected_index_count)
                extraction.selectedIndices->assign(request->selected_indices,
                                                   request->selected_indices +
                                                       request->selected_index_count);
        }
        uint64_t pathBytes = 0;
        for (uint64_t i = 0; i < request->selected_directory_count; ++i) {
            const auto directory = textInput(request->selected_directories[i]);
            pathBytes += directory.size();
            if (directory.empty() || pathBytes > 4 * 1024 * 1024)
                invalid("Directory selection is empty or exceeds the byte limit");
            extraction.selectedDirectoryPaths.emplace_back(directory);
        }
        // Validate all other inputs before creating an owned password copy.
        Secret password(request->password);
        return withEngine(engine, [&] {
            return submit(*engine, task, [&] {
                if (!password.text.empty())
                    extraction.password = std::move(password.text);
                return engine->archives->extract(path, std::move(extraction));
            });
        });
    });
}

aa_status AA_CALL aa_archive_compress(aa_engine* engine, const aa_compress_request* request,
                                      aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || request->reserved || request->overwrite_existing > 1 ||
            request->preserve_permissions > 1 || request->preserve_timestamps > 1)
            invalid("Task output and valid compression flags are required");
        arrayInput(request->input_paths, request->input_path_count);
        if (request->input_path_count == 0)
            invalid("At least one compression input is required");
        CompressionRequest compression;
        compression.destination = pathInput(request->destination);
        compression.format = formatInput(request->format);
        compression.method = methodInput(request->method);
        compression.level = request->level;
        compression.overwriteExisting = request->overwrite_existing != 0;
        compression.preservePermissions = request->preserve_permissions != 0;
        compression.preserveTimestamps = request->preserve_timestamps != 0;
        uint64_t pathBytes = 0;
        for (uint64_t i = 0; i < request->input_path_count; ++i) {
            pathBytes += request->input_paths[i].size;
            if (request->input_paths[i].size > AA_MAX_STRING_BYTES || pathBytes > 4 * 1024 * 1024)
                invalid("Compression inputs exceed the byte limit");
            compression.inputPaths.push_back(pathInput(request->input_paths[i]));
        }
        return withEngine(engine, [&] {
            return submit(*engine, task,
                          [&] { return engine->archives->compress(std::move(compression)); });
        });
    });
}

aa_status AA_CALL aa_compression_capabilities(aa_engine* engine, const aa_capability_batch** result,
                                              const aa_error** error) noexcept {
    if (result)
        *result = nullptr;
    return boundary(error, [&]() -> aa_status {
        if (!result)
            invalid("Capability output is required");
        return withEngine(engine, [&]() -> aa_status {
            *result = new CapabilityBatchResult(engine->archives->compressionCapabilities());
            return AA_OK;
        });
    });
}

aa_status AA_CALL aa_task_poll(aa_engine* engine, aa_task_id task, const aa_task_snapshot** result,
                               const aa_error** error) noexcept {
    if (result)
        *result = nullptr;
    return boundary(error, [&]() -> aa_status {
        if (!result)
            invalid("Snapshot output is required");
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->contains(task))
                return AA_NOT_FOUND;
            auto snapshot = engine->tasks->snapshot(task);
            if (!snapshot)
                return AA_NOT_FOUND;
            *result = new SnapshotResult(std::move(*snapshot));
            return AA_OK;
        });
    });
}

aa_status AA_CALL aa_task_take_entries(aa_engine* engine, aa_task_id task,
                                       const aa_entry_batch** result,
                                       const aa_error** error) noexcept {
    if (result)
        *result = nullptr;
    return boundary(error, [&]() -> aa_status {
        if (!result)
            invalid("Entry batch output is required");
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->contains(task))
                return AA_NOT_FOUND;
            auto batch = std::make_unique<EntryBatchResult>();
            batch->assign(task, engine->tasks->takeEntries(task, 1));
            *result = batch.release();
            return AA_OK;
        });
    });
}

aa_status AA_CALL aa_task_cancel(aa_engine* engine, aa_task_id task) noexcept {
    return boundary(nullptr, [&] {
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->contains(task))
                return AA_NOT_FOUND;
            return engine->tasks->cancel(task) ? AA_OK : AA_BUSY;
        });
    });
}
aa_status AA_CALL aa_task_resolve_conflict(aa_engine* engine, aa_task_id task, uint64_t conflict,
                                           uint32_t overwrite, uint32_t all) noexcept {
    return boundary(nullptr, [&]() -> aa_status {
        if (overwrite == AA_OVERWRITE_ASK || all > 1 || conflict == 0)
            invalid("A conflict ID and final overwrite decision are required");
        const auto policy = overwriteInput(overwrite);
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->contains(task))
                return AA_NOT_FOUND;
            return engine->tasks->resolveConflict(task, conflict, {policy, all != 0}) ? AA_OK
                                                                                      : AA_BUSY;
        });
    });
}
aa_status AA_CALL aa_task_release(aa_engine* engine, aa_task_id task) noexcept {
    return boundary(nullptr, [&] {
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->contains(task))
                return AA_NOT_FOUND;
            for (auto& slot : engine->slots) {
                if (slot.id == task) {
                    slot.retiring = true;
                    break;
                }
            }
            engine->changed.notify_all();
            return AA_OK;
        });
    });
}

void AA_CALL aa_error_release(const aa_error* error) noexcept {
    delete static_cast<const ErrorResult*>(error);
}
void AA_CALL aa_snapshot_release(const aa_task_snapshot* snapshot) noexcept {
    delete static_cast<const SnapshotResult*>(snapshot);
}
void AA_CALL aa_entry_batch_release(const aa_entry_batch* batch) noexcept {
    delete static_cast<const EntryBatchResult*>(batch);
}
void AA_CALL aa_capability_batch_release(const aa_capability_batch* batch) noexcept {
    delete static_cast<const CapabilityBatchResult*>(batch);
}

aa_status AA_CALL aa_browser_open(aa_engine* engine, const aa_open_request* request,
                                  aa_archive_id* archive, aa_task_id* task,
                                  const aa_error** error) noexcept {
    if (archive)
        *archive = 0;
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!archive || !task || request->probe_only)
            invalid("Browser open outputs and non-probe request required");
        auto path = pathInput(request->path);
        Secret password(request->password);
        return withEngine(engine, [&]() -> aa_status {
            return submit(*engine, task, [&]() -> Result<application::TaskId> {
                engine->browserCleanup = true;
                engine->changed.notify_all();
                auto result = engine->browser->open(path, std::move(password.text));
                if (!result)
                    return std::unexpected(result.error());
                *archive = result->archive;
                return result->task;
            });
        });
    });
}
aa_status AA_CALL aa_browser_query(aa_engine* engine, const aa_browser_query_request* request,
                                   aa_query_id* query, aa_task_id* task,
                                   const aa_error** error) noexcept {
    if (query)
        *query = 0;
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!query || !task || request->descending > 1 || request->folders_only > 1 ||
            request->sort_column < -1 || request->sort_column > 3)
            invalid("Invalid browser query");
        application::MetadataQuery input{std::string(textInput(request->folder)),
                                         std::string(textInput(request->search)),
                                         request->sort_column,
                                         request->descending != 0,
                                         true,
                                         request->folders_only != 0};
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->valid(request->archive))
                return AA_STALE_VIEW;
            return submit(*engine, task, [&]() -> Result<application::TaskId> {
                engine->browserCleanup = true;
                engine->changed.notify_all();
                auto result = engine->browser->query(request->archive, std::move(input));
                if (!result)
                    return std::unexpected(result.error());
                *query = result->query;
                return result->task;
            });
        });
    });
}
aa_status AA_CALL aa_browser_request_page(aa_engine* engine, const aa_browser_page_request* request,
                                          aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || request->reserved || !request->max_rows ||
            request->max_rows > AA_MAX_BATCH_ENTRIES ||
            request->max_bytes < application::MetadataIndex::rowWireBytes ||
            request->max_bytes > AA_MAX_PAGE_BYTES)
            invalid("Invalid browser page bounds");
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->valid(request->archive, request->query) || !request->query)
                return AA_STALE_VIEW;
            return submit(*engine, task, [&] {
                return engine->browser->page(request->archive, request->query, request->offset,
                                             request->max_rows, request->max_bytes);
            });
        });
    });
}
aa_status AA_CALL aa_browser_request_folders(aa_engine* engine,
                                             const aa_browser_folder_page_request* request,
                                             aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || request->reserved || !request->max_rows ||
            request->max_rows > AA_MAX_BATCH_ENTRIES ||
            request->max_bytes < application::MetadataIndex::rowWireBytes ||
            request->max_bytes > AA_MAX_PAGE_BYTES)
            invalid("Invalid folder page bounds");
        auto folder = std::string(textInput(request->folder));
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->valid(request->archive))
                return AA_STALE_VIEW;
            return submit(*engine, task, [&] {
                return engine->browser->folderPage(request->archive, std::move(folder),
                                                   request->offset, request->max_rows,
                                                   request->max_bytes);
            });
        });
    });
}
aa_status AA_CALL aa_browser_select(aa_engine* engine, const aa_browser_select_request* request,
                                    aa_selection_id* selection, aa_task_id* task,
                                    const aa_error** error) noexcept {
    if (selection)
        *selection = 0;
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        arrayInput(request->ranges, request->range_count);
        if (!selection || !task || request->select_all > 1 ||
            (request->select_all && request->range_count))
            invalid("Invalid browser selection");
        std::vector<application::MetadataRange> ranges;
        for (uint64_t i = 0; i < request->range_count; ++i)
            ranges.push_back({request->ranges[i].first, request->ranges[i].count});
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->valid(request->archive, request->query) || !request->query)
                return AA_STALE_VIEW;
            return submit(*engine, task, [&]() -> Result<application::TaskId> {
                auto result = engine->browser->select(request->archive, request->query,
                                                      std::move(ranges), request->select_all != 0);
                if (!result)
                    return std::unexpected(result.error());
                *selection = result->selection;
                return result->task;
            });
        });
    });
}
aa_status AA_CALL aa_browser_extract(aa_engine* engine, aa_archive_id archive, aa_query_id query,
                                     aa_selection_id selection, const aa_extract_request* request,
                                     aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || !query || !selection || request->extract_all ||
            request->selected_index_count || request->selected_directory_count ||
            request->path.size)
            invalid("Browser extraction requires only a native selection");
        ExtractionRequest input;
        input.destination = pathInput(request->destination);
        input.overwrite = overwriteInput(request->overwrite);
        input.links = linkInput(request->links);
        Secret password(request->password);
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->valid(archive, query, selection))
                return AA_STALE_VIEW;
            return submit(*engine, task, [&] {
                if (!password.text.empty())
                    input.password = std::move(password.text);
                return engine->browser->extract(archive, query, selection, std::move(input));
            });
        });
    });
}
aa_status AA_CALL aa_browser_task_result(aa_engine* engine, aa_task_id task,
                                         const aa_browser_result** result,
                                         const aa_error** error) noexcept {
    if (result)
        *result = nullptr;
    return boundary(error, [&]() -> aa_status {
        if (!result)
            invalid("Browser result output required");
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->contains(task))
                return AA_NOT_FOUND;
            auto snapshot = engine->tasks->snapshot(task);
            if (!snapshot)
                return AA_NOT_FOUND;
            if (snapshot->state == application::TaskState::Failed ||
                snapshot->state == application::TaskState::Cancelled)
                return AA_ARCHIVE_ERROR;
            auto value = engine->tasks->browserResult(task);
            if (!value)
                return snapshot->state == application::TaskState::Succeeded ? AA_INVALID_ARGUMENT
                                                                            : AA_BUSY;
            if (!engine->browser->valid(value->archive, value->query, value->selection))
                return AA_STALE_VIEW;
            *result = new BrowserResult(std::move(value));
            return AA_OK;
        });
    });
}
aa_status AA_CALL aa_browser_close(aa_engine* engine, aa_archive_id archive) noexcept {
    return boundary(nullptr, [&] {
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->contains(archive))
                return AA_STALE_VIEW;
            if (!engine->browser->close(archive))
                return AA_RESOURCE_LIMIT;
            engine->browserCleanup = true;
            engine->changed.notify_all();
            return AA_OK;
        });
    });
}
void AA_CALL aa_browser_result_release(const aa_browser_result* result) noexcept {
    delete static_cast<const BrowserResult*>(result);
}
aa_status AA_CALL aa_export_control(aa_engine* engine, const aa_export_request* request,
                                    const aa_export_result** result,
                                    const aa_error** error) noexcept {
    if (result)
        *result = nullptr;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!result || request->action < 1 || request->action > 7)
            invalid("Export control output/action required");
        auto path = request->path.size ? pathInput(request->path) : std::filesystem::path{};
        if ((request->action <= 4 || request->action == 7) &&
            (!path.is_absolute() || path.lexically_normal() != path))
            invalid("An absolute normalized private export path is required");
        return withEngine(engine, [&]() -> aa_status {
            auto output = std::make_unique<ExportResult>();
            if (!engine->exports) {
                if (request->action != 1 && request->action != 7)
                    return AA_NOT_FOUND;
                engine->exports = std::make_unique<application::TempFileManager>(path, true);
            }
            auto& manager = *engine->exports;
            if (request->action == 1) {
                auto root = manager.create();
                if (!root)
                    throw BoundaryFailure{AA_RESOURCE_LIMIT, root.error()};
                output->text.push_back(pathToUtf8(*root));
            } else if ((request->action == 5 || request->action == 7)) {
                for (const auto& root : manager.retainedRoots())
                    output->text.push_back(pathToUtf8(root));
            } else if (request->action == 6) {
                for (const auto& failure : manager.takeCleanupErrors())
                    output->ownedErrors.push_back(std::make_unique<ErrorResult>(failure));
            } else {
                if (!manager.hold(path))
                    return AA_NOT_FOUND;
                if (request->action == 2) {
                    std::size_t bytes = 0;
                    for (const auto& child : std::filesystem::directory_iterator(path)) {
                        if (child.is_symlink() ||
                            (!child.is_regular_file() && !child.is_directory()))
                            continue;
                        auto text = pathToUtf8(child.path());
                        bytes += text.size();
                        if (output->text.size() >= 256 || bytes > AA_MAX_PAGE_BYTES)
                            throw BoundaryFailure{AA_RESOURCE_LIMIT,
                                                  {ArchiveErrorCode::ResourceLimitExceeded,
                                                   "Export has more than 256 direct paths or 1 MiB "
                                                   "path text; select a folder instead",
                                                   {},
                                                   path}};
                        output->text.push_back(std::move(text));
                    }
                    if (output->text.empty())
                        throw BoundaryFailure{AA_ARCHIVE_ERROR,
                                              {ArchiveErrorCode::UnsupportedOperation,
                                               "Selection produced no files or directories",
                                               {},
                                               path}};
                } else if (request->action == 3) {
                    manager.handoff(path);
                } else {
                    manager.discard(path);
                }
            }
            output->finish();
            *result = output.release();
            return AA_OK;
        });
    });
}
void AA_CALL aa_export_result_release(const aa_export_result* result) noexcept {
    delete static_cast<const ExportResult*>(result);
}
aa_status AA_CALL aa_browser_export(aa_engine* engine, aa_archive_id archive, aa_query_id query,
                                    aa_selection_id selection, const aa_extract_request* request,
                                    aa_task_id* task, const aa_error** error) noexcept {
    if (task)
        *task = 0;
    return boundary(error, [&]() -> aa_status {
        requestSize(request);
        if (!task || !query || !selection || request->extract_all ||
            request->selected_index_count || request->selected_directory_count ||
            request->path.size)
            invalid("Export requires only a native selection");
        ExtractionRequest input;
        input.destination = pathInput(request->destination);
        input.overwrite = OverwritePolicy::Rename;
        input.links = LinkPolicy::Skip;
        input.limits.maxEntries = 10000;
        input.limits.maxTotalBytes = 512ULL * 1024 * 1024;
        input.limits.maxEntryBytes = input.limits.maxTotalBytes;
        input.limits.maxElapsed = std::chrono::minutes{10};
        Secret password(request->password);
        return withEngine(engine, [&]() -> aa_status {
            if (!engine->browser->valid(archive, query, selection))
                return AA_STALE_VIEW;
            if (!engine->exports || !engine->exports->writable(input.destination) ||
                !engine->exports->hold(input.destination))
                return AA_NOT_FOUND;
            return submit(*engine, task, [&] {
                if (!password.text.empty())
                    input.password = std::move(password.text);
                return engine->browser->extract(archive, query, selection, std::move(input));
            });
        });
    });
}
} // extern "C"
