#pragma once

#include <ananarchiver/application/task_manager.h>

namespace ananarchiver::application {

using ReaderFactory = std::function<std::unique_ptr<ArchiveReader>()>;

class ArchiveService {
  public:
    explicit ArchiveService(TaskManager& tasks, ReaderFactory factory = {});
    Result<TaskId> open(const std::filesystem::path& path);

  private:
    TaskManager& tasks_;
    ReaderFactory factory_;
};

} // namespace ananarchiver::application
