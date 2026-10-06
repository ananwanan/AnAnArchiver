#include <ananarchiver/application/directory_service.h>
#include <ananarchiver/unicode.h>

#include <algorithm>
#include <exception>

namespace ananarchiver::application {
namespace {
// C++20 file_clock implementations provide either to_sys (GCC/libc++) or
// to_utc (MSVC). Keep the epoch conversion exact without platform path logic.
template <class Clock, class Duration>
std::chrono::system_clock::time_point systemTime(std::chrono::time_point<Clock, Duration> time) {
    if constexpr (requires { Clock::to_sys(time); }) {
        return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            Clock::to_sys(time));
    } else {
        using UtcClock = typename decltype(Clock::to_utc(time))::clock;
        return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            UtcClock::to_sys(Clock::to_utc(time)));
    }
}

ArchiveError directoryError(const std::error_code& ec, const std::filesystem::path& path) {
    auto code = ArchiveErrorCode::IoError;
    if (ec == std::errc::no_such_file_or_directory)
        code = ArchiveErrorCode::FileNotFound;
    else if (ec == std::errc::permission_denied)
        code = ArchiveErrorCode::PermissionDenied;
    return {code, "Unable to enumerate directory", ec.message(), path};
}
} // namespace

Result<std::vector<DirectoryEntry>>
DirectoryService::scanChildren(const std::filesystem::path& path, CancellationToken cancellation) {
    if (cancellation.stop_requested())
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::Cancelled, "Directory scan cancelled", {}, path});
    std::error_code ec;
    std::filesystem::directory_iterator iterator(path, ec);
    if (ec)
        return std::unexpected(directoryError(ec, path));
    std::vector<DirectoryEntry> entries;
    constexpr std::size_t maxMetadataBytes = 512ULL * 1024 * 1024;
    std::size_t metadataBytes = 0;
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        if (cancellation.stop_requested())
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::Cancelled, "Directory scan cancelled", {}, path});
        const auto& item = *iterator;
        const auto status = item.symlink_status(ec);
        if (ec)
            return std::unexpected(directoryError(ec, item.path()));
        DirectoryEntry entry;
        entry.path = item.path();
        try {
            entry.name = pathToUtf8(entry.path.filename());
        } catch (const std::exception& error) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidPathEncoding,
                                                "Directory entry is not valid Unicode",
                                                error.what(), entry.path});
        }
        entry.directory = std::filesystem::is_directory(status);
        if (std::filesystem::is_regular_file(status)) {
            entry.size = item.file_size(ec);
            if (ec)
                return std::unexpected(directoryError(ec, item.path()));
        }
        // Do not stat a symlink target (including dangling/network links).
        if (!std::filesystem::is_symlink(status)) {
            const auto modified = item.last_write_time(ec);
            if (ec)
                return std::unexpected(directoryError(ec, item.path()));
            entry.modified = systemTime(modified);
        }
        const auto nativeBytes =
            entry.path.native().size() * sizeof(std::filesystem::path::value_type);
        const auto entryBytes = sizeof(DirectoryEntry) + nativeBytes + entry.name.size();
        if (entryBytes > maxMetadataBytes - metadataBytes)
            return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                                "Directory metadata exceeds the memory budget",
                                                {},
                                                path});
        metadataBytes += entryBytes;
        entries.push_back(std::move(entry));
        if (entries.size() >= 1'000'000)
            return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                                "Directory contains too many immediate children",
                                                {},
                                                path});
        iterator.increment(ec);
        if (ec)
            return std::unexpected(directoryError(ec, path));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        if (left.directory != right.directory)
            return left.directory;
        return left.name < right.name;
    });
    if (cancellation.stop_requested())
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::Cancelled, "Directory scan cancelled", {}, path});
    return entries;
}

} // namespace ananarchiver::application
