#pragma once

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

enum class CompressionFormat { Zip, SevenZip, Tar, TarGzip, TarBzip2, TarXz, TarZstd, TarLz4 };
enum class CompressionMethod { Store, Deflate, Lzma2, Gzip, Bzip2, Xz, Zstd, Lz4 };
enum class CompressionPhase { Scanning, Writing, Finalizing };

struct CompressionLimits {
    std::uint64_t maxEntries = 2'000'000;
    std::size_t maxPathBytes = 64 * 1024;
    std::size_t maxDepth = 256;
    std::uint64_t maxMetadataBytes = 512ULL * 1024 * 1024;
    std::uint64_t maxEntryBytes = 100ULL * 1024 * 1024 * 1024;
    std::uint64_t maxTotalBytes = 1024ULL * 1024 * 1024 * 1024;
    std::chrono::milliseconds maxElapsed = std::chrono::hours{24};
};

struct CompressionRequest {
    // Each selected path is stored beneath its basename. Repeated selections
    // and descendants already covered by a selected directory are deduplicated.
    // Independent inputs producing the same archive path are rejected.
    std::vector<std::filesystem::path> inputPaths;
    std::filesystem::path destination;
    CompressionFormat format = CompressionFormat::Zip;
    CompressionMethod method = CompressionMethod::Deflate;
    int level = 6;
    bool overwriteExisting = false;
    bool preservePermissions = true;
    bool preserveTimestamps = true;
    // M3 creation capabilities explicitly disable encryption. Supplied
    // passwords are rejected before scanning inputs or creating output.
    std::optional<std::string> password;
    CompressionLimits limits;
};

struct CompressionProgress {
    CompressionPhase phase = CompressionPhase::Scanning;
    std::string currentFile;
    std::uint64_t processedBytes = 0;
    std::uint64_t processedEntries = 0;
    // Unknown while scanning. Writing totals are measured metadata estimates;
    // any input size/change discovered while streaming fails the transaction.
    std::optional<std::uint64_t> totalBytes;
    std::optional<std::uint64_t> totalEntries;
};
using CompressionProgressCallback = std::function<bool(const CompressionProgress&)>;

} // namespace ananarchiver
