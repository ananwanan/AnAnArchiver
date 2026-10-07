#include "ananarchiver/libarchive_reader.h"
#include "ananarchiver/password.h"
#include "ananarchiver/path_security.h"
#include "ananarchiver/unicode.h"
#include "../../core/filesystem/safe_output.h"
#include "../../core/archive/archive_unicode_scope.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ananarchiver {
namespace {

struct ArchiveDeleter {
    void operator()(archive* value) const noexcept {
        if (value != nullptr) {
            archive_read_free(value);
        }
    }
};

using ArchiveHandle = std::unique_ptr<archive, ArchiveDeleter>;

ArchiveError makeError(ArchiveErrorCode code, std::string message,
                       const std::filesystem::path& path = {}) {
    return {code, std::move(message), {}, path};
}

struct InputStream {
    std::ifstream file;
    std::array<char, 64 * 1024> buffer{};
    CancellationToken cancellation;
    la_int64_t fileSize = 0;
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

la_ssize_t readInput(archive* handle, void* clientData, const void** buffer) {
    auto& input = *static_cast<InputStream*>(clientData);
    if (input.cancellation.stop_requested()) {
        archive_set_error(handle, ECANCELED, "Archive operation cancelled");
        return -1;
    }
    if (input.deadline && std::chrono::steady_clock::now() >= *input.deadline) {
        archive_set_error(handle, ETIMEDOUT, "Archive operation exceeded the time limit");
        return -1;
    }
    input.file.read(input.buffer.data(), static_cast<std::streamsize>(input.buffer.size()));
    if (input.file.bad() || (input.file.fail() && !input.file.eof())) {
        archive_set_error(handle, EIO, "Could not read archive input");
        return -1;
    }
    *buffer = input.buffer.data();
    return static_cast<la_ssize_t>(input.file.gcount());
}

la_int64_t seekInput(archive* handle, void* clientData, la_int64_t offset, int whence) {
    auto& input = *static_cast<InputStream*>(clientData);
    if (input.cancellation.stop_requested()) {
        archive_set_error(handle, ECANCELED, "Archive operation cancelled");
        return ARCHIVE_FATAL;
    }
    if (input.deadline && std::chrono::steady_clock::now() >= *input.deadline) {
        archive_set_error(handle, ETIMEDOUT, "Archive operation exceeded the time limit");
        return ARCHIVE_FATAL;
    }
    input.file.clear();
    la_int64_t base = 0;
    if (whence == SEEK_CUR) {
        const auto position = input.file.tellg();
        if (position == std::streampos{-1}) {
            archive_set_error(handle, EIO, "Could not locate archive input position");
            return ARCHIVE_FATAL;
        }
        base = static_cast<la_int64_t>(position);
    } else if (whence == SEEK_END) {
        base = input.fileSize;
    } else if (whence != SEEK_SET) {
        archive_set_error(handle, EINVAL, "Unsupported archive seek mode");
        return ARCHIVE_FATAL;
    }
    // Check the arithmetic and file bounds before seeking. This also prevents
    // metadata skipping from concealing a payload truncated before enumeration.
    if (base < 0 || base > input.fileSize || offset < -base ||
        (offset > 0 && offset > input.fileSize - base)) {
        archive_set_error(handle, EIO, "Archive seek points outside the input file");
        return ARCHIVE_FATAL;
    }
    const auto target = base + offset;
    input.file.seekg(static_cast<std::streamoff>(target), std::ios::beg);
    if (!input.file) {
        archive_set_error(handle, EIO, "Could not seek archive input");
        return ARCHIVE_FATAL;
    }
    return target;
}

la_int64_t skipInput(archive* handle, void* clientData, la_int64_t requested) {
    if (requested <= 0) {
        return 0;
    }
    auto& input = *static_cast<InputStream*>(clientData);
    const auto current = seekInput(handle, clientData, 0, SEEK_CUR);
    if (current < 0) {
        return -1;
    }
    const auto available = input.fileSize - current;
    const auto skip = requested < available ? requested : available;
    return seekInput(handle, clientData, skip, SEEK_CUR) < 0 ? -1 : skip;
}

ArchiveError backendError(archive* handle, int status,
                          const std::filesystem::path& path,
                          CancellationToken cancellation,
                          std::string_view password = {}) {
    if (cancellation.stop_requested() || archive_errno(handle) == ECANCELED) {
        return makeError(ArchiveErrorCode::Cancelled, "Archive operation cancelled", path);
    }
    ArchiveErrorCode code = status == ARCHIVE_WARN ? ArchiveErrorCode::BackendWarning
                                                  : ArchiveErrorCode::CorruptedArchive;
    const int backendErrno = archive_errno(handle);
    if (backendErrno == EIO) {
        code = ArchiveErrorCode::IoError;
    } else if (backendErrno == ENOMEM || backendErrno == ETIMEDOUT) {
        code = ArchiveErrorCode::ResourceLimitExceeded;
    } else if (backendErrno == EACCES || backendErrno == EPERM) {
        code = ArchiveErrorCode::PermissionDenied;
    }
    std::string detail = archive_error_string(handle) == nullptr
                             ? std::string{}
                             : std::string{archive_error_string(handle)};
    // libarchive has no typed password/codec errors; isolate its diagnostic
    // mapping here so the application never parses backend messages.
    if (detail.find("bad CRC") != std::string::npos ||
        detail.find("bad Authentication code") != std::string::npos) {
        code = ArchiveErrorCode::CorruptedArchive;
    } else if (detail.find("Passphrase") != std::string::npos ||
        detail.find("passphrase") != std::string::npos ||
        detail.find("password") != std::string::npos) {
        code = password.empty() ? ArchiveErrorCode::PasswordRequired : ArchiveErrorCode::WrongPassword;
    } else if (detail.find("Unrecognized archive format") != std::string::npos) {
        code = ArchiveErrorCode::InvalidArchive;
    } else if (detail.find("Unsupported") != std::string::npos ||
               detail.find("unsupported") != std::string::npos) {
        code = ArchiveErrorCode::UnsupportedCompression;
    }
    if (!password.empty()) {
        for (auto position = detail.find(password); position != std::string::npos;
             position = detail.find(password, position + 10)) {
            detail.replace(position, password.size(), "[redacted]");
        }
    }
    return {code, status == ARCHIVE_WARN
                      ? (code == ArchiveErrorCode::CorruptedArchive ? "Archive data integrity check failed"
                                                                    : "Archive metadata warning")
                      : "Could not read archive",
            std::move(detail), path};
}

struct ReadSession {
    // Destruction releases the backend before its callback data and stream.
    InputStream input;
    ArchiveHandle handle{archive_read_new()};
    std::string password;
    ~ReadSession() { scrubPassword(password); }
};

ArchiveError sessionError(ReadSession& session, int status,
                          const std::filesystem::path& path, CancellationToken cancellation) {
    if (cancellation.stop_requested()) {
        return makeError(ArchiveErrorCode::Cancelled, "Archive operation cancelled", path);
    }
    if (session.input.deadline && std::chrono::steady_clock::now() >= *session.input.deadline) {
        return makeError(ArchiveErrorCode::ResourceLimitExceeded,
                         "Archive operation exceeded the time limit", path);
    }
    return backendError(session.handle.get(), status, path, cancellation, session.password);
}

Result<void> finishSession(ReadSession& session, const std::filesystem::path& path,
                           CancellationToken cancellation) {
    const int status = archive_read_close(session.handle.get());
    if (status != ARCHIVE_OK) {
        return std::unexpected(sessionError(session, status, path, cancellation));
    }
    return {};
}

Result<void> startSession(ReadSession& session, const std::filesystem::path& path,
                          CancellationToken cancellation, std::string_view password = {},
                          std::optional<std::chrono::steady_clock::time_point> deadline = {}) {
    if (cancellation.stop_requested()) {
        return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                         "Archive operation cancelled", path));
    }
    std::error_code statusError;
    const auto status = std::filesystem::status(path, statusError);
    if (statusError) {
        const auto code = statusError == std::errc::no_such_file_or_directory
                              ? ArchiveErrorCode::FileNotFound
                              : (statusError == std::errc::permission_denied
                                     ? ArchiveErrorCode::PermissionDenied
                                     : ArchiveErrorCode::IoError);
        return std::unexpected(ArchiveError{code, "Could not access archive file",
                                            statusError.message(), path});
    }
    if (!std::filesystem::exists(status)) {
        return std::unexpected(makeError(ArchiveErrorCode::FileNotFound,
                                         "Archive file does not exist", path));
    }
    if (!std::filesystem::is_regular_file(status)) {
        return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                         "Archive input must be a regular file", path));
    }
    const auto fileSize = std::filesystem::file_size(path, statusError);
    if (statusError) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Could not determine archive size",
                                            statusError.message(), path});
    }
    if (fileSize > static_cast<std::uintmax_t>((std::numeric_limits<la_int64_t>::max)())) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Archive exceeds the supported file size", path));
    }
    session.input.fileSize = static_cast<la_int64_t>(fileSize);
    session.input.cancellation = cancellation;
    session.input.deadline = deadline;
    session.password = password;
    errno = 0;
    session.input.file.open(path, std::ios::binary);
    if (!session.input.file.is_open()) {
        const int openError = errno;
        return std::unexpected(makeError(openError == EACCES || openError == EPERM
                                             ? ArchiveErrorCode::PermissionDenied
                                             : ArchiveErrorCode::IoError,
                                         "Could not open archive input", path));
    }
    if (!session.handle) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Could not allocate archive reader", path));
    }
    if (!session.password.empty()) {
        const int supplied = archive_read_add_passphrase(session.handle.get(), session.password.c_str());
        if (supplied != ARCHIVE_OK) {
            return std::unexpected(sessionError(session, supplied, path, cancellation));
        }
    }
    using Configure = int (*)(archive*);
    // Register only filters implemented internally or backed by the project's
    // bundled codecs. support_filter_all also enables unpinned LRZIP/GRZIP/LZOP
    // subprocess helpers, which must not become implicit runtime dependencies.
    for (const Configure configure : {archive_read_support_filter_none,
                                       archive_read_support_filter_gzip,
                                       archive_read_support_filter_bzip2,
                                       archive_read_support_filter_compress,
                                       archive_read_support_filter_lzma,
                                       archive_read_support_filter_xz,
                                       archive_read_support_filter_lzip,
                                       archive_read_support_filter_zstd,
                                       archive_read_support_filter_lz4,
                                       archive_read_support_filter_uu,
                                       archive_read_support_filter_rpm,
                                       archive_read_support_format_all,
                                       archive_read_support_format_raw}) {
        const int result = configure(session.handle.get());
        if (result != ARCHIVE_OK) {
            return std::unexpected(sessionError(session, result, path, cancellation));
        }
    }
    const int seekConfigured = archive_read_set_seek_callback(session.handle.get(), seekInput);
    if (seekConfigured != ARCHIVE_OK) {
        return std::unexpected(sessionError(session, seekConfigured, path, cancellation));
    }
    const int result = archive_read_open2(session.handle.get(), &session.input,
                                          nullptr, readInput, skipInput, nullptr);
    if (result != ARCHIVE_OK) {
        return std::unexpected(sessionError(session, result, path, cancellation));
    }
    return {};
}

ArchiveFormat detectedFormat(archive* handle) {
    switch (archive_format(handle) & ARCHIVE_FORMAT_BASE_MASK) {
    case ARCHIVE_FORMAT_ZIP: return ArchiveFormat::Zip;
    case ARCHIVE_FORMAT_7ZIP: return ArchiveFormat::SevenZip;
    case ARCHIVE_FORMAT_RAR: return ArchiveFormat::Rar;
    case ARCHIVE_FORMAT_RAR_V5: return ArchiveFormat::Rar5;
    case ARCHIVE_FORMAT_TAR: return ArchiveFormat::Tar;
    case ARCHIVE_FORMAT_CAB: return ArchiveFormat::Cab;
    case ARCHIVE_FORMAT_CPIO: return ArchiveFormat::Cpio;
    case ARCHIVE_FORMAT_AR: return ArchiveFormat::Ar;
    case ARCHIVE_FORMAT_ISO9660: return ArchiveFormat::Iso9660;
    case ARCHIVE_FORMAT_LHA: return ArchiveFormat::Lha;
    case ARCHIVE_FORMAT_XAR: return ArchiveFormat::Xar;
    case ARCHIVE_FORMAT_RAW:
        switch (archive_filter_code(handle, 0)) {
        case ARCHIVE_FILTER_GZIP: return ArchiveFormat::Gzip;
        case ARCHIVE_FILTER_BZIP2: return ArchiveFormat::Bzip2;
        case ARCHIVE_FILTER_XZ: return ArchiveFormat::Xz;
        case ARCHIVE_FILTER_ZSTD: return ArchiveFormat::Zstd;
        case ARCHIVE_FILTER_LZ4: return ArchiveFormat::Lz4;
        case ARCHIVE_FILTER_NONE: return ArchiveFormat::Unknown;
        default: return ArchiveFormat::Other;
        }
    case ARCHIVE_FORMAT_EMPTY:
    case 0: return ArchiveFormat::Unknown;
    default: return ArchiveFormat::Other;
    }
}

Result<std::string> entryPath(archive_entry* entry, std::size_t maxPathBytes,
                             const std::filesystem::path& archivePath) {
    const char* pathname = archive_entry_pathname_utf8(entry);
    if (pathname == nullptr) {
        pathname = archive_entry_pathname(entry);
    }
    if (pathname == nullptr) {
        return std::unexpected(makeError(ArchiveErrorCode::InvalidPathEncoding,
                                         "Archive entry has no UTF-8 pathname", archivePath));
    }
    // Bound our scan and allocation independently of backend-owned metadata.
    std::size_t length = 0;
    while (length <= maxPathBytes && pathname[length] != '\0') {
        ++length;
    }
    if (length > maxPathBytes) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Archive entry pathname exceeds the limit", archivePath));
    }
    const std::string_view path{pathname, length};
    if (!isValidUtf8(path)) {
        return std::unexpected(makeError(ArchiveErrorCode::InvalidPathEncoding,
                                         "Archive entry pathname is not valid UTF-8", archivePath));
    }
    return std::string{path};
}

Result<ArchiveEntry> entryMetadata(archive_entry* entry, ArchiveReadLimits limits,
                                   const std::filesystem::path& path) {
    auto name = entryPath(entry, limits.maxPathBytes, path);
    if (!name) {
        return std::unexpected(std::move(name.error()));
    }
    ArchiveEntry result;
    result.path = std::move(*name);
    const auto size = archive_entry_size(entry);
    result.sizeKnown = archive_entry_size_is_set(entry) != 0;
    if (result.sizeKnown && size < 0) {
        return std::unexpected(makeError(ArchiveErrorCode::CorruptedArchive,
                                         "Archive entry has a negative size", path));
    }
    result.size = size > 0 ? static_cast<std::uint64_t>(size) : 0;
    if (archive_entry_hardlink(entry) != nullptr) {
        result.type = EntryType::Hardlink;
    } else {
        switch (archive_entry_filetype(entry)) {
        case AE_IFREG: result.type = EntryType::RegularFile; break;
        case AE_IFDIR: result.type = EntryType::Directory; break;
        case AE_IFLNK: result.type = EntryType::Symlink; break;
        default: result.type = EntryType::Other; break;
        }
    }
    // libarchive does not expose a reliable per-entry compressed size.
    if (archive_entry_mtime_is_set(entry)) {
        using Clock = std::chrono::system_clock;
        using Seconds = std::chrono::seconds;
        const auto seconds = archive_entry_mtime(entry);
        const auto nanos = archive_entry_mtime_nsec(entry);
        const auto minimum = std::chrono::duration_cast<Seconds>(Clock::duration::min()).count();
        const auto maximum = std::chrono::duration_cast<Seconds>(Clock::duration::max()).count();
        if (seconds > minimum && seconds < maximum && nanos >= 0 && nanos < 1'000'000'000) {
            result.modified = Clock::time_point{
                std::chrono::duration_cast<Clock::duration>(Seconds{seconds}) +
                std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds{nanos})};
        }
    }
    return result;
}

std::string outputKey(const std::filesystem::path& path);

bool beneath(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto childPart = child.begin();
    for (const auto& parentPart : parent) {
        if (childPart == child.end() || outputKey(*childPart) != outputKey(parentPart)) return false;
        ++childPart;
    }
    return true;
}

std::string outputKey(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    std::string key{utf8.begin(), utf8.end()};
#ifdef _WIN32
    // Windows' native lookup catches further Unicode case aliases. Folding
    // ASCII here also catches ordinary duplicate names before disk commits.
    for (auto& c : key) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
#endif
    return key;
}

Result<std::string> linkText(archive_entry* entry, bool hardlink,
                             std::size_t limit, const std::filesystem::path& archivePath) {
    const char* target = hardlink ? archive_entry_hardlink_utf8(entry)
                                  : archive_entry_symlink_utf8(entry);
    if (target == nullptr) {
        target = hardlink ? archive_entry_hardlink(entry) : archive_entry_symlink(entry);
    }
    if (target == nullptr) {
        return std::unexpected(makeError(ArchiveErrorCode::InvalidPathEncoding,
                                         "Archive link has no UTF-8 target", archivePath));
    }
    std::size_t size = 0;
    while (size <= limit && target[size] != '\0') ++size;
    if (size > limit) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Archive link target exceeds the limit", archivePath));
    }
    if (!isValidUtf8(std::string_view{target, size})) {
        return std::unexpected(makeError(ArchiveErrorCode::InvalidPathEncoding,
                                         "Archive link target is not valid UTF-8", archivePath));
    }
    return std::string{target, size};
}

struct PlannedOutput {
    ArchiveEntry entry;
    std::filesystem::path relative;
    std::filesystem::path linkTarget;
    std::string stagedName;
    bool replace = false;
};

Result<void> extractArchive(const std::filesystem::path& path, ArchiveReadLimits readLimits,
                             std::string_view password, const ExtractionRequest& request,
                             const ProgressCallback& callback, CancellationToken cancellation) {
    using detail::OutputKind;
    const auto& limits = request.limits;
    if (limits.maxEntries == 0 || limits.maxPathBytes == 0 || limits.maxDepth == 0 ||
        limits.maxMetadataBytes == 0 || limits.maxElapsed <= std::chrono::milliseconds::zero()) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Extraction limits must be finite and positive", path));
    }
    if (request.destination.empty()) {
        return std::unexpected(makeError(ArchiveErrorCode::UnsafePath,
                                         "Extraction destination is empty", path));
    }
    if (pathToUtf8(request.destination).size() > limits.maxPathBytes) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Extraction destination exceeds the pathname limit", path));
    }
    const auto startedAt = std::chrono::steady_clock::now();
    const auto maxDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::time_point::max() - startedAt);
    const auto deadline = startedAt + (std::min)(limits.maxElapsed, maxDuration);
    readLimits.maxEntries = (std::min)(readLimits.maxEntries, limits.maxEntries);
    readLimits.maxPathBytes = (std::min)(readLimits.maxPathBytes, limits.maxPathBytes);
    readLimits.maxMetadataBytes = (std::min)(readLimits.maxMetadataBytes, limits.maxMetadataBytes);
    std::uint64_t retainedBytes = 0;
    auto retain = [&](std::uint64_t bytes) -> Result<void> {
        if (retainedBytes > readLimits.maxMetadataBytes ||
            bytes > readLimits.maxMetadataBytes - retainedBytes) {
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Extraction metadata exceeds the memory budget", path));
        }
        retainedBytes += bytes;
        return {};
    };
    auto interrupted = [&]() -> Result<void> {
        if (cancellation.stop_requested()) {
            return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                             "Archive extraction cancelled", path));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Archive extraction exceeded the time limit", path));
        }
        return {};
    };
    const auto destinationBytes = static_cast<std::uint64_t>(request.destination.native().size()) *
                                  sizeof(std::filesystem::path::value_type);
    const auto destinationDepth = static_cast<std::uint64_t>(std::distance(
                                   request.destination.begin(), request.destination.end()));
    if (destinationDepth > limits.maxDepth) {
        return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Extraction destination exceeds the depth limit", path));
    }
    auto rootBudget = retain(destinationBytes * (destinationDepth + 3U) + 2048U);
    if (!rootBudget) return rootBudget;
    std::unordered_set<std::uint64_t> selected;
    std::unordered_set<std::string> selectedDirectories;
    if (request.selectedIndices) {
        if (request.selectedIndices->size() > readLimits.maxEntries) {
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Entry selection exceeds the limit", path));
        }
        auto budget = retain(static_cast<std::uint64_t>(request.selectedIndices->size()) * 64U);
        if (!budget) return budget;
        selected.insert(request.selectedIndices->begin(), request.selectedIndices->end());
        // A metadata-only pass resolves selected directories without relying on
        // header order. This costs a second stream pass for sequential formats,
        // but never retains payloads or a full archive metadata index.
        ReadSession selectionSession;
        auto opened = startSession(selectionSession, path, cancellation, password, deadline);
        if (!opened) return opened;
        std::uint64_t count = 0;
        std::uint64_t declaredBytes = 0;
        for (;;) {
            auto allowed = interrupted();
            if (!allowed) return allowed;
            archive_entry* entry = nullptr;
            const int status = archive_read_next_header(selectionSession.handle.get(), &entry);
            if (status == ARCHIVE_EOF) break;
            if (status != ARCHIVE_OK) {
                return std::unexpected(sessionError(selectionSession, status, path, cancellation));
            }
            if (count >= readLimits.maxEntries) {
                return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                                 "Archive entry count exceeds the limit", path));
            }
            auto metadata = entryMetadata(entry, readLimits, path);
            if (!metadata) return std::unexpected(std::move(metadata.error()));
            if (metadata->size > limits.maxEntryBytes || declaredBytes > limits.maxTotalBytes ||
                metadata->size > limits.maxTotalBytes - declaredBytes) {
                return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                                 "Archive declared size exceeds the extraction budget", path));
            }
            declaredBytes += metadata->size;
            if (selected.contains(count) && metadata->type == EntryType::Directory) {
                auto directory = secureArchivePath(metadata->path, limits.maxPathBytes, limits.maxDepth);
                if (!directory) return std::unexpected(std::move(directory.error()));
                auto space = retain(static_cast<std::uint64_t>(directory->native().size()) *
                                     sizeof(std::filesystem::path::value_type) +
                                     static_cast<std::uint64_t>(metadata->path.size()) + 256U);
                if (!space) return space;
                selectedDirectories.insert(outputKey(*directory));
            }
            ++count;
            const int skipped = archive_read_data_skip(selectionSession.handle.get());
            if (skipped != ARCHIVE_OK && skipped != ARCHIVE_EOF) {
                return std::unexpected(sessionError(selectionSession, skipped, path, cancellation));
            }
        }
        if (std::any_of(selected.begin(), selected.end(), [count](auto index) { return index >= count; })) {
            return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                             "Selected entry no longer exists in the archive", path));
        }
        auto closed = finishSession(selectionSession, path, cancellation);
        if (!closed) return closed;
    }
    auto openedOutput = detail::SafeOutput::open(request.destination);
    if (!openedOutput) return std::unexpected(std::move(openedOutput.error()));
    auto output = std::move(*openedOutput);
    std::unique_ptr<detail::OutputFile> activeFile;
    auto failed = [&](ArchiveError error) -> Result<void> {
        activeFile.reset();
        auto rolledBack = output->rollback();
        if (!rolledBack) {
            error.code = ArchiveErrorCode::CleanupFailed;
            error.message += "; extraction cleanup could not finish; inspect the remaining-file location";
            error.backendMessage += "; " + rolledBack.error().message;
            error.path = rolledBack.error().path;
        }
        return std::unexpected(std::move(error));
    };
    try {
        ReadSession session;
        auto started = startSession(session, path, cancellation, password, deadline);
        if (!started) return failed(std::move(started.error()));
        ExtractionProgress progress;
        progress.totalBytes = request.totalBytes;
        progress.totalEntries = request.totalEntries;
        auto notifyProgress = [&]() -> Result<void> {
            auto allowed = interrupted();
            if (!allowed) return allowed;
            if (callback && !callback(progress)) {
                return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                                 "Archive extraction cancelled", path));
            }
            return interrupted();
        };
        std::vector<PlannedOutput> plan;
        std::unordered_map<std::string, std::size_t> plannedPaths;
        std::unordered_set<std::string> requiredDirectories;
        std::unordered_map<std::string, std::filesystem::path> directoryAliases;
        std::unordered_set<std::string> skippedDirectories;
        auto redirected = [&](const std::filesystem::path& original) {
            auto prefix = original;
            while (!prefix.empty()) {
                const auto alias = directoryAliases.find(outputKey(prefix));
                if (alias != directoryAliases.end()) {
                    auto result = alias->second;
                    auto suffix = original.begin();
                    std::advance(suffix, std::distance(prefix.begin(), prefix.end()));
                    for (; suffix != original.end(); ++suffix) result /= *suffix;
                    return result;
                }
                prefix = prefix.parent_path();
            }
            return original;
        };
        auto availableRename = [&](const std::filesystem::path& relative) -> Result<std::filesystem::path> {
            for (unsigned suffix = 1; suffix <= 10'000; ++suffix) {
                auto allowed = interrupted();
                if (!allowed) return std::unexpected(std::move(allowed.error()));
                auto candidate = relative.parent_path() /
                    pathFromUtf8(pathToUtf8(relative.stem()) + " (" + std::to_string(suffix) + ")" +
                                 pathToUtf8(relative.extension()));
                auto candidateStatus = output->status(candidate);
                if (!candidateStatus) return std::unexpected(std::move(candidateStatus.error()));
                const auto key = outputKey(candidate);
                if (candidateStatus->kind == OutputKind::Missing &&
                    !plannedPaths.contains(key) && !requiredDirectories.contains(key)) {
                    return candidate;
                }
            }
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Could not find an available renamed output", path));
        };
        auto resolveDirectoryParents = [&](const ArchiveEntry& entry,
                                            const std::filesystem::path& original) -> Result<bool> {
            auto possibleSkipped = original;
            while (!possibleSkipped.empty()) {
                if (skippedDirectories.contains(outputKey(possibleSkipped))) return true;
                possibleSkipped = possibleSkipped.parent_path();
            }
            auto originalPrefix = std::filesystem::path{};
            const auto parentPath = entry.type == EntryType::Directory ? original : original.parent_path();
            for (const auto& component : parentPath) {
                originalPrefix /= component;
                const auto actualPrefix = redirected(originalPrefix);
                auto existing = output->status(actualPrefix);
                if (!existing) return std::unexpected(std::move(existing.error()));
                auto planned = plannedPaths.find(outputKey(actualPrefix));
                if (planned != plannedPaths.end()) {
                    const auto type = plan[planned->second].entry.type;
                    if (type == EntryType::Symlink || type == EntryType::Hardlink) {
                        return std::unexpected(makeError(ArchiveErrorCode::UnsafePath,
                                                         "An archive link cannot be an output ancestor", path));
                    }
                    existing->kind = type == EntryType::Directory ? OutputKind::Directory : OutputKind::RegularFile;
                }
                if (existing->kind != OutputKind::RegularFile) continue;
                ArchiveEntry directoryEntry;
                directoryEntry.path = pathToUtf8(originalPrefix);
                directoryEntry.index = entry.index;
                directoryEntry.type = EntryType::Directory;
                auto policy = request.overwrite;
                if (policy == OverwritePolicy::Ask && request.onConflict) {
                    policy = request.onConflict(ExtractionConflict{directoryEntry, output->root() / actualPrefix});
                    auto continuing = interrupted();
                    if (!continuing) return std::unexpected(std::move(continuing.error()));
                }
                if (policy == OverwritePolicy::Skip || policy == OverwritePolicy::OverwriteIfNewer) {
                    auto space = retain(static_cast<std::uint64_t>(entry.path.size()) + 256U);
                    if (!space) return std::unexpected(std::move(space.error()));
                    skippedDirectories.insert(outputKey(originalPrefix));
                    return true;
                }
                if (policy != OverwritePolicy::Rename) {
                    return std::unexpected(makeError(ArchiveErrorCode::OutputExists,
                                                     "An existing file conflicts with an output directory",
                                                     output->root() / actualPrefix));
                }
                auto candidate = availableRename(actualPrefix);
                if (!candidate) return std::unexpected(std::move(candidate.error()));
                auto space = retain((static_cast<std::uint64_t>(originalPrefix.native().size()) +
                                      static_cast<std::uint64_t>(candidate->native().size())) *
                                     sizeof(std::filesystem::path::value_type) +
                                     static_cast<std::uint64_t>(entry.path.size()) + 256U);
                if (!space) return std::unexpected(std::move(space.error()));
                directoryAliases.emplace(outputKey(originalPrefix), std::move(*candidate));
            }
            return false;
        };
        std::uint64_t count = 0;
        std::uint64_t consumedBytes = 0;
        for (;;) {
            auto allowed = interrupted();
            if (!allowed) return failed(std::move(allowed.error()));
            archive_entry* entry = nullptr;
            const int status = archive_read_next_header(session.handle.get(), &entry);
            if (status == ARCHIVE_EOF) break;
            if (status != ARCHIVE_OK) {
                return failed(sessionError(session, status, path, cancellation));
            }
            if (detectedFormat(session.handle.get()) == ArchiveFormat::Unknown) {
                return failed(makeError(ArchiveErrorCode::InvalidArchive,
                                         "Input is not a recognized archive", path));
            }
            if (count >= readLimits.maxEntries) {
                return failed(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Archive entry count exceeds the limit", path));
            }
            auto metadata = entryMetadata(entry, readLimits, path);
            if (!metadata) return failed(std::move(metadata.error()));
            metadata->index = count++;
            progress.currentFile = metadata->path;
            bool chosen = !request.selectedIndices || selected.contains(metadata->index);
            std::filesystem::path relative;
            if (chosen || !selectedDirectories.empty()) {
                auto normalized = secureArchivePath(metadata->path, limits.maxPathBytes, limits.maxDepth);
                if (!normalized) return failed(std::move(normalized.error()));
                relative = std::move(*normalized);
                if (!chosen) {
                    chosen = selectedDirectories.contains("");
                    auto prefix = relative;
                    while (!chosen && !prefix.empty()) {
                        chosen = selectedDirectories.contains(outputKey(prefix));
                        prefix = prefix.parent_path();
                    }
                }
            }
            std::optional<PlannedOutput> item;
            bool skip = !chosen;
            const auto originalRelative = relative;
            if (chosen) {
                if (relative.empty() && metadata->type != EntryType::Directory) {
                    return failed(makeError(ArchiveErrorCode::UnsafePath,
                                             "A non-directory entry names the extraction root", path));
                }
                auto authorized = detail::validateOutputPath(relative);
                if (!authorized) return failed(std::move(authorized.error()));
                if (metadata->type == EntryType::Other) {
                    return failed(makeError(ArchiveErrorCode::UnsupportedOperation,
                                             "Special files cannot be extracted", path));
                }
                const bool isLink = metadata->type == EntryType::Symlink || metadata->type == EntryType::Hardlink;
                skip = isLink && request.links == LinkPolicy::Skip;
                if (!skip) {
                    auto resolved = resolveDirectoryParents(*metadata, originalRelative);
                    if (!resolved) return failed(std::move(resolved.error()));
                    skip = *resolved;
                    relative = redirected(originalRelative);
                }
                if (!skip && !relative.empty()) {
                    const auto pathBytes = static_cast<std::uint64_t>(relative.native().size()) *
                                           sizeof(std::filesystem::path::value_type);
                    const auto depth = static_cast<std::uint64_t>(std::distance(relative.begin(), relative.end()));
                    // Includes plan/map strings, staging/backup names, and the
                    // maximum sum of newly-created parent directory paths.
                    const auto journalBytes = pathBytes * (depth + 8U) +
                                              static_cast<std::uint64_t>(metadata->path.size()) + 1024U;
                    auto space = retain(journalBytes);
                    if (!space) return failed(std::move(space.error()));
                    item.emplace(PlannedOutput{*metadata, relative, {}, {}, false});
                    auto existing = output->status(relative);
                    if (!existing) return failed(std::move(existing.error()));
                    if (requiredDirectories.contains(outputKey(relative))) {
                        if (isLink) return failed(makeError(ArchiveErrorCode::UnsafePath,
                                                           "An archive link cannot be an output ancestor", path));
                        existing->kind = OutputKind::Directory;
                    }
                    auto duplicate = plannedPaths.find(outputKey(relative));
                    if (duplicate != plannedPaths.end()) {
                        const auto& previous = plan[duplicate->second];
                        if (previous.entry.type == EntryType::Symlink || previous.entry.type == EntryType::Hardlink || isLink) {
                            return failed(makeError(ArchiveErrorCode::UnsafePath,
                                                     "Archive link collides with another selected entry", path));
                        }
                        existing->kind = previous.entry.type == EntryType::Directory
                                             ? OutputKind::Directory : OutputKind::RegularFile;
                        existing->modified = previous.entry.modified;
                    }
                    if (existing->kind != OutputKind::Missing &&
                        !(metadata->type == EntryType::Directory && existing->kind == OutputKind::Directory)) {
                        auto policy = request.overwrite;
                        if (policy == OverwritePolicy::Ask && request.onConflict) {
                            policy = request.onConflict(ExtractionConflict{*metadata, output->root() / relative});
                            auto continuing = interrupted();
                            if (!continuing) return failed(std::move(continuing.error()));
                        }
                        if (policy == OverwritePolicy::Ask) {
                            return failed(makeError(ArchiveErrorCode::OutputExists,
                                                     "Extraction output already exists", output->root() / relative));
                        }
                        if (policy == OverwritePolicy::Skip ||
                            (policy == OverwritePolicy::OverwriteIfNewer &&
                             (!metadata->modified || !existing->modified || *metadata->modified <= *existing->modified))) {
                            skip = true;
                            item.reset();
                        } else if (policy == OverwritePolicy::Rename) {
                            auto candidate = availableRename(relative);
                            if (!candidate) return failed(std::move(candidate.error()));
                            item->relative = std::move(*candidate);
                        } else {
                            if (existing->kind == OutputKind::Directory || metadata->type == EntryType::Directory) {
                                return failed(makeError(ArchiveErrorCode::OutputExists,
                                                         "Cannot replace a directory with a file or a file with a directory",
                                                         output->root() / relative));
                            }
                            item->replace = true;
                        }
                    }
                    if (item && isLink) {
                        auto text = linkText(entry, metadata->type == EntryType::Hardlink,
                                              limits.maxPathBytes, path);
                        if (!text) return failed(std::move(text.error()));
                        auto target = secureLinkTarget(originalRelative, *text,
                                                       metadata->type == EntryType::Hardlink,
                                                       limits.maxPathBytes, limits.maxDepth);
                        if (!target) return failed(std::move(target.error()));
                        auto authorizedTarget = detail::validateOutputPath(*target);
                        if (!authorizedTarget) return failed(std::move(authorizedTarget.error()));
                        // Reject self/ancestor directory cycles as well as escapes.
                        if (beneath(originalRelative, *target)) {
                            return failed(makeError(ArchiveErrorCode::UnsafePath,
                                                     "Archive link creates an ancestor cycle", path));
                        }
                        auto targetSpace = retain(static_cast<std::uint64_t>(target->native().size()) *
                                                   sizeof(std::filesystem::path::value_type) + 128U);
                        if (!targetSpace) return failed(std::move(targetSpace.error()));
                        item->linkTarget = std::move(*target);
                    }
                    if (item && metadata->type == EntryType::RegularFile) {
                        auto staged = output->stage();
                        if (!staged) return failed(std::move(staged.error()));
                        item->stagedName = std::move(staged->name);
                        activeFile = std::move(staged->file);
                    }
                }
            }
            auto notified = notifyProgress();
            if (!notified) return failed(std::move(notified.error()));
            std::uint64_t entryBytes = 0;
            std::uint64_t logicalEnd = 0;
            std::uint64_t entryBudget = metadata->size;
            if (entryBudget > limits.maxEntryBytes || consumedBytes > limits.maxTotalBytes ||
                entryBudget > limits.maxTotalBytes - consumedBytes) {
                return failed(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Archive expanded size exceeds the extraction budget", path));
            }
            const bool sparse = archive_entry_sparse_count(entry) > 0;
            for (;;) {
                auto continuing = interrupted();
                if (!continuing) return failed(std::move(continuing.error()));
                const void* block = nullptr;
                std::size_t blockSize = 0;
                la_int64_t offset = 0;
                const int read = archive_read_data_block(session.handle.get(), &block, &blockSize, &offset);
                if (read == ARCHIVE_EOF) break;
                if (read != ARCHIVE_OK) {
                    return failed(sessionError(session, read, path, cancellation));
                }
                if (offset < 0 || static_cast<std::uint64_t>(offset) > limits.maxEntryBytes ||
                    blockSize > limits.maxEntryBytes - static_cast<std::uint64_t>(offset) ||
                    entryBytes > limits.maxEntryBytes || blockSize > limits.maxEntryBytes - entryBytes) {
                    return failed(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Archive entry expanded size exceeds the extraction budget", path));
                }
                const auto end = static_cast<std::uint64_t>(offset) + blockSize;
                if (static_cast<std::uint64_t>(offset) < logicalEnd ||
                    (metadata->sizeKnown && end > metadata->size)) {
                    return failed(makeError(ArchiveErrorCode::CorruptedArchive,
                                             "Archive entry has inconsistent data offsets or size", path));
                }
                logicalEnd = end;
                entryBytes += blockSize;
                entryBudget = (std::max)({entryBudget, logicalEnd, entryBytes});
                if (entryBudget > limits.maxTotalBytes - consumedBytes) {
                    return failed(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Archive expanded size exceeds the extraction budget", path));
                }
                if (activeFile) {
                    auto written = activeFile->write(std::span{
                        static_cast<const std::byte*>(block), blockSize}, static_cast<std::uint64_t>(offset));
                    if (!written) return failed(std::move(written.error()));
                }
                if (chosen) progress.processedBytes += blockSize;
                auto updated = notifyProgress();
                if (!updated) return failed(std::move(updated.error()));
            }
            if (metadata->type == EntryType::RegularFile && metadata->sizeKnown &&
                !sparse && logicalEnd != metadata->size) {
                return failed(makeError(ArchiveErrorCode::CorruptedArchive,
                                         "Archive entry ended before its declared size", path));
            }
            consumedBytes += entryBudget;
            if (activeFile) {
                const auto logicalSize = metadata->sizeKnown ? metadata->size : logicalEnd;
                auto finished = activeFile->finish(logicalSize, metadata->modified);
                if (!finished) return failed(std::move(finished.error()));
                activeFile.reset();
            }
            if (item) {
                std::filesystem::path prefix;
                for (const auto& component : item->relative.parent_path()) {
                    prefix /= component;
                    requiredDirectories.insert(outputKey(prefix));
                }
                const auto key = outputKey(item->relative);
                auto duplicate = plannedPaths.find(key);
                if (duplicate != plannedPaths.end()) {
                    plan[duplicate->second] = std::move(*item);
                } else {
                    plannedPaths.emplace(key, plan.size());
                    plan.push_back(std::move(*item));
                }
            }
            if (chosen) ++progress.processedEntries;
            auto updated = notifyProgress();
            if (!updated) return failed(std::move(updated.error()));
        }
        if (detectedFormat(session.handle.get()) == ArchiveFormat::Unknown) {
            return failed(makeError(ArchiveErrorCode::InvalidArchive,
                                     "Input is not a recognized archive", path));
        }
        auto closed = finishSession(session, path, cancellation);
        if (!closed) return failed(std::move(closed.error()));
        // Never create a link until every file payload has passed validation.
        // Parent traversal uses native no-follow handles during each commit.
        for (const auto& item : plan) {
            if (item.entry.type != EntryType::Directory && item.entry.type != EntryType::RegularFile) continue;
            auto continuing = notifyProgress();
            if (!continuing) return failed(std::move(continuing.error()));
            auto committed = item.entry.type == EntryType::Directory
                ? output->directory(item.relative)
                : output->install(item.stagedName, item.relative, item.replace);
            if (!committed) return failed(std::move(committed.error()));
        }
        for (const auto& item : plan) {
            if (item.entry.type != EntryType::Symlink && item.entry.type != EntryType::Hardlink) continue;
            auto continuing = notifyProgress();
            if (!continuing) return failed(std::move(continuing.error()));
            const auto targetPath = redirected(item.linkTarget);
            auto target = output->status(targetPath);
            if (!target) return failed(std::move(target.error()));
            if ((request.links == LinkPolicy::Safe && target->kind == OutputKind::Missing) ||
                (item.entry.type == EntryType::Hardlink && target->kind != OutputKind::RegularFile)) {
                return failed(makeError(ArchiveErrorCode::UnsafePath,
                                         "Archive link target is not an available safe output", output->root() / targetPath));
            }
            auto committed = output->link(item.relative, targetPath,
                                           item.entry.type == EntryType::Hardlink,
                                           target->kind == OutputKind::Directory, item.replace);
            if (!committed) return failed(std::move(committed.error()));
        }
        progress.totalBytes = progress.processedBytes;
        progress.totalEntries = progress.processedEntries;
        auto continuing = notifyProgress();
        if (!continuing) return failed(std::move(continuing.error()));
        return output->finish();
    } catch (const std::bad_alloc&) {
        return failed(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                 "Not enough memory to extract archive", path));
    } catch (const std::exception&) {
        // Callback exceptions may include arbitrary caller text, including
        // secrets. Deliberately omit what() from the public error diagnostic.
        return failed(makeError(ArchiveErrorCode::BackendError,
                                 "Archive extraction or callback failed", path));
    } catch (...) {
        return failed(makeError(ArchiveErrorCode::Unknown,
                                 "Archive extraction callback failed", path));
    }
}

class LibArchiveReader final : public ArchiveReader {
public:
    explicit LibArchiveReader(ArchiveReadLimits limits) : limits_(limits) {}
    ~LibArchiveReader() override { scrubPassword(password_); }

    Result<ArchiveInfo> open(const std::filesystem::path& file,
                             CancellationToken cancellation) override {
        return open(file, std::string_view{}, cancellation);
    }

    Result<ArchiveInfo> open(const std::filesystem::path& file, std::string_view password,
                             CancellationToken cancellation) override {
        path_.clear();
        scrubPassword(password_);
        try {
            auto unicode = detail::ArchiveUnicodeScope::enter();
            if (!unicode) return std::unexpected(std::move(unicode.error()));
            ReadSession session;
            auto started = startSession(session, file, cancellation, password);
            if (!started) {
                return std::unexpected(std::move(started.error()));
            }
            archive_entry* entry = nullptr;
            const int status = archive_read_next_header(session.handle.get(), &entry);
            if (status != ARCHIVE_OK && status != ARCHIVE_EOF) {
                return std::unexpected(backendError(session.handle.get(), status, file,
                                                     cancellation, session.password));
            }
            const auto format = detectedFormat(session.handle.get());
            if (format == ArchiveFormat::Unknown) {
                return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                                 "Input is not a recognized archive", file));
            }
            if (status == ARCHIVE_OK) {
                auto metadata = entryMetadata(entry, limits_, file);
                if (!metadata) {
                    return std::unexpected(std::move(metadata.error()));
                }
            }
            ArchiveInfo info;
            info.path = file;
            info.format = format;
            const char* name = archive_format_name(session.handle.get());
            info.formatName = name == nullptr ? std::string{archiveFormatName(format)}
                                              : std::string{name};
            if ((archive_format(session.handle.get()) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_RAW) {
                info.formatName = archiveFormatName(format);
            }
            info.encrypted = archive_read_has_encrypted_entries(session.handle.get()) > 0 ||
                             (entry != nullptr && archive_entry_is_encrypted(entry) > 0);
            if (cancellation.stop_requested()) {
                return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                                 "Archive operation cancelled", file));
            }
            auto closed = finishSession(session, file, cancellation);
            if (!closed) {
                return std::unexpected(std::move(closed.error()));
            }
            path_ = file;
            password_ = password;
            return info;
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Not enough memory to read archive metadata", file));
        } catch (const std::exception& error) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                                "Could not open archive", error.what(), file});
        }
    }

    Result<void> enumerate(const EntryCallback& callback,
                           CancellationToken cancellation) override {
        if (path_.empty()) {
            return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                             "Open an archive before enumerating entries"));
        }
        if (!callback) {
            return std::unexpected(makeError(ArchiveErrorCode::UnsupportedOperation,
                                             "Entry callback must not be empty", path_));
        }
        try {
            auto unicode = detail::ArchiveUnicodeScope::enter();
            if (!unicode) return std::unexpected(std::move(unicode.error()));
            ReadSession session;
            auto started = startSession(session, path_, cancellation, password_);
            if (!started) {
                return std::unexpected(std::move(started.error()));
            }
            std::uint64_t count = 0;
            std::uint64_t metadataBytes = 0;
            for (;;) {
                if (cancellation.stop_requested()) {
                    return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                                     "Archive operation cancelled", path_));
                }
                archive_entry* entry = nullptr;
                const int status = archive_read_next_header(session.handle.get(), &entry);
                if (status == ARCHIVE_EOF) {
                    if (detectedFormat(session.handle.get()) == ArchiveFormat::Unknown) {
                        return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                                         "Input is not a recognized archive", path_));
                    }
                    return finishSession(session, path_, cancellation);
                }
                if (status != ARCHIVE_OK) {
                    return std::unexpected(backendError(session.handle.get(), status, path_,
                                                         cancellation, session.password));
                }
                if (detectedFormat(session.handle.get()) == ArchiveFormat::Unknown) {
                    return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                                     "Input is not a recognized archive", path_));
                }
                if (count >= limits_.maxEntries) {
                    return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                                     "Archive entry count exceeds the limit", path_));
                }
                ++count;
                auto metadata = entryMetadata(entry, limits_, path_);
                if (!metadata) {
                    return std::unexpected(std::move(metadata.error()));
                }
                metadata->index = count - 1;
                const auto entryBytes = static_cast<std::uint64_t>(sizeof(ArchiveEntry)) +
                                        static_cast<std::uint64_t>(metadata->path.size());
                if (metadataBytes > limits_.maxMetadataBytes ||
                    entryBytes > limits_.maxMetadataBytes - metadataBytes) {
                    return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                                     "Archive metadata exceeds the memory budget", path_));
                }
                metadataBytes += entryBytes;
                if (!callback(*metadata)) {
                    return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                                     "Archive enumeration cancelled", path_));
                }
                if (cancellation.stop_requested()) {
                    return std::unexpected(makeError(ArchiveErrorCode::Cancelled,
                                                     "Archive operation cancelled", path_));
                }
                const int skipped = archive_read_data_skip(session.handle.get());
                if (skipped != ARCHIVE_OK && skipped != ARCHIVE_EOF) {
                    return std::unexpected(backendError(session.handle.get(), skipped, path_,
                                                         cancellation, session.password));
                }
            }
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Not enough memory to enumerate archive", path_));
        } catch (const std::exception& error) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::BackendError,
                                                "Archive enumeration failed", error.what(), path_});
        } catch (...) {
            return std::unexpected(makeError(ArchiveErrorCode::Unknown,
                                             "Archive callback failed with an unknown exception", path_));
        }
    }

    Result<void> extract(const ExtractionRequest& request, const ProgressCallback& progress,
                         CancellationToken cancellation) override {
        if (path_.empty()) {
            return std::unexpected(makeError(ArchiveErrorCode::InvalidArchive,
                                             "Open an archive before extracting entries"));
        }
        try {
            auto unicode = detail::ArchiveUnicodeScope::enter();
            if (!unicode) return std::unexpected(std::move(unicode.error()));
            const auto password = request.password ? std::string_view{*request.password}
                                                    : std::string_view{password_};
            return extractArchive(path_, limits_, password, request, progress, cancellation);
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Not enough memory to prepare extraction", path_));
        } catch (const std::exception&) {
            return std::unexpected(makeError(ArchiveErrorCode::IoError,
                                             "Could not prepare archive extraction", path_));
        }
    }

private:
    ArchiveReadLimits limits_;
    std::filesystem::path path_;
    std::string password_;
};

} // namespace

std::unique_ptr<ArchiveReader> makeLibArchiveReader(ArchiveReadLimits limits) {
    return std::make_unique<LibArchiveReader>(limits);
}

} // namespace ananarchiver
