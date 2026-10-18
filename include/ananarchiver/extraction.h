#pragma once

#include "ananarchiver/archive_entry.h"
#include "ananarchiver/archive_error.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ananarchiver {

enum class OverwritePolicy { Ask, Overwrite, Skip, Rename, OverwriteIfNewer };
// Safe permits only links to regular files/directories inside the output root.
// Preserve also permits contained dangling symlinks. Neither permits escapes.
enum class LinkPolicy { Safe, Preserve, Skip };

struct ExtractionLimits {
    std::uint64_t maxEntries = 2'000'000;
    std::size_t maxPathBytes = 64 * 1024;
    std::size_t maxDepth = 256;
    std::uint64_t maxMetadataBytes = 512ULL * 1024 * 1024;
    std::uint64_t maxEntryBytes = 100ULL * 1024 * 1024 * 1024;
    std::uint64_t maxTotalBytes = 1024ULL * 1024 * 1024 * 1024;
    std::chrono::milliseconds maxElapsed = std::chrono::hours{24};
};

struct ExtractionConflict {
    ArchiveEntry entry;
    std::filesystem::path destination;
};
using ConflictCallback = std::function<OverwritePolicy(const ExtractionConflict&)>;

struct ExtractionRequest {
    std::filesystem::path destination;
    // Zero-based enumeration identities distinguish duplicate entry names.
    // nullopt with no selectedDirectoryPaths extracts all entries; an empty
    // vector with no directory paths extracts none. Selecting a directory
    // includes its descendants, including entries before its header.
    std::optional<std::vector<std::uint64_t>> selectedIndices;
    // Additional directory subtrees, including implicit parents without a
    // directory header. Paths are validated and normalized by the backend.
    // Their descendants are combined with selectedIndices, never treated as
    // an extract-all request when selectedIndices is nullopt.
    std::vector<std::string> selectedDirectoryPaths;
    std::optional<std::string> password;
    OverwritePolicy overwrite = OverwritePolicy::Ask;
    LinkPolicy links = LinkPolicy::Safe;
    ExtractionLimits limits;
    ConflictCallback onConflict;
    // Optional metadata estimates. Actual byte accounting never trusts them.
    std::optional<std::uint64_t> totalBytes;
    std::optional<std::uint64_t> totalEntries;
};

struct ExtractionProgress {
    std::string currentFile;
    std::uint64_t processedBytes = 0;
    std::uint64_t processedEntries = 0;
    std::optional<std::uint64_t> totalBytes;
    std::optional<std::uint64_t> totalEntries;
};
using ProgressCallback = std::function<bool(const ExtractionProgress&)>;

} // namespace ananarchiver
