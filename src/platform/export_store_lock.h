#pragma once
#include <filesystem>
#include <memory>
namespace ananarchiver::application {
class ExportStoreLock {
  public:
    virtual ~ExportStoreLock() = default;
};
std::filesystem::path exportIoPath(const std::filesystem::path& path);
std::unique_ptr<ExportStoreLock> lockExportStore(const std::filesystem::path& path);
} // namespace ananarchiver::application
