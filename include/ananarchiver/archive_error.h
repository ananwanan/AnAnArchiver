#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace ananarchiver {

enum class ArchiveErrorCode {
    FileNotFound,
    PermissionDenied,
    UnsupportedFormat,
    UnsupportedCompression,
    UnsupportedOperation,
    InvalidArchive,
    CorruptedArchive,
    WrongPassword,
    PasswordRequired,
    DiskFull,
    OutputExists,
    Cancelled,
    ResourceLimitExceeded,
    InvalidPathEncoding,
    IoError,
    BackendWarning,
    BackendError,
    Unknown
};

struct ArchiveError {
    ArchiveErrorCode code = ArchiveErrorCode::Unknown;
    std::string message;
    std::string backendMessage;
    std::filesystem::path path;
};

template <class T>
using Result = std::expected<T, ArchiveError>;

} // namespace ananarchiver
