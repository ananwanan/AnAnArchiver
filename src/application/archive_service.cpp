#include <ananarchiver/application/archive_service.h>
#include <ananarchiver/libarchive_reader.h>
#include <ananarchiver/libarchive_writer.h>
#include <ananarchiver/password.h>

#include <limits>

namespace ananarchiver::application {
namespace {
struct ArchiveOperation {
    std::string password;
    ExtractionRequest extraction;
    CompressionRequest compression;
    ~ArchiveOperation() {
        scrubPassword(password);
        if (extraction.password)
            scrubPassword(*extraction.password);
        if (compression.password)
            scrubPassword(*compression.password);
    }
};
} // namespace

ArchiveService::ArchiveService(TaskManager& tasks, ReaderFactory factory, WriterFactory writer)
    : tasks_(tasks), factory_(std::move(factory)), writerFactory_(std::move(writer)) {
    if (!factory_)
        factory_ = [] { return makeLibArchiveReader(); };
    if (!writerFactory_)
        writerFactory_ = [] { return makeLibArchiveWriter(); };
}

Result<TaskId> ArchiveService::open(const std::filesystem::path& path, std::string password) {
    auto secret = std::make_shared<ArchiveOperation>();
    secret->password = std::move(password);
    return tasks_.start(
        TaskType::OpenArchive, [path, secret, factory = factory_](TaskContext& context) -> Result<void> {
            auto reader = factory();
            if (!reader) {
                return std::unexpected(ArchiveError{ArchiveErrorCode::BackendError,
                                                    "Archive reader factory returned no reader",
                                                    {},
                                                    path});
            }
            auto info = secret->password.empty()
                            ? reader->open(path, context.cancellation())
                            : reader->open(path, secret->password, context.cancellation());
            scrubPassword(secret->password);
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
                    batch.back().index = entries - 1;
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

Result<TaskId> ArchiveService::extract(const std::filesystem::path& path,
                                      ExtractionRequest request) {
    auto owned = std::make_shared<ArchiveOperation>();
    owned->extraction = std::move(request);
    return tasks_.start(TaskType::ExtractArchive,
        [path, owned, factory = factory_](TaskContext& context) -> Result<void> {
            auto reader = factory();
            if (!reader)
                return std::unexpected(ArchiveError{ArchiveErrorCode::BackendError,
                    "Archive reader factory returned no reader", {}, path});
            auto& extraction = owned->extraction;
            const auto info = extraction.password && !extraction.password->empty()
                                  ? reader->open(path, *extraction.password, context.cancellation())
                                  : reader->open(path, context.cancellation());
            if (!info)
                return std::unexpected(info.error());
            context.setArchiveInfo(*info);
            if (extraction.overwrite == OverwritePolicy::Ask && !extraction.onConflict) {
                extraction.onConflict = [&context](const ExtractionConflict& conflict) {
                    return context.requestConflict(conflict);
                };
            }
            // A bounded stream callback updates only a snapshot. The frontend
            // polls at its own cadence; no per-block GUI events accumulate.
            return reader->extract(extraction, [&context](const ExtractionProgress& progress) {
                context.report(progress);
                return !context.cancellation().stop_requested();
            }, context.cancellation());
        });
}

std::vector<ArchiveCapabilities> ArchiveService::compressionCapabilities() const {
    auto writer = writerFactory_();
    return writer ? writer->capabilities() : std::vector<ArchiveCapabilities>{};
}

Result<TaskId> ArchiveService::compress(CompressionRequest request) {
    auto owned = std::make_shared<ArchiveOperation>();
    owned->compression = std::move(request);
    return tasks_.start(TaskType::CompressArchive,
        [owned, factory = writerFactory_](TaskContext& context) -> Result<void> {
            auto writer = factory();
            if (!writer)
                return std::unexpected(ArchiveError{ArchiveErrorCode::BackendError,
                    "Archive writer factory returned no writer", {}, owned->compression.destination});
            return writer->create(owned->compression,
                [&context](const CompressionProgress& progress) {
                    context.report(progress);
                    return !context.cancellation().stop_requested();
                }, context.cancellation());
        });
}

} // namespace ananarchiver::application
