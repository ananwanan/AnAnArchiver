#pragma once

#include <ananarchiver/application/archive_service.h>
#include <ananarchiver/application/metadata_index.h>
#include <mutex>

namespace ananarchiver::application {

struct BrowserSubmission {
    TaskId task{};
    std::uint64_t archive{};
    std::uint64_t query{};
    std::uint64_t selection{};
};
struct BrowserTaskResult {
    std::uint64_t archive{};
    std::uint64_t query{};
    std::uint64_t selection{};
    std::uint64_t entryCount{};
    std::uint64_t folderCount{};
    std::uint64_t indexBytes{};
    MetadataPage page;
};

// One archive per service; numeric handles are service-scoped generations.
// Submission/invalidation only moves small handles under locks. The caller's
// cleanup worker must call reclaim() after invalidation and before destruction.
// Destroy, like TaskManager destruction, is an off-UI operation.
class BrowserService {
  public:
    explicit BrowserService(TaskManager& tasks, ReaderFactory factory = {});
    ~BrowserService();
    Result<BrowserSubmission> open(const std::filesystem::path& path, std::string password = {},
                                   MetadataLimits limits = {});
    Result<BrowserSubmission> query(std::uint64_t archive, MetadataQuery request);
    Result<TaskId> page(std::uint64_t archive, std::uint64_t query, std::uint64_t offset,
                        std::size_t rows, std::size_t bytes);
    Result<TaskId> folderPage(std::uint64_t archive, std::string folder, std::uint64_t offset,
                              std::size_t rows, std::size_t bytes);
    Result<BrowserSubmission> select(std::uint64_t archive, std::uint64_t query,
                                     std::vector<MetadataRange> ranges, bool all);
    Result<TaskId> extract(std::uint64_t archive, std::uint64_t query, std::uint64_t selection,
                           ExtractionRequest request);
    bool close(std::uint64_t archive);
    bool contains(std::uint64_t archive) const;
    bool valid(std::uint64_t archive, std::uint64_t query = 0, std::uint64_t selection = 0) const;
    void reclaim();

  private:
    struct State;
    struct View;
    TaskManager& tasks_;
    ReaderFactory factory_;
    ArchiveService archives_;
    struct Control;
    std::shared_ptr<Control> control_;
};
} // namespace ananarchiver::application
