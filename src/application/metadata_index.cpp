#include <algorithm>
#include <ananarchiver/application/metadata_index.h>
#include <ananarchiver/path_security.h>
#include <ananarchiver/unicode.h>
#include <limits>
#include <stdexcept>

namespace ananarchiver::application {
namespace {
struct Fold {
    std::uint32_t point;
    std::u16string_view text;
};
constexpr Fold folds[] = {
#include "case_fold_data.inc"
};
struct Failure {
    ArchiveError error;
};
[[noreturn]] void fail(ArchiveErrorCode code, const char* message) {
    throw Failure{{code, message, {}, {}}};
}
void check(CancellationToken stop) {
    if (stop.stop_requested())
        fail(ArchiveErrorCode::Cancelled, "Metadata operation cancelled");
}
template <class F> auto guarded(F&& work) -> Result<decltype(work())> {
    try {
        return work();
    } catch (const Failure& e) {
        return std::unexpected(e.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ArchiveError{
            ArchiveErrorCode::ResourceLimitExceeded, "Metadata allocation failed", {}, {}});
    }
}
std::u16string_view leaf(std::u16string_view path) {
    while (!path.empty()) {
        while (!path.empty() && (path.back() == u'/' || path.back() == u'\\'))
            path.remove_suffix(1);
        const auto pos = path.find_last_of(u"/\\");
        const auto part = pos == path.npos ? path : path.substr(pos + 1);
        if (part != u".")
            return part;
        path = pos == path.npos ? std::u16string_view{} : path.substr(0, pos);
    }
    return {};
}

} // namespace

std::string normalizedMetadataPath(std::string_view path) {
    std::string result;
    for (std::size_t start = 0; start < path.size();) {
        auto end = path.find_first_of("/\\", start);
        if (end == path.npos)
            end = path.size();
        const auto part = path.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (!result.empty())
                result += '/';
            result += part;
        }
        start = end + 1;
    }
    return result;
}

static std::u16string transformText(std::string_view text, bool fold) {
    if (!isValidUtf8(text))
        throw std::invalid_argument("Invalid metadata UTF-8");
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        auto point = static_cast<std::uint32_t>(static_cast<unsigned char>(text[i++]));
        unsigned more = 0;
        if (point >= 0xF0) {
            point &= 7;
            more = 3;
        } else if (point >= 0xE0) {
            point &= 15;
            more = 2;
        } else if (point >= 0xC0) {
            point &= 31;
            more = 1;
        }
        while (more--)
            point = (point << 6) | (static_cast<unsigned char>(text[i++]) & 63U);
        const auto found =
            std::lower_bound(std::begin(folds), std::end(folds), point,
                             [](const Fold& a, std::uint32_t b) { return a.point < b; });
        if (found != std::end(folds) && found->point == point && fold)
            out += found->text;
        else if (point < 0x10000)
            out += static_cast<char16_t>(point);
        else {
            point -= 0x10000;
            out += static_cast<char16_t>(0xD800U + (point >> 10));
            out += static_cast<char16_t>(0xDC00U + (point & 1023U));
        }
    }
    return out;
}

std::u16string foldMetadataText(std::string_view text) { return transformText(text, true); }

Result<std::shared_ptr<const MetadataIndex>>
MetadataIndex::build(std::vector<MetadataBatch> batches, MetadataLimits limits,
                     CancellationToken stop) {
    return guarded([&]() -> std::shared_ptr<const MetadataIndex> {
        if (!limits.maxFolders)
            fail(ArchiveErrorCode::ResourceLimitExceeded,
                 "Metadata root exceeds folder node limit");
        auto result = std::shared_ptr<MetadataIndex>(new MetadataIndex);
        auto& self = *result;
        auto charge = [&](std::size_t amount) {
            if (amount > limits.maxIndexBytes - std::min(self.bytes_, limits.maxIndexBytes))
                fail(ArchiveErrorCode::ResourceLimitExceeded,
                     "Metadata index memory limit exceeded");
            self.bytes_ += amount;
        };
        charge(sizeof(MetadataIndex) + batches.capacity() * sizeof(MetadataBatch) + 512);
        std::size_t count = 0;
        for (const auto& batch : batches) {
            check(stop);
            if (!batch || batch->size() > limits.maxEntries - std::min(count, limits.maxEntries))
                fail(ArchiveErrorCode::ResourceLimitExceeded,
                     "Metadata entry count limit exceeded");
            count += batch->size();
            charge(batch->capacity() * sizeof(ArchiveEntry));
        }
        charge(count * (sizeof(Item) + sizeof(std::size_t) * 2));
        self.items_.reserve(count);
        self.batches_ = std::move(batches);
        std::size_t folderText = 0;
        for (const auto& batch : self.batches_)
            for (const auto& entry : *batch) {
                check(stop);
                if (entry.path.size() > limits.maxPathBytes)
                    fail(ArchiveErrorCode::ResourceLimitExceeded,
                         "Metadata pathname limit exceeded");
                if (!isValidUtf8(entry.path) || entry.path.find('\0') != entry.path.npos)
                    fail(ArchiveErrorCode::InvalidPathEncoding, "Invalid metadata UTF-8");
                charge(entry.path.capacity() + 1 + 2 * (entry.path.size() * 3 + 1));
                auto path = normalizedMetadataPath(entry.path);
                if (static_cast<std::size_t>(std::count(path.begin(), path.end(), '/')) >=
                    limits.maxDepth)
                    fail(ArchiveErrorCode::ResourceLimitExceeded,
                         "Metadata hierarchy depth limit exceeded");
                auto directory = entry.type == EntryType::Directory
                                     ? path
                                     : path.substr(0, path.find_last_of('/') == path.npos
                                                          ? 0
                                                          : path.find_last_of('/'));
                std::size_t parent = 0;
                for (std::size_t start = 0; start < directory.size();) {
                    auto end = directory.find('/', start);
                    if (end == directory.npos)
                        end = directory.size();
                    auto prefix = directory.substr(0, end);
                    const auto found = self.folders_->byPath.find(prefix);
                    if (found != self.folders_->byPath.end())
                        parent = found->second;
                    else {
                        if (self.folders_->folders.size() >= limits.maxFolders)
                            fail(ArchiveErrorCode::ResourceLimitExceeded,
                                 "Metadata folder node limit exceeded");
                        const auto text = 2 * prefix.size() + 7 * (end - start) + 8;
                        if (text > limits.maxFolderTextBytes -
                                       std::min(folderText, limits.maxFolderTextBytes))
                            fail(ArchiveErrorCode::ResourceLimitExceeded,
                                 "Metadata folder path memory limit exceeded");
                        folderText += text;
                        // Include spare vector capacity, hash buckets/nodes, string
                        // capacity and allocator bookkeeping conservatively.
                        charge(text + 2 * sizeof(MetadataFolder) + 192);
                        const auto id = self.folders_->folders.size();
                        auto name = directory.substr(start, end - start);
                        self.folders_->folders.push_back({prefix,
                                                          name,
                                                          foldMetadataText(name),
                                                          transformText(name, false),
                                                          parent,
                                                          0,
                                                          {},
                                                          {}});
                        self.folders_->folders[parent].children.push_back(id);
                        self.folders_->byPath.emplace(std::move(prefix), id);
                        parent = id;
                    }
                    start = end + 1;
                }
                self.folders_->folders[parent].files.push_back(self.items_.size());
                self.items_.push_back({&entry, foldMetadataText(entry.path)});
            }
        for (auto& folder : self.folders_->folders) {
            check(stop);
            std::size_t comparisons = 0;
            std::sort(folder.children.begin(), folder.children.end(), [&](auto a, auto b) {
                if ((++comparisons & 4095U) == 0)
                    check(stop);
                const auto& left = self.folders_->folders[a];
                const auto& right = self.folders_->folders[b];
                return left.foldedName == right.foldedName ? left.rawName < right.rawName
                                                           : left.foldedName < right.foldedName;
            });
            for (std::size_t i = 0; i < folder.children.size(); ++i)
                self.folders_->folders[folder.children[i]].row = static_cast<int>(i);
        }
        check(stop);
        return result;
    });
}

Result<std::vector<std::uint64_t>> MetadataIndex::query(const MetadataQuery& query,
                                                        CancellationToken stop) const {
    return guarded([&] {
        if (query.sortColumn < -1 || query.sortColumn > 3 || !isValidUtf8(query.search))
            fail(ArchiveErrorCode::UnsupportedOperation, "Invalid metadata query");
        const auto needle = foldMetadataText(query.search);
        const auto folder = folders_->byPath.find(normalizedMetadataPath(query.folder));
        if (query.scoped && folder == folders_->byPath.end())
            fail(ArchiveErrorCode::UnsupportedOperation, "Metadata folder does not exist");
        std::vector<std::uint64_t> result;
        const auto* scoped = query.scoped ? &folders_->folders[folder->second] : nullptr;
        result.reserve(scoped ? scoped->children.size() +
                                    (query.foldersOnly ? 0 : scoped->files.size())
                              : items_.size());
        if (scoped) {
            for (const auto id : scoped->children) {
                check(stop);
                if (folders_->folders[id].foldedName.find(needle) != std::u16string::npos)
                    result.push_back(folderBit | id);
            }
            if (query.sortColumn == 0 && query.descending)
                std::reverse(result.begin(), result.end());
        }
        const auto firstFile = result.size();
        if (!query.foldersOnly) {
            const auto count = scoped ? scoped->files.size() : items_.size();
            for (std::size_t i = 0; i < count; ++i) {
                if ((i & 255U) == 0)
                    check(stop);
                const auto id = scoped ? scoped->files[i] : i;
                const auto& item = items_[id];
                if (scoped && item.entry->type == EntryType::Directory)
                    continue;
                const auto text = scoped ? leaf(item.folded) : std::u16string_view(item.folded);
                if (text.find(needle) != text.npos)
                    result.push_back(id);
            }
        }
        std::size_t comparisons = 0;
        if (query.sortColumn >= 0)
            std::sort(
                result.begin() + static_cast<std::ptrdiff_t>(firstFile), result.end(),
                [&](auto a, auto b) {
                    if ((++comparisons & 4095U) == 0)
                        check(stop);
                    const auto& left = *items_[a].entry;
                    const auto& right = *items_[b].entry;
                    int order = 0;
                    if (query.sortColumn == 0)
                        order = (scoped ? leaf(items_[a].folded)
                                        : std::u16string_view(items_[a].folded))
                                    .compare(scoped ? leaf(items_[b].folded)
                                                    : std::u16string_view(items_[b].folded));
                    if (query.sortColumn == 1) {
                        const auto ak = left.sizeKnown && left.type != EntryType::Directory;
                        const auto bk = right.sizeKnown && right.type != EntryType::Directory;
                        if (ak != bk)
                            return ak;
                        if (ak)
                            order = left.size < right.size ? -1 : (left.size > right.size ? 1 : 0);
                    }
                    if (query.sortColumn == 2)
                        order = left.type < right.type ? -1 : (left.type > right.type ? 1 : 0);
                    if (query.sortColumn == 3) {
                        if (left.modified.has_value() != right.modified.has_value())
                            return left.modified.has_value();
                        if (left.modified)
                            order = *left.modified < *right.modified
                                        ? -1
                                        : (*left.modified > *right.modified ? 1 : 0);
                    }
                    return order == 0 ? a < b : (query.descending ? order > 0 : order < 0);
                });
        check(stop);
        return result;
    });
}

const ArchiveEntry& MetadataIndex::entry(std::uint64_t row) const {
    return *items_.at(static_cast<std::size_t>(row)).entry;
}
std::string MetadataIndex::name(std::uint64_t row) const {
    if (row & folderBit)
        return folders_->folders.at(static_cast<std::size_t>(row & ~folderBit)).name;
    const auto path = normalizedMetadataPath(entry(row).path);
    return path.substr(path.find_last_of('/') == path.npos ? 0 : path.find_last_of('/') + 1);
}
Result<MetadataPage> MetadataIndex::page(std::span<const std::uint64_t> view, std::uint64_t offset,
                                         std::size_t rows, std::size_t bytes,
                                         CancellationToken stop) const {
    return guarded([&] {
        if (!rows || rows > maxPageRows || bytes > maxPageBytes || bytes < rowWireBytes ||
            offset > view.size())
            fail(ArchiveErrorCode::UnsupportedOperation, "Invalid metadata page window");
        MetadataPage result{offset, view.size(), 128, {}};
        result.rows.reserve(std::min(rows, view.size() - static_cast<std::size_t>(offset)));
        for (std::size_t i = static_cast<std::size_t>(offset);
             i < view.size() && result.rows.size() < rows; ++i) {
            check(stop);
            const auto id = view[i];
            const bool folder = (id & folderBit) != 0;
            const auto& path =
                folder ? folders_->folders.at(static_cast<std::size_t>(id & ~folderBit)).path
                       : entry(id).path;
            auto rowName = name(id);
            const auto cost = rowWireBytes + path.size() + rowName.size();
            if (cost > bytes - result.bytes) {
                if (result.rows.empty())
                    fail(ArchiveErrorCode::ResourceLimitExceeded,
                         "Metadata row exceeds page byte budget");
                break;
            }
            MetadataRow row{id & ~folderBit, folder, std::move(rowName), {}};
            if (folder) {
                row.entry.path = path;
                row.entry.type = EntryType::Directory;
                row.entry.index = noEntryIndex;
                row.entry.sizeKnown = false;
            } else
                row.entry = entry(id);
            result.bytes += cost;
            result.rows.push_back(std::move(row));
        }
        return result;
    });
}
Result<MetadataPage> MetadataIndex::folderPage(std::string_view folder, std::uint64_t offset,
                                               std::size_t rows, std::size_t bytes,
                                               CancellationToken stop) const {
    auto found = folders_->byPath.find(normalizedMetadataPath(folder));
    if (found == folders_->byPath.end() || rows == 0 || rows > maxPageRows)
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::UnsupportedOperation, "Invalid folder window", {}, {}});
    const auto& children = folders_->folders[found->second].children;
    if (offset > children.size())
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::UnsupportedOperation, "Invalid folder offset", {}, {}});
    std::vector<std::uint64_t> ids;
    const auto end = std::min(children.size(), static_cast<std::size_t>(offset) + rows);
    for (auto i = static_cast<std::size_t>(offset); i < end; ++i)
        ids.push_back(folderBit | children[i]);
    auto result = page(ids, 0, rows, bytes, stop);
    if (result) {
        result->offset = offset;
        result->total = children.size();
    }
    return result;
}

Result<MetadataSelection> MetadataIndex::resolve(std::span<const std::uint64_t> view,
                                                 std::span<const MetadataRange> ranges,
                                                 CancellationToken stop) const {
    return guarded([&] {
        MetadataSelection result;
        std::uint64_t previous = 0;
        for (const auto range : ranges) {
            if (range.first < previous || range.first > view.size() ||
                range.count > view.size() - range.first)
                fail(ArchiveErrorCode::UnsupportedOperation,
                     "Invalid or overlapping metadata selection ranges");
            previous = range.first + range.count;
            for (auto i = range.first; i < previous; ++i) {
                if ((i & 255U) == 0)
                    check(stop);
                const auto id = view[static_cast<std::size_t>(i)];
                if ((id & folderBit) || entry(id).type == EntryType::Directory) {
                    auto path =
                        id & folderBit
                            ? folders_->folders.at(static_cast<std::size_t>(id & ~folderBit)).path
                            : normalizedMetadataPath(entry(id).path);
                    auto safe = secureArchivePath(path);
                    if (!safe)
                        throw Failure{safe.error()};
                    result.directories.push_back(std::move(path));
                } else
                    result.indices.push_back(entry(id).index);
            }
        }
        check(stop);
        return result;
    });
}
} // namespace ananarchiver::application
