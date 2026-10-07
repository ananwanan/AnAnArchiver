#pragma once

#include "ananarchiver/compression.h"

#include <optional>
#include <string>
#include <vector>

namespace ananarchiver {

struct CompressionMethodCapabilities {
    CompressionMethod method = CompressionMethod::Store;
    std::string name;
    std::optional<int> minLevel;
    std::optional<int> maxLevel;
    std::optional<int> defaultLevel;
};

struct ArchiveCapabilities {
    CompressionFormat format = CompressionFormat::Zip;
    std::string name;
    std::string extension;
    std::vector<CompressionMethodCapabilities> methods;
    bool supportsPassword = false;
    bool supportsMultithreading = false;
    bool supportsSolid = false;
    // TAR and 7z retain POSIX permissions; ZIP stores mode metadata but
    // consumers differ. Timestamps are stored subject to format precision.
    bool supportsPermissions = true;
    bool supportsTimestamps = true;
};

} // namespace ananarchiver
