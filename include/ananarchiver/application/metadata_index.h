#pragma once

#include <ananarchiver/archive_reader.h>
#include <memory>
#include <span>
#include <unordered_map>

namespace ananarchiver::application {

struct MetadataLimits {
    std::size_t maxEntries{2'000'000};
    std::size_t maxFolders{1'000'000};
    std::size_t maxDepth{256};
    std::size_t maxPathBytes{64 * 1024};
    std::size_t maxFolderTextBytes{128 * 1024 * 1024};
    std::size_t maxIndexBytes{512 * 1024 * 1024};
};

// This normalization is for display grouping only. Selection paths are checked
// again by secureArchivePath; original metadata is never an output destination.
std::string normalizedMetadataPath(std::string_view path);
// Default simple (C + S) folding, as in the legacy QString::toCaseFolded.
// Locale independent, UTF-16 comparison keys, no Unicode normalization.
std::u16string foldMetadataText(std::string_view text);

using MetadataBatch = std::shared_ptr<const std::vector<ArchiveEntry>>;
struct MetadataFolder {
    std::string path;
    std::string name;
    std::u16string foldedName;
    std::u16string rawName;
    std::size_t parent{};
    int row{};
    std::vector<std::size_t> children;
    std::vector<std::size_t> files;
};
struct MetadataFolders {
    std::vector<MetadataFolder> folders{MetadataFolder{}};
    std::unordered_map<std::string, std::size_t> byPath{{"", 0}};
};
struct MetadataQuery {
    std::string folder;
    std::string search;
    int sortColumn{-1};
    bool descending{};
    bool scoped{true};
    bool foldersOnly{};
};
struct MetadataRange {
    std::uint64_t first{};
    std::uint64_t count{};
};
struct MetadataSelection {
    std::vector<std::uint64_t> indices;
    std::vector<std::string> directories;
};
struct MetadataRow {
    // Folder IDs occupy a separate namespace and have no archive ordinal.
    std::uint64_t identity{};
    bool folder{};
    std::string name;
    ArchiveEntry entry;
};
struct MetadataPage {
    std::uint64_t offset{};
    std::uint64_t total{};
    std::uint64_t bytes{};
    std::vector<MetadataRow> rows;
};

// Immutable after build. Build/query/page/resolve are worker operations. A view
// and its source must be held together; row IDs are valid only for that source.
class MetadataIndex {
  public:
    static constexpr std::uint64_t folderBit = std::uint64_t{1} << 63;
    static constexpr std::uint64_t noEntryIndex = ~std::uint64_t{0};
    static constexpr std::size_t maxPageRows = 512;
    static constexpr std::size_t maxPageBytes = 1024 * 1024;
    // Conservative wire allowance >= sizeof(aa_browser_row), on every target.
    static constexpr std::size_t rowWireBytes = 160;
    static Result<std::shared_ptr<const MetadataIndex>> build(std::vector<MetadataBatch> batches,
                                                              MetadataLimits limits = {},
                                                              CancellationToken stop = {});
    Result<std::vector<std::uint64_t>> query(const MetadataQuery& query,
                                             CancellationToken stop = {}) const;
    Result<MetadataPage> page(std::span<const std::uint64_t> view, std::uint64_t offset,
                              std::size_t rows, std::size_t bytes,
                              CancellationToken stop = {}) const;
    Result<MetadataSelection> resolve(std::span<const std::uint64_t> view,
                                      std::span<const MetadataRange> ranges,
                                      CancellationToken stop = {}) const;
    Result<MetadataPage> folderPage(std::string_view folder, std::uint64_t offset, std::size_t rows,
                                    std::size_t bytes, CancellationToken stop = {}) const;
    const ArchiveEntry& entry(std::uint64_t row) const;
    std::string name(std::uint64_t row) const;
    const MetadataFolders& hierarchy() const { return *folders_; }
    std::shared_ptr<const MetadataFolders> hierarchySnapshot() const { return folders_; }
    std::size_t count() const { return items_.size(); }
    std::size_t accountedBytes() const { return bytes_; }

  private:
    struct Item {
        const ArchiveEntry* entry{};
        std::u16string folded;
    };
    std::vector<MetadataBatch> batches_;
    std::vector<Item> items_;
    std::shared_ptr<MetadataFolders> folders_{std::make_shared<MetadataFolders>()};
    std::size_t bytes_{};
};
} // namespace ananarchiver::application
