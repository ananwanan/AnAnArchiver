#include "results.h"

#include <ananarchiver/unicode.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ananarchiver::c_api {
namespace {
uint32_t errorValue(ArchiveErrorCode code) noexcept {
    switch (code) {
    case ArchiveErrorCode::FileNotFound:
        return AA_ERROR_FILE_NOT_FOUND;
    case ArchiveErrorCode::PermissionDenied:
        return AA_ERROR_PERMISSION_DENIED;
    case ArchiveErrorCode::UnsupportedFormat:
        return AA_ERROR_UNSUPPORTED_FORMAT;
    case ArchiveErrorCode::UnsupportedCompression:
        return AA_ERROR_UNSUPPORTED_COMPRESSION;
    case ArchiveErrorCode::UnsupportedOperation:
        return AA_ERROR_UNSUPPORTED_OPERATION;
    case ArchiveErrorCode::InvalidArchive:
        return AA_ERROR_INVALID_ARCHIVE;
    case ArchiveErrorCode::CorruptedArchive:
        return AA_ERROR_CORRUPTED_ARCHIVE;
    case ArchiveErrorCode::WrongPassword:
        return AA_ERROR_WRONG_PASSWORD;
    case ArchiveErrorCode::PasswordRequired:
        return AA_ERROR_PASSWORD_REQUIRED;
    case ArchiveErrorCode::DiskFull:
        return AA_ERROR_DISK_FULL;
    case ArchiveErrorCode::OutputExists:
        return AA_ERROR_OUTPUT_EXISTS;
    case ArchiveErrorCode::Cancelled:
        return AA_ERROR_CANCELLED;
    case ArchiveErrorCode::CleanupFailed:
        return AA_ERROR_CLEANUP_FAILED;
    case ArchiveErrorCode::ResourceLimitExceeded:
        return AA_ERROR_RESOURCE_LIMIT;
    case ArchiveErrorCode::InvalidPathEncoding:
        return AA_ERROR_INVALID_PATH_ENCODING;
    case ArchiveErrorCode::UnsafePath:
        return AA_ERROR_UNSAFE_PATH;
    case ArchiveErrorCode::IoError:
        return AA_ERROR_IO;
    case ArchiveErrorCode::BackendWarning:
        return AA_ERROR_BACKEND_WARNING;
    case ArchiveErrorCode::BackendError:
        return AA_ERROR_BACKEND;
    case ArchiveErrorCode::Unknown:
        return AA_ERROR_UNKNOWN;
    }
    return AA_ERROR_UNKNOWN;
}

// Backend diagnostics can contain native bytes. Never export malformed UTF-8;
// diagnostics are bounded and may be shortened (operational paths are not).
std::string diagnostic(std::string_view input) {
    auto prefix = input.substr(0, AA_MAX_STRING_BYTES);
    // At most three trailing bytes can be part of a cut UTF-8 sequence.
    for (unsigned removed = 0; removed < 4; ++removed) {
        if (isValidUtf8(prefix))
            return std::string(prefix);
        if (prefix.empty())
            break;
        prefix.remove_suffix(1);
    }
    return "Diagnostic contains invalid UTF-8";
}

} // namespace

aa_entry entryValue(const ArchiveEntry& entry) noexcept {
    aa_entry row{};
    row.struct_size = sizeof(row);
    switch (entry.type) {
    case EntryType::RegularFile:
        row.type = AA_ENTRY_FILE;
        break;
    case EntryType::Directory:
        row.type = AA_ENTRY_DIRECTORY;
        break;
    case EntryType::Symlink:
        row.type = AA_ENTRY_SYMLINK;
        break;
    case EntryType::Hardlink:
        row.type = AA_ENTRY_HARDLINK;
        break;
    case EntryType::Other:
        row.type = AA_ENTRY_OTHER;
        break;
    }
    row.path = view(entry.path);
    row.index = entry.index;
    row.size = entry.size;
    if (entry.sizeKnown)
        row.flags |= AA_SIZE_KNOWN;
    if (entry.compressedSize) {
        row.flags |= AA_COMPRESSED_SIZE_KNOWN;
        row.compressed_size = *entry.compressedSize;
    }
    if (entry.modified) {
        row.flags |= AA_MODIFIED_KNOWN;
        row.modified_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   entry.modified->time_since_epoch())
                                   .count();
    }
    return row;
}

aa_string view(const std::string& text) noexcept {
    return {text.data(), static_cast<uint64_t>(text.size())};
}

ErrorResult::ErrorResult(const ArchiveError& failure)
    : aa_error{}, messageText(diagnostic(failure.message)),
      backendText(diagnostic(failure.backendMessage)) {
    try {
        pathText = diagnostic(pathToUtf8(failure.path));
    } catch (const std::invalid_argument&) {
        pathText = "Path contains invalid Unicode";
    }
    struct_size = sizeof(aa_error);
    code = errorValue(failure.code);
    message = view(messageText);
    backend_message = view(backendText);
    path = view(pathText);
}

SnapshotResult::SnapshotResult(application::TaskSnapshot snapshot)
    : aa_task_snapshot{}, source(std::move(snapshot)) {
    struct_size = sizeof(aa_task_snapshot);
    task_id = source.id;
    switch (source.type) {
    case application::TaskType::OpenArchive:
        type = AA_TASK_OPEN;
        break;
    case application::TaskType::ExtractArchive:
        type = AA_TASK_EXTRACT;
        break;
    case application::TaskType::CompressArchive:
        type = AA_TASK_COMPRESS;
        break;
    case application::TaskType::BrowseArchive:
        type = AA_TASK_BROWSE;
        break;
    }
    switch (source.state) {
    case application::TaskState::Pending:
        state = AA_TASK_PENDING;
        break;
    case application::TaskState::Running:
        state = AA_TASK_RUNNING;
        break;
    case application::TaskState::Cancelling:
        state = AA_TASK_CANCELLING;
        break;
    case application::TaskState::Cancelled:
        state = AA_TASK_CANCELLED;
        break;
    case application::TaskState::Succeeded:
        state = AA_TASK_SUCCEEDED;
        break;
    case application::TaskState::Failed:
        state = AA_TASK_FAILED;
        break;
    }
    source.currentFile = diagnostic(source.currentFile);
    current_file = view(source.currentFile);
    processed_bytes = source.processedBytes;
    processed_entries = source.processedEntries;
    elapsed_ms = static_cast<uint64_t>(std::max<int64_t>(0, source.elapsed.count()));
    bytes_per_second = source.bytesPerSecond;
    if (source.totalBytes) {
        flags |= AA_HAS_TOTAL_BYTES;
        total_bytes = *source.totalBytes;
    }
    if (source.totalEntries) {
        flags |= AA_HAS_TOTAL_ENTRIES;
        total_entries = *source.totalEntries;
    }
    if (source.estimatedRemaining) {
        flags |= AA_HAS_ETA;
        estimated_remaining_ms =
            static_cast<uint64_t>(std::max<int64_t>(0, source.estimatedRemaining->count()));
    }
    if (source.archive) {
        flags |= AA_HAS_ARCHIVE;
        format_name = view(source.archive->formatName);
        if (source.archive->encrypted)
            flags |= AA_ARCHIVE_ENCRYPTED;
    }
    if (source.error) {
        failure = std::make_unique<ErrorResult>(*source.error);
        error = failure.get();
    }
    if (source.compressionPhase) {
        switch (*source.compressionPhase) {
        case CompressionPhase::Scanning:
            compression_phase = AA_PHASE_SCANNING;
            break;
        case CompressionPhase::Writing:
            compression_phase = AA_PHASE_WRITING;
            break;
        case CompressionPhase::Finalizing:
            compression_phase = AA_PHASE_FINALIZING;
            break;
        }
    }
    if (source.conflict) {
        flags |= AA_HAS_CONFLICT;
        conflict_id = source.conflictId;
        conflict_entry = entryValue(source.conflict->entry);
        conflictPath = diagnostic(pathToUtf8(source.conflict->destination));
        conflict_destination = view(conflictPath);
    }
}

EntryBatchResult::EntryBatchResult() : aa_entry_batch{} {
    struct_size = sizeof(aa_entry_batch);
    rows.reserve(AA_MAX_BATCH_ENTRIES); // allocate before consuming queue
}
void EntryBatchResult::assign(aa_task_id task, std::vector<ArchiveEntry> metadata) {
    source = std::move(metadata);
    for (const auto& entry : source)
        rows.push_back(entryValue(entry));
    task_id = task;
    count = static_cast<uint32_t>(rows.size());
    this->entries = rows.data();
}

uint32_t formatValue(CompressionFormat format) noexcept {
    switch (format) {
    case CompressionFormat::Zip:
        return AA_FORMAT_ZIP;
    case CompressionFormat::SevenZip:
        return AA_FORMAT_7Z;
    case CompressionFormat::Tar:
        return AA_FORMAT_TAR;
    case CompressionFormat::TarGzip:
        return AA_FORMAT_TAR_GZIP;
    case CompressionFormat::TarBzip2:
        return AA_FORMAT_TAR_BZIP2;
    case CompressionFormat::TarXz:
        return AA_FORMAT_TAR_XZ;
    case CompressionFormat::TarZstd:
        return AA_FORMAT_TAR_ZSTD;
    case CompressionFormat::TarLz4:
        return AA_FORMAT_TAR_LZ4;
    }
    return 0;
}
uint32_t methodValue(CompressionMethod method) noexcept {
    switch (method) {
    case CompressionMethod::Store:
        return AA_METHOD_STORE;
    case CompressionMethod::Deflate:
        return AA_METHOD_DEFLATE;
    case CompressionMethod::Lzma2:
        return AA_METHOD_LZMA2;
    case CompressionMethod::Gzip:
        return AA_METHOD_GZIP;
    case CompressionMethod::Bzip2:
        return AA_METHOD_BZIP2;
    case CompressionMethod::Xz:
        return AA_METHOD_XZ;
    case CompressionMethod::Zstd:
        return AA_METHOD_ZSTD;
    case CompressionMethod::Lz4:
        return AA_METHOD_LZ4;
    }
    return 0;
}

CapabilityBatchResult::CapabilityBatchResult(std::vector<ArchiveCapabilities> formats)
    : aa_capability_batch{}, source(std::move(formats)) {
    struct_size = sizeof(aa_capability_batch);
    for (const auto& format : source) {
        for (const auto& method : format.methods) {
            aa_compression_capability row{};
            row.struct_size = sizeof(row);
            row.format = formatValue(format.format);
            row.method = methodValue(method.method);
            row.has_levels = method.minLevel && method.maxLevel && method.defaultLevel;
            row.min_level = method.minLevel.value_or(0);
            row.max_level = method.maxLevel.value_or(0);
            row.default_level = method.defaultLevel.value_or(0);
            row.supports_password = format.supportsPassword;
            row.supports_multithreading = format.supportsMultithreading;
            row.supports_solid = format.supportsSolid;
            row.supports_permissions = format.supportsPermissions;
            row.supports_timestamps = format.supportsTimestamps;
            row.format_name = view(format.name);
            row.method_name = view(method.name);
            row.extension = view(format.extension);
            rows.push_back(row);
        }
    }
    count = static_cast<uint32_t>(rows.size());
    this->capabilities = rows.data();
}
} // namespace ananarchiver::c_api
