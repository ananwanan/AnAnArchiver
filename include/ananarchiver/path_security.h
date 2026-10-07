#pragma once

#include "ananarchiver/archive_error.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace ananarchiver {

// Validates portable archive paths, independent of the host OS. Backslashes
// are separators. Rejects traversal, root/drive/UNC paths, NTFS streams and
// Windows filename aliases so an archive remains safe on every target OS.
Result<std::filesystem::path> secureArchivePath(std::string_view pathname,
                                              std::size_t maxPathBytes = 64 * 1024,
                                              std::size_t maxDepth = 256);

// Symlink targets may contain '..' only when resolution from the link's parent
// remains inside the extraction root. Returns the normalized root-relative
// target, never an absolute filesystem path.
Result<std::filesystem::path> secureLinkTarget(const std::filesystem::path& link,
                                             std::string_view target,
                                             bool hardlink,
                                             std::size_t maxPathBytes = 64 * 1024,
                                             std::size_t maxDepth = 256);

} // namespace ananarchiver
