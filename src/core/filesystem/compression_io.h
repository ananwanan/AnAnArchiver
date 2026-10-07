#pragma once

#include "ananarchiver/archive_error.h"
#include "ananarchiver/cancellation.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace ananarchiver::detail {

struct CompressionFileIdentity {
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    bool operator==(const CompressionFileIdentity&) const = default;
};

struct CompressionPathInfo {
    bool directory = false;
    std::uint64_t size = 0;
    std::uint32_t permissions = 0;
    std::chrono::system_clock::time_point modified;
    CompressionFileIdentity identity;
};

// Native no-follow inspection also rejects junctions, reparse points and
// special files. No descriptor is retained for a complete metadata index.
Result<CompressionPathInfo> inspectCompressionPath(const std::filesystem::path& path);
// Windows compares native components case-insensitively; POSIX preserves case.
bool compressionPathContains(const std::filesystem::path& path, const std::filesystem::path& root);
std::filesystem::path compressionNativePath(const std::filesystem::path& path);
Result<std::filesystem::path> canonicalCompressionInputPath(const std::filesystem::path& path);
Result<std::filesystem::path> canonicalCompressionParentPath(const std::filesystem::path& path);
// Decompose logical filesystem paths before adding any native device prefix.
std::filesystem::path compressionParentPath(const std::filesystem::path& path);
bool compressionFilesystemRoot(const std::filesystem::path& path);
std::filesystem::path compressionRelativePath(const std::filesystem::path& path,
                                              const std::filesystem::path& root);

// Flat native directory cursor. The backend owns its bounded DFS stack and
// checks cancellation/depth before entering children. Never follows links.
class CompressionDirectoryCursor {
public:
    struct Impl;
    explicit CompressionDirectoryCursor(std::unique_ptr<Impl> impl);
    ~CompressionDirectoryCursor();
    CompressionDirectoryCursor(const CompressionDirectoryCursor&) = delete;
    CompressionDirectoryCursor& operator=(const CompressionDirectoryCursor&) = delete;
    static Result<std::unique_ptr<CompressionDirectoryCursor>> open(const std::filesystem::path& directory);
    Result<std::optional<std::filesystem::path>> next();
private:
    std::unique_ptr<Impl> impl_;
};

class CompressionInputFile {
public:
    struct Impl;
    explicit CompressionInputFile(std::unique_ptr<Impl> impl);
    ~CompressionInputFile();
    CompressionInputFile(const CompressionInputFile&) = delete;
    CompressionInputFile& operator=(const CompressionInputFile&) = delete;
    static Result<std::unique_ptr<CompressionInputFile>> open(const std::filesystem::path& path,
                                                              const CompressionPathInfo& scanned);
    Result<std::size_t> read(std::span<std::byte> buffer);
    Result<void> verifyUnchanged();
private:
    std::unique_ptr<Impl> impl_;
};

// An exclusively created sibling staging directory contains the temporary
// archive. Parent descriptors/locks pin the selected location; no destination
// component is ever followed. commit performs one atomic namespace change.
class AtomicArchiveOutput {
public:
    struct Impl;
    explicit AtomicArchiveOutput(std::unique_ptr<Impl> impl);
    ~AtomicArchiveOutput();
    AtomicArchiveOutput(const AtomicArchiveOutput&) = delete;
    AtomicArchiveOutput& operator=(const AtomicArchiveOutput&) = delete;
    static Result<std::unique_ptr<AtomicArchiveOutput>> open(const std::filesystem::path& destination,
                                                             bool overwrite);
    Result<void> write(std::span<const std::byte> data);
    Result<void> finish();
    Result<void> commit(CancellationToken cancellation = {});
    Result<void> rollback();
    bool committed() const noexcept;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ananarchiver::detail
