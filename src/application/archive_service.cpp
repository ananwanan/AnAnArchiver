#include <ananarchiver/application/archive_service.h>
#include <ananarchiver/libarchive_reader.h>

#include <limits>

namespace ananarchiver::application {

ArchiveService::ArchiveService(TaskManager& tasks, ReaderFactory factory)
    : tasks_(tasks), factory_(std::move(factory)) {
    if (!factory_)
        factory_ = [] { return makeLibArchiveReader(); };
}

Result<TaskId> ArchiveService::open(const std::filesystem::path& path) {
    return tasks_.start(
        TaskType::OpenArchive, [path, factory = factory_](TaskContext& context) -> Result<void> {
            auto reader = factory();
            if (!reader) {
                return std::unexpected(ArchiveError{ArchiveErrorCode::BackendError,
                                                    "Archive reader factory returned no reader",
                                                    {},
                                                    path});
            }
            auto info = reader->open(path, context.cancellation());
            if (!info)
                return std::unexpected(info.error());
            context.setArchiveInfo(std::move(*info));
            std::vector<ArchiveEntry> batch;
            batch.reserve(512);
            std::uint64_t bytes = 0;
            std::uint64_t entries = 0;
            bool allSizesKnown = true;
            auto result = reader->enumerate(
                [&](const ArchiveEntry& entry) {
                    if (context.cancellation().stop_requested())
                        return false;
                    if (entry.size > std::numeric_limits<std::uint64_t>::max() - bytes) {
                        bytes = std::numeric_limits<std::uint64_t>::max();
                        allSizesKnown = false;
                    } else {
                        bytes += entry.size;
                    }
                    ++entries;
                    allSizesKnown = allSizesKnown && entry.sizeKnown;
                    batch.push_back(entry);
                    if (batch.size() < 512)
                        return true;
                    context.report(entry.path, bytes, entries, allSizesKnown);
                    if (!context.publishEntries(std::move(batch)))
                        return false;
                    batch = {};
                    batch.reserve(512);
                    return true;
                },
                context.cancellation());
            context.report(batch.empty() ? std::string{} : batch.back().path, bytes, entries,
                           allSizesKnown);
            if (!result)
                return result;
            if (!batch.empty() && !context.publishEntries(std::move(batch))) {
                return std::unexpected(ArchiveError{
                    ArchiveErrorCode::Cancelled, "Archive indexing cancelled", {}, path});
            }
            return {};
        });
}

} // namespace ananarchiver::application
