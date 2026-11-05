#ifndef ANANARCHIVER_C_API_H
#define ANANARCHIVER_C_API_H

#include <stdint.h>

#if defined(_WIN32)
#define AA_CALL __cdecl
#if defined(ANANARCHIVER_C_API_BUILD)
#define AA_API __declspec(dllexport)
#else
#define AA_API __declspec(dllimport)
#endif
#else
#define AA_CALL
#define AA_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
#define AA_NOEXCEPT noexcept
extern "C" {
#else
#define AA_NOEXCEPT
#endif

#define AA_ABI_VERSION 1u
#define AA_MAX_BATCH_ENTRIES 512u
#define AA_MAX_STRING_BYTES 65536u
#define AA_MAX_REQUEST_ITEMS 4096u

typedef uint32_t aa_status;
#define AA_OK 0u
#define AA_INVALID_ARGUMENT 1u
#define AA_ABI_MISMATCH 2u
#define AA_NOT_FOUND 3u
#define AA_BUSY 4u
#define AA_SHUTTING_DOWN 5u
#define AA_RESOURCE_LIMIT 6u
#define AA_INTERNAL_ERROR 7u
#define AA_ARCHIVE_ERROR 8u
#define AA_STALE_VIEW 9u
#define AA_MAX_PAGE_BYTES 1048576u
#define AA_NO_ENTRY_INDEX UINT64_MAX

/* Values are ABI constants, independent of C++ enum ordinals. */
#define AA_ERROR_NONE 0u
#define AA_ERROR_FILE_NOT_FOUND 1u
#define AA_ERROR_PERMISSION_DENIED 2u
#define AA_ERROR_UNSUPPORTED_FORMAT 3u
#define AA_ERROR_UNSUPPORTED_COMPRESSION 4u
#define AA_ERROR_UNSUPPORTED_OPERATION 5u
#define AA_ERROR_INVALID_ARCHIVE 6u
#define AA_ERROR_CORRUPTED_ARCHIVE 7u
#define AA_ERROR_WRONG_PASSWORD 8u
#define AA_ERROR_PASSWORD_REQUIRED 9u
#define AA_ERROR_DISK_FULL 10u
#define AA_ERROR_OUTPUT_EXISTS 11u
#define AA_ERROR_CANCELLED 12u
#define AA_ERROR_CLEANUP_FAILED 13u
#define AA_ERROR_RESOURCE_LIMIT 14u
#define AA_ERROR_INVALID_PATH_ENCODING 15u
#define AA_ERROR_UNSAFE_PATH 16u
#define AA_ERROR_IO 17u
#define AA_ERROR_BACKEND_WARNING 18u
#define AA_ERROR_BACKEND 19u
#define AA_ERROR_UNKNOWN 20u

#define AA_TASK_OPEN 1u
#define AA_TASK_EXTRACT 2u
#define AA_TASK_COMPRESS 3u
#define AA_TASK_BROWSE 4u
#define AA_TASK_PENDING 1u
#define AA_TASK_RUNNING 2u
#define AA_TASK_CANCELLING 3u
#define AA_TASK_CANCELLED 4u
#define AA_TASK_SUCCEEDED 5u
#define AA_TASK_FAILED 6u

#define AA_OVERWRITE_ASK 0u
#define AA_OVERWRITE_REPLACE 1u
#define AA_OVERWRITE_SKIP 2u
#define AA_OVERWRITE_RENAME 3u
#define AA_OVERWRITE_IF_NEWER 4u
#define AA_LINK_SAFE 0u
#define AA_LINK_PRESERVE 1u
#define AA_LINK_SKIP 2u
#define AA_ENTRY_FILE 1u
#define AA_ENTRY_DIRECTORY 2u
#define AA_ENTRY_SYMLINK 3u
#define AA_ENTRY_HARDLINK 4u
#define AA_ENTRY_OTHER 5u

#define AA_FORMAT_ZIP 1u
#define AA_FORMAT_7Z 2u
#define AA_FORMAT_TAR 3u
#define AA_FORMAT_TAR_GZIP 4u
#define AA_FORMAT_TAR_BZIP2 5u
#define AA_FORMAT_TAR_XZ 6u
#define AA_FORMAT_TAR_ZSTD 7u
#define AA_FORMAT_TAR_LZ4 8u
#define AA_METHOD_STORE 1u
#define AA_METHOD_DEFLATE 2u
#define AA_METHOD_LZMA2 3u
#define AA_METHOD_GZIP 4u
#define AA_METHOD_BZIP2 5u
#define AA_METHOD_XZ 6u
#define AA_METHOD_ZSTD 7u
#define AA_METHOD_LZ4 8u

#define AA_FEATURE_OPEN 1u
#define AA_FEATURE_EXTRACT 2u
#define AA_FEATURE_COMPRESS 4u
#define AA_FEATURE_METADATA_STREAM 8u
#define AA_FEATURE_ASYNC_CLEANUP 16u
#define AA_FEATURE_BROWSER 32u
#define AA_HAS_TOTAL_BYTES 1u
#define AA_HAS_TOTAL_ENTRIES 2u
#define AA_HAS_ETA 4u
#define AA_HAS_ARCHIVE 8u
#define AA_ARCHIVE_ENCRYPTED 16u
#define AA_HAS_CONFLICT 32u
#define AA_SIZE_KNOWN 1u
#define AA_COMPRESSED_SIZE_KNOWN 2u
#define AA_MODIFIED_KNOWN 4u
#define AA_PHASE_NONE 0u
#define AA_PHASE_SCANNING 1u
#define AA_PHASE_WRITING 2u
#define AA_PHASE_FINALIZING 3u

typedef struct aa_engine aa_engine;
typedef uint64_t aa_task_id;
/* UTF-8, length in bytes, no required trailing NUL. NULL is valid only at size 0.
 * Inputs are borrowed for the duration of the call and copied before return.
 * Outputs are immutable views valid until their owning result is released. */
typedef struct aa_string {
    const char* data;
    uint64_t size;
} aa_string;

typedef struct aa_api_info {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t features;
    uint32_t max_batch_entries;
    uint32_t max_string_bytes;
    uint32_t max_active_tasks;
    uint32_t max_retained_tasks;
} aa_api_info;

typedef struct aa_error {
    uint32_t struct_size;
    uint32_t code;
    aa_string message;
    aa_string backend_message;
    aa_string path;
} aa_error;

typedef struct aa_open_request {
    uint32_t struct_size;
    uint32_t probe_only; /* 0: stream metadata; 1: content detection only */
    aa_string path;
    aa_string password;
} aa_open_request;

typedef struct aa_extract_request {
    uint32_t struct_size;
    uint32_t overwrite;
    aa_string path;
    aa_string destination;
    aa_string password;
    uint32_t links;
    uint32_t extract_all; /* 1: selection must be empty; 0: explicit selection */
    const uint64_t* selected_indices;
    uint64_t selected_index_count;
    const aa_string* selected_directories;
    uint64_t selected_directory_count;
} aa_extract_request;

typedef struct aa_compress_request {
    uint32_t struct_size;
    uint32_t format;
    uint32_t method;
    int32_t level;
    aa_string destination;
    const aa_string* input_paths;
    uint64_t input_path_count;
    uint32_t overwrite_existing;
    uint32_t preserve_permissions;
    uint32_t preserve_timestamps;
    uint32_t reserved; /* must be zero */
} aa_compress_request;

typedef struct aa_entry {
    uint32_t struct_size;
    uint32_t type;
    aa_string path;
    uint64_t index; /* original enumeration ordinal, including duplicate names */
    uint64_t size;
    uint64_t compressed_size;
    int64_t modified_unix_ms;
    uint32_t flags;
    uint32_t reserved;
} aa_entry;

typedef struct aa_entry_batch {
    uint32_t struct_size;
    uint32_t count;
    aa_task_id task_id;
    const aa_entry* entries;
} aa_entry_batch;

typedef struct aa_task_snapshot {
    uint32_t struct_size;
    uint32_t type;
    aa_task_id task_id;
    uint32_t state;
    uint32_t flags;
    aa_string current_file;
    uint64_t processed_bytes;
    uint64_t processed_entries;
    uint64_t total_bytes;
    uint64_t total_entries;
    uint64_t elapsed_ms;
    uint64_t estimated_remaining_ms;
    double bytes_per_second;
    aa_string format_name; /* detected backend format, not filename extension */
    uint32_t compression_phase;
    uint32_t reserved;
    const aa_error* error; /* borrowed; released with this snapshot */
    uint64_t conflict_id;
    aa_entry conflict_entry;
    aa_string conflict_destination;
} aa_task_snapshot;

typedef struct aa_compression_capability {
    uint32_t struct_size;
    uint32_t format;
    uint32_t method;
    uint32_t has_levels;
    int32_t min_level;
    int32_t max_level;
    int32_t default_level;
    uint32_t supports_password;
    uint32_t supports_multithreading;
    uint32_t supports_solid;
    uint32_t supports_permissions;
    uint32_t supports_timestamps;
    aa_string format_name;
    aa_string method_name;
    aa_string extension;
} aa_compression_capability;

typedef struct aa_capability_batch {
    uint32_t struct_size;
    uint32_t count;
    const aa_compression_capability* capabilities;
} aa_capability_batch;

/* F2 is additive to ABI v1. Handles are engine-scoped generation numbers.
 * There is one current archive and one current query per engine. Opening or
 * closing invalidates the previous archive; every query invalidates its prior
 * query/selection immediately, even if the replacement fails or is cancelled.
 * Folder row identity is independent of aa_entry.index; for folders the nested
 * index is AA_NO_ENTRY_INDEX. A file row identity is stable within its archive and its
 * nested index is the original backend ordinal. Never treat identity as index. */
typedef uint64_t aa_archive_id;
typedef uint64_t aa_query_id;
typedef uint64_t aa_selection_id;
typedef struct aa_browser_query_request {
    uint32_t struct_size;
    int32_t sort_column; /* -1 archive order, 0 name, 1 size, 2 type, 3 date */
    aa_archive_id archive;
    aa_string folder; /* empty is root; caller can derive parent by components */
    aa_string search;
    uint32_t descending;
    uint32_t folders_only; /* direct folder children for a lazy tree */
} aa_browser_query_request;
typedef struct aa_browser_page_request {
    uint32_t struct_size;
    uint32_t max_rows; /* 1..512 */
    aa_archive_id archive;
    aa_query_id query;
    uint64_t offset;
    uint32_t max_bytes; /* 160..AA_MAX_PAGE_BYTES, includes row allowance */
    uint32_t reserved;
} aa_browser_page_request;
typedef struct aa_browser_folder_page_request {
    uint32_t struct_size;
    uint32_t max_rows;
    aa_archive_id archive;
    aa_string folder;
    uint64_t offset;
    uint32_t max_bytes;
    uint32_t reserved;
} aa_browser_folder_page_request;
typedef struct aa_browser_range {
    uint64_t first;
    uint64_t count; /* query positions, sorted nonoverlapping ranges */
} aa_browser_range;
typedef struct aa_browser_select_request {
    uint32_t struct_size;
    uint32_t select_all; /* whole current query, requires range_count == 0 */
    aa_archive_id archive;
    aa_query_id query;
    const aa_browser_range* ranges;
    uint64_t range_count; /* <=4096; never a million-item FFI array */
} aa_browser_select_request;
typedef struct aa_browser_row {
    uint32_t struct_size;
    uint32_t is_folder;
    uint64_t identity;
    aa_string name;
    aa_entry entry;
} aa_browser_row;
typedef struct aa_browser_result {
    uint32_t struct_size;
    uint32_t count;
    aa_archive_id archive;
    aa_query_id query;
    aa_selection_id selection;
    uint64_t entry_count;
    uint64_t folder_count;
    uint64_t index_bytes; /* conservative native index accounting */
    uint64_t offset;
    uint64_t total_rows;
    uint64_t transfer_bytes; /* row allowance plus UTF-8 bytes <= max_bytes */
    const aa_browser_row* rows;
} aa_browser_result;
/* Open performs content detection + complete native indexing, without F1's
 * metadata stream. On failure/cancel there is no usable partial index. Input
 * uses aa_open_request with probe_only == 0. Results arrive on the task.
 * Calls only submit/invalidate; build/query/sort/page/select run on TaskManager.
 * aa_browser_task_result returns AA_BUSY until success, AA_ARCHIVE_ERROR after
 * failure/cancel, AA_STALE_VIEW after invalidation, or an owned bounded result.
 * Release it exactly once with aa_browser_result_release; it can outlive engine.
 * Keep caller caches bounded; a result is a page, never the complete index.
 * Task release/close defer large data reclamation to the engine cleaner.
 * A selection handle stays native; browser_extract uses it on a worker. */
AA_API aa_status AA_CALL aa_browser_open(aa_engine*, const aa_open_request*, aa_archive_id*,
                                         aa_task_id*, const aa_error**) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_browser_query(aa_engine*, const aa_browser_query_request*, aa_query_id*,
                                          aa_task_id*, const aa_error**) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_browser_request_page(aa_engine*, const aa_browser_page_request*,
                                                 aa_task_id*, const aa_error**) AA_NOEXCEPT;
/* Lazy tree window independent of current list query; does not clear selection.
 * Result query == 0; tree rows can navigate by path but cannot authorize a list
 * selection. Like list windows this returns at most 512 rows / 1 MiB. */
AA_API aa_status AA_CALL aa_browser_request_folders(aa_engine*,
                                                    const aa_browser_folder_page_request*,
                                                    aa_task_id*, const aa_error**) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_browser_select(aa_engine*, const aa_browser_select_request*,
                                           aa_selection_id*, aa_task_id*,
                                           const aa_error**) AA_NOEXCEPT;
/* request.path must be empty, extract_all == 0, selection arrays empty.
 * Destination/password/overwrite/links follow aa_archive_extract. The native
 * selection is checked against the current query again before extraction. */
AA_API aa_status AA_CALL aa_browser_extract(aa_engine*, aa_archive_id, aa_query_id, aa_selection_id,
                                            const aa_extract_request*, aa_task_id*,
                                            const aa_error**) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_browser_task_result(aa_engine*, aa_task_id, const aa_browser_result**,
                                                const aa_error**) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_browser_close(aa_engine*, aa_archive_id) AA_NOEXCEPT;
AA_API void AA_CALL aa_browser_result_release(const aa_browser_result*) AA_NOEXCEPT;

/* Initialize input struct_size to sizeof(the struct); larger suffixes are
 * ignored in ABI v1. Do not pack structs or change the native C alignment.
 * All non-NULL pointers must address valid, suitably sized memory.
 * Engine calls are thread-safe; destroy must have exclusive ownership.
 * Task IDs are engine-scoped and invalid immediately after task_release.
 * Released engine/result pointers must never be reused (including double free).
 * Output slots are cleared on failure. Release an old result before reusing
 * its slot. Optional error outputs may be NULL; allocation failure can leave
 * them NULL, so always check the returned status as well.
 * No C++ exceptions or callbacks cross this boundary. */
AA_API aa_status AA_CALL aa_get_api_info(aa_api_info* info) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_engine_create(uint32_t abi_version, aa_engine** engine,
                                          const aa_error** error) AA_NOEXCEPT;
/* Nonblocking: rejects new work, cancels/joins/cleans on the owned worker. */
AA_API aa_status AA_CALL aa_engine_shutdown(aa_engine* engine) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_engine_is_stopped(aa_engine* engine, uint32_t* stopped) AA_NOEXCEPT;
/* Final blocking join/free. Call on a background isolate/thread, never the UI
 * isolate. Requests shutdown too if needed. Results may outlive the engine. */
AA_API void AA_CALL aa_engine_destroy(aa_engine* engine) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_archive_open(aa_engine* engine, const aa_open_request* request,
                                         aa_task_id* task, const aa_error** error) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_archive_extract(aa_engine* engine, const aa_extract_request* request,
                                            aa_task_id* task, const aa_error** error) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_archive_compress(aa_engine* engine, const aa_compress_request* request,
                                             aa_task_id* task, const aa_error** error) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_compression_capabilities(aa_engine* engine,
                                                     const aa_capability_batch** result,
                                                     const aa_error** error) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_task_poll(aa_engine* engine, aa_task_id task,
                                      const aa_task_snapshot** result,
                                      const aa_error** error) AA_NOEXCEPT;
/* One batch, at most 512 entries; success with count 0 means no data currently
 * queued. Drain until count 0 after terminal success. Metadata streams are F1
 * transport, not a Dart-side full index; shared C++ browsing is stage F2. */
AA_API aa_status AA_CALL aa_task_take_entries(aa_engine* engine, aa_task_id task,
                                              const aa_entry_batch** result,
                                              const aa_error** error) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_task_cancel(aa_engine* engine, aa_task_id task) AA_NOEXCEPT;
AA_API aa_status AA_CALL aa_task_resolve_conflict(aa_engine* engine, aa_task_id task,
                                                  uint64_t conflict_id, uint32_t overwrite,
                                                  uint32_t apply_to_all) AA_NOEXCEPT;
/* Invalidates ID immediately. Worker cancels if needed, drains and joins. */
AA_API aa_status AA_CALL aa_task_release(aa_engine* engine, aa_task_id task) AA_NOEXCEPT;
AA_API void AA_CALL aa_error_release(const aa_error* error) AA_NOEXCEPT;
AA_API void AA_CALL aa_snapshot_release(const aa_task_snapshot* snapshot) AA_NOEXCEPT;
AA_API void AA_CALL aa_entry_batch_release(const aa_entry_batch* batch) AA_NOEXCEPT;
AA_API void AA_CALL aa_capability_batch_release(const aa_capability_batch* batch) AA_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif
