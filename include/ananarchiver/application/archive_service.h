#pragma once

#include <ananarchiver/application/task_manager.h>
#include <ananarchiver/archive_writer.h>

namespace ananarchiver::application {

using ReaderFactory = std::function<std::unique_ptr<ArchiveReader>()>;
using WriterFactory = std::function<std::unique_ptr<ArchiveWriter>()>;

class ArchiveService {
  public:
    explicit ArchiveService(TaskManager& tasks, ReaderFactory factory = {}, WriterFactory writer = {});
    Result<TaskId> open(const std::filesystem::path& path, std::string password = {});
    Result<TaskId> extract(const std::filesystem::path& path, ExtractionRequest request);
    // Capability discovery configures backend options only; it performs no IO.
    std::vector<ArchiveCapabilities> compressionCapabilities() const;
    Result<TaskId> compress(CompressionRequest request);

  private:
    TaskManager& tasks_;
    ReaderFactory factory_;
    WriterFactory writerFactory_;
};

} // namespace ananarchiver::application
