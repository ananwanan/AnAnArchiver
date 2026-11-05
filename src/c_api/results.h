#pragma once

#include <ananarchiver/application/task_manager.h>
#include <ananarchiver/archive_capabilities.h>
#include <ananarchiver/c_api/ananarchiver.h>

namespace ananarchiver::c_api {

aa_string view(const std::string& text) noexcept;
struct ErrorResult final : aa_error {
    std::string messageText;
    std::string backendText;
    std::string pathText;
    explicit ErrorResult(const ArchiveError& error);
};

struct SnapshotResult final : aa_task_snapshot {
    application::TaskSnapshot source;
    std::unique_ptr<ErrorResult> failure;
    std::string conflictPath;
    explicit SnapshotResult(application::TaskSnapshot snapshot);
};

struct EntryBatchResult final : aa_entry_batch {
    std::vector<ArchiveEntry> source;
    std::vector<aa_entry> rows;
    EntryBatchResult();
    void assign(aa_task_id task, std::vector<ArchiveEntry> metadata);
};

struct CapabilityBatchResult final : aa_capability_batch {
    std::vector<ArchiveCapabilities> source;
    std::vector<aa_compression_capability> rows;
    explicit CapabilityBatchResult(std::vector<ArchiveCapabilities> formats);
};

uint32_t formatValue(CompressionFormat format) noexcept;
uint32_t methodValue(CompressionMethod method) noexcept;

} // namespace ananarchiver::c_api
