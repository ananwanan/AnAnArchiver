#pragma once

#include <ananarchiver/archive_error.h>
#include <ananarchiver/cancellation.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ananarchiver::application {

struct DirectoryEntry {
    std::filesystem::path path;
    std::string name;
    bool directory{};
    std::uint64_t size{};
    std::optional<std::chrono::system_clock::time_point> modified;
};

class DirectoryService {
  public:
    // Enumerates immediate children only; called by the frontend worker when
    // expanding a directory. Never follows directory symlinks recursively.
    static Result<std::vector<DirectoryEntry>> scanChildren(const std::filesystem::path& path,
                                                            CancellationToken cancellation = {});
};

} // namespace ananarchiver::application
