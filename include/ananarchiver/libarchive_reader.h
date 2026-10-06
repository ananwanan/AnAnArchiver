#pragma once

#include "ananarchiver/archive_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ananarchiver {

struct ArchiveReadLimits {
    std::uint64_t maxEntries = 2'000'000;
    std::size_t maxPathBytes = 64 * 1024;
    std::uint64_t maxMetadataBytes = 512ULL * 1024 * 1024;
};

// Backend composition entry point; callers never receive libarchive handles.
std::unique_ptr<ArchiveReader> makeLibArchiveReader(ArchiveReadLimits limits = {});

} // namespace ananarchiver
