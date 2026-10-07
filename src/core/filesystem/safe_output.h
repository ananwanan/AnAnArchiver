#pragma once

#include "ananarchiver/archive_error.h"
#include "ananarchiver/unicode.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace ananarchiver::detail {

inline Result<void> validateOutputPath(const std::filesystem::path& relative) {
    if (relative.has_root_path()) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsafePath,
                                            "Extraction output must be root-relative", {}, relative});
    }
    for (const auto& component : relative) {
        if (component == "." || component == "..") {
            return std::unexpected(ArchiveError{ArchiveErrorCode::UnsafePath,
                                                "Extraction output contains traversal", {}, relative});
        }
    }
    if (!relative.empty()) {
        auto first = pathToUtf8(*relative.begin());
        for (auto& c : first) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (first.starts_with(".ananarchiver-")) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::UnsafePath,
                                                "Extraction output uses the private staging namespace", {}, relative});
        }
    }
    return {};
}

enum class OutputKind { Missing, RegularFile, Directory };
struct OutputStatus {
    OutputKind kind = OutputKind::Missing;
    std::optional<std::chrono::system_clock::time_point> modified;
};

// Platform IO owns native descriptors. Every component is opened without
// following links/reparse points. Payloads and backups live in an exclusively
// created private directory, and file replacement never writes through an
// existing file (including a preexisting hardlink).
class OutputFile {
public:
    struct Impl;
    explicit OutputFile(std::unique_ptr<Impl> impl);
    ~OutputFile();
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    Result<void> write(std::span<const std::byte> data, std::uint64_t offset);
    Result<void> finish(std::uint64_t logicalSize,
                        std::optional<std::chrono::system_clock::time_point> modified);
private:
    std::unique_ptr<Impl> impl_;
};

struct StagedFile {
    std::string name;
    std::unique_ptr<OutputFile> file;
};

class SafeOutput {
public:
    struct Impl;
    explicit SafeOutput(std::unique_ptr<Impl> impl);
    ~SafeOutput();
    SafeOutput(const SafeOutput&) = delete;
    SafeOutput& operator=(const SafeOutput&) = delete;
    static Result<std::unique_ptr<SafeOutput>> open(const std::filesystem::path& root);
    const std::filesystem::path& root() const noexcept;
    Result<OutputStatus> status(const std::filesystem::path& relative);
    Result<void> directory(const std::filesystem::path& relative);
    Result<StagedFile> stage();
    Result<void> install(std::string_view stagedName,
                         const std::filesystem::path& relative, bool replace);
    Result<void> link(const std::filesystem::path& relative,
                      const std::filesystem::path& target,
                      bool hardlink, bool targetDirectory, bool replace);
    // Must be called only after the archive has been fully verified and every
    // output committed. Until then destruction rolls back installed outputs.
    Result<void> finish();
    Result<void> rollback();
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ananarchiver::detail
