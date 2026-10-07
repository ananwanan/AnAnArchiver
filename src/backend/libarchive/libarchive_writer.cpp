#include "ananarchiver/libarchive_writer.h"
#include "ananarchiver/path_security.h"
#include "ananarchiver/unicode.h"
#include "../../core/filesystem/compression_io.h"
#include "../../core/filesystem/safe_output.h"
#include "../../core/archive/archive_unicode_scope.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ananarchiver {
namespace {

ArchiveError error(ArchiveErrorCode code, std::string message,
                   const std::filesystem::path& path = {}) {
    return {code, std::move(message), {}, path};
}

ArchiveError filesystemError(std::string message, const std::filesystem::path& path,
                             const std::error_code& native) {
    auto code = ArchiveErrorCode::IoError;
    if (native == std::errc::permission_denied) code = ArchiveErrorCode::PermissionDenied;
    else if (native == std::errc::no_such_file_or_directory) code = ArchiveErrorCode::FileNotFound;
    else if (native == std::errc::no_space_on_device) code = ArchiveErrorCode::DiskFull;
    return {code, std::move(message), native.message(), path};
}

struct WriterDeleter {
    void operator()(archive* value) const noexcept {
        if (value != nullptr) {
            // Failure/cancellation must discard codec state, not implicitly
            // finalize an unfinished archive or replay a large 7z spool.
            archive_write_fail(value);
            archive_write_free(value);
        }
    }
};
struct EntryDeleter {
    void operator()(archive_entry* value) const noexcept { if (value != nullptr) archive_entry_free(value); }
};
using WriterHandle = std::unique_ptr<archive, WriterDeleter>;
using EntryHandle = std::unique_ptr<archive_entry, EntryDeleter>;

// Returning WARN from add_filter means libarchive selected an external
// executable. Require OK throughout: codecs must come from the pinned build.
int configureWriter(archive* handle, CompressionFormat format, CompressionMethod method, int level) {
    int status = ARCHIVE_FAILED;
    const char* formatModule = nullptr;
    const char* compression = nullptr;
    const char* filterModule = nullptr;
    switch (format) {
    case CompressionFormat::Zip:
        status = archive_write_set_format_zip(handle);
        formatModule = "zip";
        compression = method == CompressionMethod::Store ? "store" : "deflate";
        break;
    case CompressionFormat::SevenZip:
        status = archive_write_set_format_7zip(handle);
        formatModule = "7zip";
        compression = method == CompressionMethod::Store ? "copy" : "lzma2";
        break;
    case CompressionFormat::Tar:
    case CompressionFormat::TarGzip:
    case CompressionFormat::TarBzip2:
    case CompressionFormat::TarXz:
    case CompressionFormat::TarZstd:
    case CompressionFormat::TarLz4:
        status = archive_write_set_format_pax_restricted(handle);
        break;
    }
    if (status != ARCHIVE_OK) return status;
    if (formatModule != nullptr) {
        status = archive_write_set_format_option(handle, formatModule, "compression", compression);
        if (status != ARCHIVE_OK) return status;
        if (method != CompressionMethod::Store) {
            const auto value = std::to_string(level);
            status = archive_write_set_format_option(handle, formatModule, "compression-level", value.c_str());
            if (status != ARCHIVE_OK) return status;
        }
        if (format == CompressionFormat::Zip) {
            status = archive_write_set_format_option(handle, "zip", "hdrcharset", "UTF-8");
            if (status != ARCHIVE_OK) return status;
        }
    }
    switch (format) {
    case CompressionFormat::TarGzip:
        status = archive_write_add_filter_gzip(handle); filterModule = "gzip"; break;
    case CompressionFormat::TarBzip2:
        status = archive_write_add_filter_bzip2(handle); filterModule = "bzip2"; break;
    case CompressionFormat::TarXz:
        status = archive_write_add_filter_xz(handle); filterModule = "xz"; break;
    case CompressionFormat::TarZstd:
        status = archive_write_add_filter_zstd(handle); filterModule = "zstd"; break;
    case CompressionFormat::TarLz4:
        status = archive_write_add_filter_lz4(handle); filterModule = "lz4"; break;
    default:
        status = archive_write_add_filter_none(handle); break;
    }
    if (status != ARCHIVE_OK) return status;
    if (filterModule != nullptr) {
        const auto value = std::to_string(level);
        status = archive_write_set_filter_option(handle, filterModule, "compression-level", value.c_str());
        if (status != ARCHIVE_OK) return status;
    }
    status = archive_write_set_bytes_per_block(handle, 0);
    if (status != ARCHIVE_OK) return status;
    return archive_write_set_bytes_in_last_block(handle, 1);
}

bool probe(CompressionFormat format, CompressionMethod method, int level) {
    WriterHandle handle{archive_write_new()};
    return handle && configureWriter(handle.get(), format, method, level) == ARCHIVE_OK;
}

std::vector<ArchiveCapabilities> creationCapabilities() {
    struct Candidate {
        CompressionFormat format;
        const char* name;
        const char* extension;
        CompressionMethod method;
        const char* methodName;
        int minimum;
        int maximum;
        int initial;
    };
    // This adapter deliberately offers the conventional bounded positive
    // zstd level range. Backend option probes remove unsupported levels/codecs.
    // ZIP level zero changes its method to Store, so Deflate starts at one.
    constexpr std::array candidates{
        Candidate{CompressionFormat::Zip, "ZIP", "zip", CompressionMethod::Deflate, "Deflate", 1, 9, 6},
        Candidate{CompressionFormat::Zip, "ZIP", "zip", CompressionMethod::Store, "Store", 0, 0, 0},
        Candidate{CompressionFormat::SevenZip, "7z", "7z", CompressionMethod::Lzma2, "LZMA2", 0, 9, 6},
        Candidate{CompressionFormat::SevenZip, "7z", "7z", CompressionMethod::Store, "Store", 0, 0, 0},
        Candidate{CompressionFormat::Tar, "TAR", "tar", CompressionMethod::Store, "Store", 0, 0, 0},
        Candidate{CompressionFormat::TarGzip, "TAR.GZ", "tar.gz", CompressionMethod::Gzip, "Gzip", 0, 9, 6},
        Candidate{CompressionFormat::TarBzip2, "TAR.BZ2", "tar.bz2", CompressionMethod::Bzip2, "Bzip2", 1, 9, 6},
        Candidate{CompressionFormat::TarXz, "TAR.XZ", "tar.xz", CompressionMethod::Xz, "XZ", 0, 9, 6},
        Candidate{CompressionFormat::TarZstd, "TAR.ZST", "tar.zst", CompressionMethod::Zstd, "Zstandard", 1, 22, 3},
        Candidate{CompressionFormat::TarLz4, "TAR.LZ4", "tar.lz4", CompressionMethod::Lz4, "LZ4", 1, 9, 1}
    };
    std::vector<ArchiveCapabilities> result;
    for (const auto& candidate : candidates) {
        if (!probe(candidate.format, candidate.method, candidate.initial)) continue;
        int minimum = candidate.initial;
        int maximum = candidate.initial;
        for (int level = candidate.initial - 1; level >= candidate.minimum; --level) {
            if (!probe(candidate.format, candidate.method, level)) break;
            minimum = level;
        }
        for (int level = candidate.initial + 1; level <= candidate.maximum; ++level) {
            if (!probe(candidate.format, candidate.method, level)) break;
            maximum = level;
        }
        auto found = std::find_if(result.begin(), result.end(), [&](const auto& value) {
            return value.format == candidate.format;
        });
        if (found == result.end()) {
            ArchiveCapabilities value;
            value.format = candidate.format;
            value.name = candidate.name;
            value.extension = candidate.extension;
            result.push_back(std::move(value));
            found = std::prev(result.end());
        }
        CompressionMethodCapabilities method;
        method.method = candidate.method;
        method.name = candidate.methodName;
        if (candidate.method != CompressionMethod::Store) {
            method.minLevel = minimum;
            method.maxLevel = maximum;
            method.defaultLevel = candidate.initial;
        }
        found->methods.push_back(std::move(method));
    }
    return result;
}

Result<void> checkRequest(const CompressionRequest& request,
                          const std::vector<ArchiveCapabilities>& capabilities) {
    if (request.password.has_value()) {
        return std::unexpected(error(ArchiveErrorCode::UnsupportedOperation,
                                     "Archive creation encryption is not supported", request.destination));
    }
    const auto format = std::find_if(capabilities.begin(), capabilities.end(), [&](const auto& value) {
        return value.format == request.format;
    });
    if (format == capabilities.end()) {
        return std::unexpected(error(ArchiveErrorCode::UnsupportedFormat,
                                     "Archive creation format is not supported", request.destination));
    }
    const auto method = std::find_if(format->methods.begin(), format->methods.end(), [&](const auto& value) {
        return value.method == request.method;
    });
    if (method == format->methods.end() ||
        (method->minLevel && request.level < *method->minLevel) ||
        (method->maxLevel && request.level > *method->maxLevel) ||
        (!method->minLevel && request.level != 0)) {
        return std::unexpected(error(ArchiveErrorCode::UnsupportedCompression,
                                     "Compression method or level is not supported", request.destination));
    }
    if (request.inputPaths.empty()) {
        return std::unexpected(error(ArchiveErrorCode::InvalidArchive,
                                     "At least one compression input is required", request.destination));
    }
    if (request.destination.empty()) {
        return std::unexpected(error(ArchiveErrorCode::UnsafePath,
                                     "Archive destination is empty", request.destination));
    }
    return {};
}

struct Operation {
    const CompressionRequest& request;
    const CompressionProgressCallback& callback;
    CancellationToken cancellation;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    Result<void> checkpoint() const {
        if (cancellation.stop_requested()) {
            return std::unexpected(error(ArchiveErrorCode::Cancelled,
                                         "Archive creation cancelled", request.destination));
        }
        if (std::chrono::steady_clock::now() - started >= request.limits.maxElapsed) {
            return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Archive creation exceeded the time limit", request.destination));
        }
        return {};
    }

    Result<void> report(const CompressionProgress& progress) const {
        auto current = checkpoint();
        if (!current) return current;
        if (callback) {
            bool accepted = false;
            try { accepted = callback(progress); }
            catch (...) {
                // Callback exceptions can contain private content/passwords.
                // Never copy arbitrary exception text into a task error.
                return std::unexpected(error(ArchiveErrorCode::BackendError,
                                             "Compression progress callback failed", request.destination));
            }
            if (!accepted) {
                return std::unexpected(error(ArchiveErrorCode::Cancelled,
                                             "Archive creation cancelled", request.destination));
            }
        }
        return checkpoint();
    }
};

struct InputEntry {
    std::filesystem::path source;
    std::string name;
    detail::CompressionPathInfo info;
};
struct InputIndex {
    std::vector<InputEntry> entries;
    std::uint64_t bytes = 0;
};

Result<std::uint64_t> metadataCost(const InputEntry& entry, bool selectionRoot = false) {
    std::uint64_t total = sizeof(InputEntry) + (selectionRoot ? sizeof(std::filesystem::path) + 128ULL : 64ULL);
    auto add = [&](std::size_t count, std::uint64_t factor) {
        if (count > ((std::numeric_limits<std::uint64_t>::max)() - total) / factor) return false;
        total += static_cast<std::uint64_t>(count) * factor;
        return true;
    };
    const auto nativeWidth = sizeof(std::filesystem::path::value_type) * (selectionRoot ? 2ULL : 1ULL);
    if (!add(entry.name.size(), 2) || !add(entry.source.native().size(), nativeWidth)) {
        return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                     "Compression metadata accounting exceeds limits", entry.source));
    }
    return total;
}

struct IdentityHash {
    std::size_t operator()(const detail::CompressionFileIdentity& value) const noexcept {
        return std::hash<std::uint64_t>{}(value.device) ^
               (std::hash<std::uint64_t>{}(value.file) + 0x9e3779b9U);
    }
};

std::string portableKey(std::string name) {
    for (auto& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

Result<std::filesystem::path> selectedInputPath(const std::filesystem::path& requested) {
    if (requested.empty()) return std::unexpected(error(ArchiveErrorCode::UnsafePath, "Compression input is empty"));
    if (pathToUtf8(requested).find('\0') != std::string::npos) {
        return std::unexpected(error(ArchiveErrorCode::InvalidPathEncoding,
                                     "Compression input contains an embedded null", requested));
    }
    std::error_code native;
    auto absolute = std::filesystem::absolute(requested, native).lexically_normal();
    if (native) return std::unexpected(filesystemError("Could not resolve compression input", requested, native));
    absolute = detail::compressionNativePath(absolute);
    while (!detail::compressionFilesystemRoot(absolute) && absolute.filename().empty()) {
        absolute = detail::compressionParentPath(absolute);
    }
    if (detail::compressionFilesystemRoot(absolute) || absolute.filename().empty()) {
        return std::unexpected(error(ArchiveErrorCode::UnsafePath, "Compression input must have a basename", requested));
    }
    // Resolve user-selected ancestor aliases once. All subsequent component
    // walks use native no-follow descriptors/locks against that location.
    auto parent = detail::canonicalCompressionParentPath(detail::compressionParentPath(absolute));
    if (!parent) return std::unexpected(std::move(parent.error()));
    return detail::compressionNativePath(*parent / absolute.filename());
}

Result<std::filesystem::path> destinationPath(const std::filesystem::path& requested) {
    if (pathToUtf8(requested).find('\0') != std::string::npos) {
        return std::unexpected(error(ArchiveErrorCode::InvalidPathEncoding,
                                     "Archive destination contains an embedded null", requested));
    }
    std::error_code native;
    auto absolute = std::filesystem::absolute(requested, native).lexically_normal();
    if (native) return std::unexpected(filesystemError("Could not resolve archive destination", requested, native));
    absolute = detail::compressionNativePath(absolute);
    if (absolute.filename().empty() || absolute.filename() == "." || absolute.filename() == "..") {
        return std::unexpected(error(ArchiveErrorCode::UnsafePath, "Archive destination must be a file", requested));
    }
    auto parent = detail::canonicalCompressionParentPath(detail::compressionParentPath(absolute));
    if (!parent) return std::unexpected(std::move(parent.error()));
    return detail::compressionNativePath(*parent / absolute.filename());
}

Result<InputIndex> scanInputs(Operation& operation, const std::filesystem::path& destination) {
    CompressionProgress progress;
    auto report = operation.report(progress);
    if (!report) return std::unexpected(std::move(report.error()));
    std::optional<detail::CompressionFileIdentity> destinationIdentity;
    auto destinationInfo = detail::inspectCompressionPath(destination);
    if (destinationInfo) {
        const auto& info = destinationInfo;
        if (info->directory) return std::unexpected(error(ArchiveErrorCode::UnsafePath, "Archive destination is a directory", destination));
        destinationIdentity = info->identity;
    } else if (destinationInfo.error().code != ArchiveErrorCode::FileNotFound) {
        if (destinationInfo.error().code == ArchiveErrorCode::UnsupportedOperation) {
            return std::unexpected(error(ArchiveErrorCode::UnsafePath, "Archive destination is not a regular file", destination));
        }
        return std::unexpected(std::move(destinationInfo.error()));
    }
    std::vector<InputEntry> roots;
    std::unordered_set<std::filesystem::path> selectedPaths;
    std::uint64_t metadataBytes = 0;
    for (const auto& requested : operation.request.inputPaths) {
        auto current = operation.checkpoint();
        if (!current) return std::unexpected(std::move(current.error()));
        auto source = selectedInputPath(requested);
        if (!source) return std::unexpected(std::move(source.error()));
        auto canonical = detail::canonicalCompressionInputPath(*source);
        if (!canonical) return std::unexpected(std::move(canonical.error()));
        source = std::move(*canonical);
        auto info = detail::inspectCompressionPath(*source);
        if (!info) return std::unexpected(std::move(info.error()));
        if (detail::compressionPathContains(destination, *source) ||
            (destinationIdentity && *destinationIdentity == info->identity)) {
            return std::unexpected(error(ArchiveErrorCode::UnsafePath,
                                         "Archive destination overlaps a compression input", requested));
        }
        if (selectedPaths.contains(*source)) continue;
        InputEntry root{*source, pathToUtf8(source->filename()), *info};
        const auto& limits = operation.request.limits;
        auto safe = secureArchivePath(root.name, limits.maxPathBytes, limits.maxDepth);
        if (!safe) {
            safe.error().path = root.source;
            return std::unexpected(std::move(safe.error()));
        }
        auto cost = metadataCost(root, true);
        if (!cost) return std::unexpected(std::move(cost.error()));
        if (roots.size() >= limits.maxEntries || metadataBytes > limits.maxMetadataBytes ||
            *cost > limits.maxMetadataBytes - metadataBytes) {
            return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Compression selections exceed configured metadata limits", requested));
        }
        metadataBytes += *cost;
        selectedPaths.insert(*source);
        roots.push_back(std::move(root));
        if ((roots.size() & 127U) == 0) {
            progress.currentFile = roots.back().name;
            auto updated = operation.report(progress);
            if (!updated) return std::unexpected(std::move(updated.error()));
        }
    }
    std::unordered_set<detail::CompressionFileIdentity, IdentityHash> selectedDirectories;
    for (const auto& root : roots) if (root.info.directory) selectedDirectories.insert(root.info.identity);
    std::unordered_map<std::filesystem::path, bool> coveredAncestors;
    auto coveredSelection = [&](const InputEntry& root) -> Result<bool> {
        if (selectedDirectories.empty()) return false;
        std::vector<std::filesystem::path> visited;
        auto ancestor = detail::compressionParentPath(root.source);
        bool covered = false;
        while (!detail::compressionFilesystemRoot(ancestor)) {
            auto ready = operation.checkpoint();
            if (!ready) return std::unexpected(std::move(ready.error()));
            const auto known = coveredAncestors.find(ancestor);
            if (known != coveredAncestors.end()) {
                covered = known->second;
                break;
            }
            auto info = detail::inspectCompressionPath(ancestor);
            if (!info) return std::unexpected(std::move(info.error()));
            if (!info->directory) {
                return std::unexpected(error(ArchiveErrorCode::UnsafePath,
                                             "Compression input ancestor is not a directory", ancestor));
            }
            // Physical identities handle Windows aliases while also respecting
            // case-sensitive Windows directories used by WSL. Lexical prefix
            // comparison alone could silently drop a distinct Foo/child input.
            const bool selectedParent = selectedDirectories.contains(info->identity);
            const auto units = ancestor.native().size();
            constexpr auto fixed = sizeof(std::filesystem::path) + 128ULL;
            constexpr auto width = sizeof(std::filesystem::path::value_type);
            if (units > ((std::numeric_limits<std::uint64_t>::max)() - fixed) / width) {
                return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Compression ancestor metadata exceeds limits", ancestor));
            }
            const auto cost = fixed + static_cast<std::uint64_t>(units) * width;
            const auto maximum = operation.request.limits.maxMetadataBytes;
            if (metadataBytes > maximum || cost > maximum - metadataBytes) {
                return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Compression ancestor metadata exceeds the memory limit", ancestor));
            }
            metadataBytes += cost;
            visited.push_back(ancestor);
            if (selectedParent) {
                covered = true;
                break;
            }
            ancestor = detail::compressionParentPath(ancestor);
        }
        for (auto& path : visited) coveredAncestors.emplace(std::move(path), covered);
        return covered;
    };
    InputIndex index;
    std::unordered_set<std::string> names;
    auto append = [&](InputEntry entry) -> Result<void> {
        const auto& limits = operation.request.limits;
        auto safe = secureArchivePath(entry.name, limits.maxPathBytes, limits.maxDepth);
        if (!safe) {
            safe.error().path = entry.source;
            return std::unexpected(std::move(safe.error()));
        }
        auto allowed = detail::validateOutputPath(*safe);
        if (!allowed) {
            allowed.error().path = entry.source;
            return allowed;
        }
        // A literal backslash in a POSIX filename must not become an archive
        // directory separator when later extracted on another OS.
        if (entry.name.find('\\') != std::string::npos) {
            return std::unexpected(error(ArchiveErrorCode::UnsafePath,
                                         "Compression input name contains a nonportable separator", entry.source));
        }
        if (index.entries.size() >= limits.maxEntries || entry.info.size > limits.maxEntryBytes ||
            entry.info.size > static_cast<std::uint64_t>((std::numeric_limits<la_int64_t>::max)()) ||
            index.bytes > limits.maxTotalBytes || entry.info.size > limits.maxTotalBytes - index.bytes) {
            return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Compression input exceeds configured limits", entry.source));
        }
        auto cost = metadataCost(entry);
        if (!cost) return std::unexpected(std::move(cost.error()));
        if (metadataBytes > limits.maxMetadataBytes || *cost > limits.maxMetadataBytes - metadataBytes) {
            return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                         "Compression metadata exceeds the memory limit", entry.source));
        }
        if (!names.insert(portableKey(entry.name)).second) {
            return std::unexpected(error(ArchiveErrorCode::OutputExists,
                                         "Compression inputs produce duplicate archive paths", entry.source));
        }
        if (destinationIdentity && entry.info.identity == *destinationIdentity) {
            return std::unexpected(error(ArchiveErrorCode::UnsafePath,
                                         "Archive destination aliases a compression input", entry.source));
        }
        metadataBytes += *cost;
        index.bytes += entry.info.size;
        progress.currentFile = entry.name;
        progress.processedBytes = 0;
        progress.processedEntries = static_cast<std::uint64_t>(index.entries.size()) + 1;
        index.entries.push_back(std::move(entry));
        return operation.report(progress);
    };
    for (const auto& root : roots) {
        auto covered = coveredSelection(root);
        if (!covered) return std::unexpected(std::move(covered.error()));
        if (*covered) continue;
        auto added = append(root);
        if (!added) return std::unexpected(std::move(added.error()));
        if (!root.info.directory) continue;
        auto opened = detail::CompressionDirectoryCursor::open(root.source);
        if (!opened) return std::unexpected(std::move(opened.error()));
        std::vector<std::unique_ptr<detail::CompressionDirectoryCursor>> directories;
        directories.push_back(std::move(*opened));
        while (!directories.empty()) {
            auto checked = operation.checkpoint();
            if (!checked) return std::unexpected(std::move(checked.error()));
            auto next = directories.back()->next();
            if (!next) return std::unexpected(std::move(next.error()));
            if (!*next) {
                directories.pop_back();
                continue;
            }
            const auto& source = **next;
            auto info = detail::inspectCompressionPath(source);
            if (!info) return std::unexpected(std::move(info.error()));
            const auto relative = detail::compressionRelativePath(source, root.source);
            if (relative.empty() || relative.has_root_path() ||
                std::any_of(relative.begin(), relative.end(), [](const auto& component) {
                    return component == ".." || component == ".";
                })) {
                return std::unexpected(error(ArchiveErrorCode::UnsafePath,
                                             "Compression directory entry is outside its selected root", source));
            }
            std::string name = root.name;
            for (const auto& component : relative) {
                name += '/';
                name += pathToUtf8(component);
            }
            auto child = append(InputEntry{source, std::move(name), *info});
            if (!child) return std::unexpected(std::move(child.error()));
            if (info->directory) {
                auto ready = operation.checkpoint();
                if (!ready) return std::unexpected(std::move(ready.error()));
                auto cursor = detail::CompressionDirectoryCursor::open(source);
                if (!cursor) return std::unexpected(std::move(cursor.error()));
                directories.push_back(std::move(*cursor));
            }
        }
    }
    return index;
}

struct OutputSink {
    detail::AtomicArchiveOutput& output;
    Operation& operation;
    std::optional<ArchiveError> failure;
};

la_ssize_t writeArchive(archive* handle, void* client, const void* data, std::size_t size) {
    auto& sink = *static_cast<OutputSink*>(client);
    try {
        auto checked = sink.operation.checkpoint();
        if (!checked) {
            sink.failure = std::move(checked.error());
            archive_set_error(handle, sink.failure->code == ArchiveErrorCode::Cancelled ? ECANCELED : ETIMEDOUT,
                               "Archive creation interrupted");
            return -1;
        }
        auto written = sink.output.write(std::span{static_cast<const std::byte*>(data), size});
        if (!written) {
            sink.failure = std::move(written.error());
            archive_set_error(handle, EIO, "Could not write archive output");
            return -1;
        }
        return static_cast<la_ssize_t>(size);
    } catch (const std::bad_alloc&) {
        archive_set_error(handle, ENOMEM, "Archive output allocation failed");
        return -1;
    } catch (...) {
        archive_set_error(handle, EIO, "Archive output callback failed");
        return -1;
    }
}

ArchiveError writerError(archive* handle, int status, OutputSink& sink,
                         const std::filesystem::path& path) {
    if (sink.failure) return *sink.failure;
    auto checked = sink.operation.checkpoint();
    if (!checked) return std::move(checked.error());
    auto code = status == ARCHIVE_WARN ? ArchiveErrorCode::BackendWarning : ArchiveErrorCode::BackendError;
    if (archive_errno(handle) == ENOMEM) code = ArchiveErrorCode::ResourceLimitExceeded;
    else if (archive_errno(handle) == ENOSPC) code = ArchiveErrorCode::DiskFull;
    else if (archive_errno(handle) == EACCES || archive_errno(handle) == EPERM) code = ArchiveErrorCode::PermissionDenied;
    const auto* message = archive_error_string(handle);
    return {code, "Could not create archive", message == nullptr ? std::string{} : std::string{message}, path};
}

bool sameMetadata(const detail::CompressionPathInfo& first, const detail::CompressionPathInfo& second) {
    return first.directory == second.directory && first.size == second.size &&
           first.identity == second.identity && first.modified == second.modified && first.permissions == second.permissions;
}

Result<void> writeInputs(Operation& operation, const InputIndex& index,
                         detail::AtomicArchiveOutput& output) {
    OutputSink sink{output, operation, {}};
    WriterHandle handle{archive_write_new()};
    if (!handle) return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded, "Could not allocate archive writer"));
    int status = configureWriter(handle.get(), operation.request.format, operation.request.method, operation.request.level);
    if (status != ARCHIVE_OK) return std::unexpected(writerError(handle.get(), status, sink, operation.request.destination));
    status = archive_write_open(handle.get(), &sink, nullptr, writeArchive, nullptr);
    if (status != ARCHIVE_OK) return std::unexpected(writerError(handle.get(), status, sink, operation.request.destination));
    CompressionProgress progress;
    progress.phase = CompressionPhase::Writing;
    progress.totalBytes = index.bytes;
    progress.totalEntries = index.entries.size();
    auto reported = operation.report(progress);
    if (!reported) return reported;
    std::array<std::byte, 128 * 1024> buffer{};
    EntryHandle entry{archive_entry_new()};
    if (!entry) return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded, "Could not allocate archive entry"));
    for (const auto& input : index.entries) {
        progress.currentFile = input.name;
        auto checked = operation.report(progress);
        if (!checked) return checked;
        std::unique_ptr<detail::CompressionInputFile> source;
        if (input.info.directory) {
            auto current = detail::inspectCompressionPath(input.source);
            if (!current) return std::unexpected(std::move(current.error()));
            if (!sameMetadata(input.info, *current)) {
                return std::unexpected(error(ArchiveErrorCode::IoError, "Compression directory changed after scanning", input.source));
            }
        } else {
            auto opened = detail::CompressionInputFile::open(input.source, input.info);
            if (!opened) return std::unexpected(std::move(opened.error()));
            source = std::move(*opened);
        }
        archive_entry_clear(entry.get());
        const auto name = input.info.directory ? input.name + '/' : input.name;
#ifdef _WIN32
        // Windows libarchive conversions consume WCS directly; an UTF-8-only
        // entry otherwise falls back through the process ANSI code page.
        const auto wideName = pathFromUtf8(name).native();
        archive_entry_copy_pathname_w(entry.get(), wideName.c_str());
#else
        archive_entry_set_pathname_utf8(entry.get(), name.c_str());
#endif
        archive_entry_set_filetype(entry.get(), input.info.directory ? AE_IFDIR : AE_IFREG);
        archive_entry_set_perm(entry.get(), operation.request.preservePermissions
            ? static_cast<int>(input.info.permissions & 07777U) : (input.info.directory ? 0755 : 0644));
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(input.info.size));
        if (operation.request.preserveTimestamps) {
            const auto seconds = std::chrono::floor<std::chrono::seconds>(input.info.modified.time_since_epoch());
            const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(input.info.modified.time_since_epoch() - seconds);
            archive_entry_set_mtime(entry.get(), static_cast<time_t>(seconds.count()), static_cast<long>(nanos.count()));
        } else {
            archive_entry_set_mtime(entry.get(), 0, 0);
        }
        status = archive_write_header(handle.get(), entry.get());
        if (status != ARCHIVE_OK) return std::unexpected(writerError(handle.get(), status, sink, input.source));
        std::uint64_t readBytes = 0;
        while (source) {
            auto ready = operation.checkpoint();
            if (!ready) return ready;
            auto count = source->read(buffer);
            if (!count) return std::unexpected(std::move(count.error()));
            if (*count == 0) break;
            if (readBytes > input.info.size || *count > input.info.size - readBytes) {
                return std::unexpected(error(ArchiveErrorCode::IoError, "Compression input grew while being archived", input.source));
            }
            std::span<const std::byte> pending{buffer.data(), *count};
            while (!pending.empty()) {
                const auto written = archive_write_data(handle.get(), pending.data(), pending.size());
                if (written <= 0 || static_cast<std::size_t>(written) > pending.size()) {
                    return std::unexpected(writerError(handle.get(), ARCHIVE_FATAL, sink, input.source));
                }
                pending = pending.subspan(static_cast<std::size_t>(written));
            }
            readBytes += *count;
            progress.processedBytes += *count;
            auto updated = operation.report(progress);
            if (!updated) return updated;
        }
        if (source) {
            if (readBytes != input.info.size) {
                return std::unexpected(error(ArchiveErrorCode::IoError, "Compression input was truncated while being archived", input.source));
            }
            auto stable = source->verifyUnchanged();
            if (!stable) return stable;
        }
        status = archive_write_finish_entry(handle.get());
        if (status != ARCHIVE_OK) return std::unexpected(writerError(handle.get(), status, sink, input.source));
        ++progress.processedEntries;
        auto updated = operation.report(progress);
        if (!updated) return updated;
    }
    progress.phase = CompressionPhase::Finalizing;
    progress.currentFile.clear();
    auto finishing = operation.report(progress);
    if (!finishing) return finishing;
    status = archive_write_close(handle.get());
    if (status != ARCHIVE_OK) return std::unexpected(writerError(handle.get(), status, sink, operation.request.destination));
    // Release backend resources (including 7z's internal spool) before staging
    // is closed and committed. A close/free warning aborts the transaction.
    const int released = archive_write_free(handle.release());
    if (released != ARCHIVE_OK) {
        return std::unexpected(error(ArchiveErrorCode::BackendError, "Could not release completed archive writer", operation.request.destination));
    }
    auto finished = output.finish();
    if (!finished) return finished;
    auto ready = operation.checkpoint();
    if (!ready) return ready;
    // Once the atomic commit succeeds, no cancellation callback/checkpoint can
    // reclassify that completed output as a cancelled operation.
    return output.commit(operation.cancellation);
}

class LibArchiveWriter final : public ArchiveWriter {
public:
    std::vector<ArchiveCapabilities> capabilities() const override {
        auto unicode = detail::ArchiveUnicodeScope::enter();
        if (!unicode) return {};
        return creationCapabilities();
    }

    Result<void> create(const CompressionRequest& request, const CompressionProgressCallback& progress,
                        CancellationToken cancellation) override {
        // Keep the transaction outside the exception boundary so failures in
        // allocation/path conversion still receive explicit checked cleanup.
        std::unique_ptr<detail::AtomicArchiveOutput> output;
        auto perform = [&]() -> Result<void> {
            try {
                Operation operation{request, progress, cancellation};
                auto ready = operation.checkpoint();
                if (!ready) return ready;
                auto unicode = detail::ArchiveUnicodeScope::enter();
                if (!unicode) return std::unexpected(std::move(unicode.error()));
                auto valid = checkRequest(request, creationCapabilities());
                if (!valid) return valid;
                auto destination = destinationPath(request.destination);
                if (!destination) return std::unexpected(std::move(destination.error()));
                auto index = scanInputs(operation, *destination);
                if (!index) return std::unexpected(std::move(index.error()));
                auto opened = detail::AtomicArchiveOutput::open(*destination, request.overwriteExisting);
                if (!opened) return std::unexpected(std::move(opened.error()));
                output = std::move(*opened);
                return writeInputs(operation, *index, *output);
            } catch (const std::bad_alloc&) {
                return std::unexpected(error(ArchiveErrorCode::ResourceLimitExceeded,
                                             "Archive creation allocation limit reached", request.destination));
            } catch (const std::filesystem::filesystem_error& failure) {
                return std::unexpected(filesystemError("Archive creation filesystem operation failed", request.destination, failure.code()));
            } catch (const std::invalid_argument&) {
                return std::unexpected(error(ArchiveErrorCode::InvalidPathEncoding,
                                             "Compression path is not valid Unicode", request.destination));
            } catch (...) {
                return std::unexpected(error(ArchiveErrorCode::BackendError,
                                             "Archive creation failed unexpectedly", request.destination));
            }
        };
        auto created = perform();
        if (!created && output) {
            // A postcommit exception/cleanup failure cannot be described as
            // an uncommitted failed write or cancellation; retain the archive
            // and make its installed state explicit in the diagnostic.
            const bool installed = output->committed();
            try {
                auto cleaned = output->rollback();
                if (!cleaned) {
                    cleaned.error().code = ArchiveErrorCode::CleanupFailed;
                    cleaned.error().message = installed
                        ? "Archive was committed, but temporary cleanup failed"
                        : "Archive creation failed and temporary cleanup could not complete";
                    return cleaned;
                }
            } catch (...) {
                return std::unexpected(error(ArchiveErrorCode::CleanupFailed,
                    installed ? "Archive was committed, but temporary cleanup failed"
                              : "Archive creation failed and temporary cleanup could not complete",
                    request.destination));
            }
            if (installed && created.error().code != ArchiveErrorCode::CleanupFailed) {
                created.error().code = ArchiveErrorCode::CleanupFailed;
                created.error().message = "Archive was committed, but completion diagnostics failed";
            }
        }
        return created;
    }
};

} // namespace

std::unique_ptr<ArchiveWriter> makeLibArchiveWriter() { return std::make_unique<LibArchiveWriter>(); }

} // namespace ananarchiver
