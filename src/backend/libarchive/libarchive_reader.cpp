#include "ananarchiver/libarchive_reader.h"
#include "ananarchiver/unicode.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
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
};

la_ssize_t readInput(archive* handle, void* clientData, const void** buffer) {
    auto& input = *static_cast<InputStream*>(clientData);
    if (input.cancellation.stop_requested()) {
        archive_set_error(handle, ECANCELED, "Archive operation cancelled");
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
                          CancellationToken cancellation) {
    if (cancellation.stop_requested() || archive_errno(handle) == ECANCELED) {
        return makeError(ArchiveErrorCode::Cancelled, "Archive operation cancelled", path);
    }
    ArchiveErrorCode code = status == ARCHIVE_WARN ? ArchiveErrorCode::BackendWarning
                                                  : ArchiveErrorCode::CorruptedArchive;
    const int backendErrno = archive_errno(handle);
    if (backendErrno == EIO) {
        code = ArchiveErrorCode::IoError;
    } else if (backendErrno == ENOMEM) {
        code = ArchiveErrorCode::ResourceLimitExceeded;
    } else if (backendErrno == EACCES || backendErrno == EPERM) {
        code = ArchiveErrorCode::PermissionDenied;
    }
    std::string detail = archive_error_string(handle) == nullptr
                             ? std::string{}
                             : std::string{archive_error_string(handle)};
    // libarchive has no typed password/codec errors; isolate its diagnostic
    // mapping here so the application never parses backend messages.
    if (detail.find("Passphrase") != std::string::npos ||
        detail.find("passphrase") != std::string::npos ||
        detail.find("password") != std::string::npos) {
        code = ArchiveErrorCode::PasswordRequired;
    } else if (detail.find("Unrecognized archive format") != std::string::npos) {
        code = ArchiveErrorCode::InvalidArchive;
    } else if (detail.find("Unsupported") != std::string::npos ||
               detail.find("unsupported") != std::string::npos) {
        code = ArchiveErrorCode::UnsupportedCompression;
    }
    return {code, status == ARCHIVE_WARN ? "Archive metadata warning"
                                        : "Could not read archive",
            std::move(detail), path};
}

struct ReadSession {
    // Destruction releases the backend before its callback data and stream.
    InputStream input;
    ArchiveHandle handle{archive_read_new()};
};

Result<void> finishSession(ReadSession& session, const std::filesystem::path& path,
                           CancellationToken cancellation) {
    const int status = archive_read_close(session.handle.get());
    if (status != ARCHIVE_OK) {
        return std::unexpected(backendError(session.handle.get(), status, path, cancellation));
    }
    return {};
}

Result<void> startSession(ReadSession& session, const std::filesystem::path& path,
                          CancellationToken cancellation) {
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
            return std::unexpected(backendError(session.handle.get(), result, path,
                                                 cancellation));
        }
    }
    const int seekConfigured = archive_read_set_seek_callback(session.handle.get(), seekInput);
    if (seekConfigured != ARCHIVE_OK) {
        return std::unexpected(backendError(session.handle.get(), seekConfigured, path,
                                             cancellation));
    }
    const int result = archive_read_open2(session.handle.get(), &session.input,
                                          nullptr, readInput, skipInput, nullptr);
    if (result != ARCHIVE_OK) {
        return std::unexpected(backendError(session.handle.get(), result, path,
                                             cancellation));
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

class LibArchiveReader final : public ArchiveReader {
public:
    explicit LibArchiveReader(ArchiveReadLimits limits) : limits_(limits) {}

    Result<ArchiveInfo> open(const std::filesystem::path& file,
                             CancellationToken cancellation) override {
        path_.clear();
        try {
            ReadSession session;
            auto started = startSession(session, file, cancellation);
            if (!started) {
                return std::unexpected(std::move(started.error()));
            }
            archive_entry* entry = nullptr;
            const int status = archive_read_next_header(session.handle.get(), &entry);
            if (status != ARCHIVE_OK && status != ARCHIVE_EOF) {
                return std::unexpected(backendError(session.handle.get(), status, file,
                                                     cancellation));
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
            ReadSession session;
            auto started = startSession(session, path_, cancellation);
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
                                                         cancellation));
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
                                                         cancellation));
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

private:
    ArchiveReadLimits limits_;
    std::filesystem::path path_;
};

} // namespace

std::unique_ptr<ArchiveReader> makeLibArchiveReader(ArchiveReadLimits limits) {
    return std::make_unique<LibArchiveReader>(limits);
}

} // namespace ananarchiver
