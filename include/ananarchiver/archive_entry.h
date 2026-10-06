#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace ananarchiver {

enum class EntryType { RegularFile, Directory, Symlink, Hardlink, Other };

// Value metadata only. Path is UTF-8 as stored in the archive, not an authorized
// filesystem destination; enumeration does not normalize or extract it.
struct ArchiveEntry {
    std::string path;
    std::uint64_t size = 0;
    bool sizeKnown = true;
    std::optional<std::uint64_t> compressedSize;
    EntryType type = EntryType::Other;
    std::optional<std::chrono::system_clock::time_point> modified;
};

} // namespace ananarchiver
