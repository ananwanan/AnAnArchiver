#include "../../core/filesystem/compression_io.h"
#include "../../core/archive/archive_unicode_scope.h"
#include "ananarchiver/unicode.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

namespace ananarchiver::detail {
namespace {

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return value != INVALID_HANDLE_VALUE; }
};

ArchiveError ioError(std::string message, const std::filesystem::path& path,
                     DWORD code = GetLastError()) {
    ArchiveErrorCode kind = ArchiveErrorCode::IoError;
    if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_PRIVILEGE_NOT_HELD) {
        kind = ArchiveErrorCode::PermissionDenied;
    } else if (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL) {
        kind = ArchiveErrorCode::DiskFull;
    } else if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS) {
        kind = ArchiveErrorCode::OutputExists;
    } else if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
        kind = ArchiveErrorCode::FileNotFound;
    }
    return {kind, std::move(message), "Windows error " + std::to_string(code), path};
}

ArchiveError unsafe(const std::filesystem::path& path) {
    return {ArchiveErrorCode::UnsafePath, "Compression path contains a link, reparse point or unsafe output", {}, path};
}

std::filesystem::path plainAbsolute(const std::filesystem::path& path) {
    auto value = path.native();
    if (value.starts_with(L"\\\\?\\UNC\\")) value = L"\\\\" + value.substr(8);
    else if (value.starts_with(L"\\\\?\\")) value = value.substr(4);
    auto result = std::filesystem::absolute(std::filesystem::path{value}).lexically_normal();
    result.make_preferred();
    return result;
}

std::filesystem::path extended(const std::filesystem::path& path) {
    const auto value = plainAbsolute(path).native();
    return value.starts_with(L"\\\\") ? std::filesystem::path{L"\\\\?\\UNC\\" + value.substr(2)}
                                         : std::filesystem::path{L"\\\\?\\" + value};
}

Result<std::vector<Handle>> lockDirectories(const std::filesystem::path& parent) {
    std::vector<Handle> locks;
    const auto plain = plainAbsolute(parent);
    auto current = extended(plain.root_path());
    auto lock = [&](const std::filesystem::path& path) -> Result<void> {
        // Deny reparse retargeting and ancestor replacement until this
        // operation releases the lock. File payload writes remain possible.
        Handle handle{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!handle) return std::unexpected(ioError("Could not secure compression directory", path));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle.value, &info)) {
            return std::unexpected(ioError("Could not inspect compression directory", path));
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return std::unexpected(unsafe(path));
        }
        locks.push_back(std::move(handle));
        return {};
    };
    auto root = lock(current);
    if (!root) return std::unexpected(std::move(root.error()));
    for (const auto& part : plain.relative_path()) {
        current /= part;
        auto acquired = lock(current);
        if (!acquired) return std::unexpected(std::move(acquired.error()));
    }
    return locks;
}

Result<CompressionPathInfo> metadata(const BY_HANDLE_FILE_INFORMATION& value,
                                     const std::filesystem::path& path) {
    const bool directory = (value.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const auto ticks = (static_cast<std::uint64_t>(value.ftLastWriteTime.dwHighDateTime) << 32U) |
                       value.ftLastWriteTime.dwLowDateTime;
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    using NativeTicks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
    const auto magnitude = ticks >= epoch ? ticks - epoch : epoch - ticks;
    if (magnitude > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Compression timestamp exceeds clock limits", {}, path});
    }
    const auto delta = ticks >= epoch ? static_cast<std::int64_t>(magnitude) : -static_cast<std::int64_t>(magnitude);
    const auto minimum = std::chrono::duration_cast<NativeTicks>(std::chrono::system_clock::duration::min()).count();
    const auto maximum = std::chrono::duration_cast<NativeTicks>(std::chrono::system_clock::duration::max()).count();
    if (delta < minimum || delta > maximum) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Compression timestamp exceeds clock limits", {}, path});
    }
    const auto modified = std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(NativeTicks{delta})};
    const bool readonly = (value.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    return CompressionPathInfo{directory, directory ? 0 : (static_cast<std::uint64_t>(value.nFileSizeHigh) << 32U) | value.nFileSizeLow,
            directory ? (readonly ? 0555U : 0755U) : (readonly ? 0444U : 0644U), modified,
            {value.dwVolumeSerialNumber, (static_cast<std::uint64_t>(value.nFileIndexHigh) << 32U) | value.nFileIndexLow}};
}

bool matches(const CompressionPathInfo& first, const CompressionPathInfo& second) {
    return first.directory == second.directory && first.size == second.size &&
           first.modified == second.modified && first.identity == second.identity &&
           first.permissions == second.permissions;
}

Result<CompressionPathInfo> inspect(HANDLE handle, const std::filesystem::path& path) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) {
        return std::unexpected(ioError("Could not inspect compression path", path));
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return std::unexpected(unsafe(path));
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
                                            "Compression input must be a regular file or directory", {}, path});
    }
    return metadata(info, path);
}

std::string uniqueName() {
    static std::atomic<std::uint64_t> serial{0};
    std::random_device entropy;
    return ".ananarchiver-create-" + std::to_string(GetCurrentProcessId()) + "-" +
           std::to_string(entropy()) + "-" + std::to_string(++serial);
}

struct PrivateDirectorySecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ~PrivateDirectorySecurity() { if (descriptor != nullptr) LocalFree(descriptor); }
    Result<void> initialize(const std::filesystem::path& path) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;OICI;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr)) {
            return std::unexpected(ioError("Could not protect compression staging permissions", path));
        }
        return {};
    }
};

Result<void> checkDestination(const std::filesystem::path& path, bool overwrite) {
    Handle existing{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!existing) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        return std::unexpected(ioError("Could not inspect archive destination", path));
    }
    auto info = inspect(existing.value, path);
    if (!info) return std::unexpected(std::move(info.error()));
    if (info->directory) return std::unexpected(unsafe(path));
    if (!overwrite) return std::unexpected(ioError("Archive destination already exists", path, ERROR_ALREADY_EXISTS));
    return {};
}

} // namespace

struct CompressionDirectoryCursor::Impl {
    std::vector<Handle> locks;
    std::filesystem::path directory;
    HANDLE search = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW data{};
    bool first = true;
    bool finished = false;
    ~Impl() { if (search != INVALID_HANDLE_VALUE) FindClose(search); }
};

CompressionDirectoryCursor::CompressionDirectoryCursor(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CompressionDirectoryCursor::~CompressionDirectoryCursor() = default;

Result<std::unique_ptr<CompressionDirectoryCursor>> CompressionDirectoryCursor::open(
        const std::filesystem::path& directory) {
    auto locks = lockDirectories(directory);
    if (!locks) return std::unexpected(std::move(locks.error()));
    auto impl = std::make_unique<Impl>();
    impl->locks = std::move(*locks);
    impl->directory = extended(directory);
    const auto pattern = impl->directory / L"*";
    impl->search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &impl->data,
                                    FindExSearchNameMatch, nullptr, 0);
    if (impl->search == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_NO_MORE_FILES) impl->finished = true;
        else return std::unexpected(ioError("Could not enumerate compression directory", directory, code));
    }
    return std::make_unique<CompressionDirectoryCursor>(std::move(impl));
}

Result<std::optional<std::filesystem::path>> CompressionDirectoryCursor::next() {
    while (!impl_->finished) {
        if (!impl_->first && !FindNextFileW(impl_->search, &impl_->data)) {
            const auto code = GetLastError();
            if (code != ERROR_NO_MORE_FILES) {
                return std::unexpected(ioError("Could not continue compression directory enumeration", impl_->directory, code));
            }
            impl_->finished = true;
            break;
        }
        impl_->first = false;
        const std::wstring_view name{impl_->data.cFileName};
        if (name == L"." || name == L"..") continue;
        return std::optional{impl_->directory / std::filesystem::path{name}};
    }
    return std::optional<std::filesystem::path>{};
}

struct ArchiveUnicodeScope::Impl {};
ArchiveUnicodeScope::ArchiveUnicodeScope(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ArchiveUnicodeScope::~ArchiveUnicodeScope() = default;
Result<std::unique_ptr<ArchiveUnicodeScope>> ArchiveUnicodeScope::enter() {
    return std::make_unique<ArchiveUnicodeScope>(std::make_unique<Impl>());
}

std::filesystem::path compressionNativePath(const std::filesystem::path& path) { return extended(path); }
std::filesystem::path compressionParentPath(const std::filesystem::path& path) {
    return extended(plainAbsolute(path).parent_path());
}
bool compressionFilesystemRoot(const std::filesystem::path& path) {
    const auto plain = plainAbsolute(path);
    return plain == plain.root_path();
}
std::filesystem::path compressionRelativePath(const std::filesystem::path& path,
                                              const std::filesystem::path& root) {
    // MinGW rejects a drive component inside a device-prefixed relative_path,
    // returning empty even for two identical '\\?\C:\...' prefixes.
    return plainAbsolute(path).lexically_relative(plainAbsolute(root));
}

static Result<std::filesystem::path> canonicalNativePath(const std::filesystem::path& path,
                                                        bool requireDirectory) {
    auto locks = lockDirectories(compressionParentPath(path));
    if (!locks) return std::unexpected(std::move(locks.error()));
    const auto native = extended(path);
    Handle handle{CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle) return std::unexpected(ioError("Could not resolve native compression input name", native));
    auto valid = inspect(handle.value, native);
    if (!valid) return std::unexpected(std::move(valid.error()));
    if (requireDirectory && !valid->directory) return std::unexpected(unsafe(native));
    const DWORD size = GetFinalPathNameByHandleW(handle.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0) return std::unexpected(ioError("Could not resolve native compression input name", native));
    std::wstring name(size, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle.value, name.data(), size,
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0 || length >= size) {
        return std::unexpected(ioError("Could not resolve native compression input name", native));
    }
    name.resize(length);
    return std::filesystem::path{std::move(name)};
}

Result<std::filesystem::path> canonicalCompressionInputPath(const std::filesystem::path& path) {
    return canonicalNativePath(path, false);
}

Result<std::filesystem::path> canonicalCompressionParentPath(const std::filesystem::path& path) {
    return canonicalNativePath(path, true);
}

bool compressionPathContains(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto plainPath = plainAbsolute(path);
    const auto plainRoot = plainAbsolute(root);
    auto current = plainPath.begin();
    for (const auto& part : plainRoot) {
        if (current == plainPath.end()) return false;
        const auto first = current->native();
        const auto second = part.native();
        if (CompareStringOrdinal(first.c_str(), static_cast<int>(first.size()), second.c_str(),
                                 static_cast<int>(second.size()), TRUE) != CSTR_EQUAL) return false;
        ++current;
    }
    return true;
}

Result<CompressionPathInfo> inspectCompressionPath(const std::filesystem::path& path) {
    auto locks = lockDirectories(compressionParentPath(path));
    if (!locks) return std::unexpected(std::move(locks.error()));
    const auto native = extended(path);
    Handle handle{CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle) return std::unexpected(ioError("Could not inspect compression input", native));
    return inspect(handle.value, native);
}

struct CompressionInputFile::Impl {
    std::vector<Handle> ancestors;
    Handle file;
    std::filesystem::path path;
    CompressionPathInfo scanned;
};

CompressionInputFile::CompressionInputFile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CompressionInputFile::~CompressionInputFile() = default;

Result<std::unique_ptr<CompressionInputFile>> CompressionInputFile::open(
        const std::filesystem::path& path, const CompressionPathInfo& scanned) {
    auto locks = lockDirectories(compressionParentPath(path));
    if (!locks) return std::unexpected(std::move(locks.error()));
    auto native = extended(path);
    Handle file{CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file) return std::unexpected(ioError("Could not open compression input", native));
    auto value = inspect(file.value, native);
    if (!value) return std::unexpected(std::move(value.error()));
    if (value->directory || !matches(scanned, *value)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Compression input changed after scanning", {}, native});
    }
    auto impl = std::make_unique<Impl>();
    impl->ancestors = std::move(*locks);
    impl->file = std::move(file);
    impl->path = std::move(native);
    impl->scanned = scanned;
    return std::make_unique<CompressionInputFile>(std::move(impl));
}

Result<std::size_t> CompressionInputFile::read(std::span<std::byte> buffer) {
    const auto count = static_cast<DWORD>((std::min)(buffer.size(), static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD read = 0;
    if (!ReadFile(impl_->file.value, buffer.data(), count, &read, nullptr)) {
        return std::unexpected(ioError("Could not read compression input", impl_->path));
    }
    return static_cast<std::size_t>(read);
}

Result<void> CompressionInputFile::verifyUnchanged() {
    auto info = inspect(impl_->file.value, impl_->path);
    if (!info) return std::unexpected(std::move(info.error()));
    if (!matches(impl_->scanned, *info)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Compression input changed while being archived", {}, impl_->path});
    }
    return {};
}

struct AtomicArchiveOutput::Impl {
    std::vector<Handle> ancestors;
    Handle stageLock;
    Handle file;
    std::filesystem::path destination;
    std::filesystem::path stagePath;
    std::filesystem::path temporaryPath;
    bool ownsStage = false;
    bool overwrite = false;
    bool committed = false;
    bool cleaned = false;
};

AtomicArchiveOutput::AtomicArchiveOutput(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AtomicArchiveOutput::~AtomicArchiveOutput() {
    if (!impl_->cleaned) {
        try {
            auto result = rollback();
            if (!result) std::fputs("LiteArc: compression staging cleanup failed\n", stderr);
        } catch (...) { std::fputs("LiteArc: compression staging cleanup failed\n", stderr); }
    }
}

Result<std::unique_ptr<AtomicArchiveOutput>> AtomicArchiveOutput::open(
        const std::filesystem::path& destination, bool overwrite) {
    auto locks = lockDirectories(compressionParentPath(destination));
    if (!locks) return std::unexpected(std::move(locks.error()));
    auto native = extended(destination);
    auto valid = checkDestination(native, overwrite);
    if (!valid) return std::unexpected(std::move(valid.error()));
    auto impl = std::make_unique<Impl>();
    impl->ancestors = std::move(*locks);
    impl->destination = std::move(native);
    impl->overwrite = overwrite;
    auto output = std::make_unique<AtomicArchiveOutput>(std::move(impl));
    PrivateDirectorySecurity security;
    auto secured = security.initialize(destination);
    if (!secured) return std::unexpected(std::move(secured.error()));
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = security.descriptor;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        // Prepare every owning path before the namespace operation. No path
        // allocation may separate successful creation from ownership tracking.
        output->impl_->stagePath = compressionParentPath(output->impl_->destination) / pathFromUtf8(uniqueName());
        output->impl_->temporaryPath = output->impl_->stagePath / L"archive.tmp";
        if (!CreateDirectoryW(output->impl_->stagePath.c_str(), &attributes)) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) continue;
            return std::unexpected(ioError("Could not reserve archive staging directory", output->impl_->stagePath));
        }
        output->impl_->ownsStage = true;
        auto stageLocks = lockDirectories(output->impl_->stagePath);
        if (!stageLocks) return std::unexpected(std::move(stageLocks.error()));
        output->impl_->stageLock = std::move(stageLocks->back());
        output->impl_->file = Handle{CreateFileW(output->impl_->temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!output->impl_->file) return std::unexpected(ioError("Could not create temporary archive", output->impl_->temporaryPath));
        return output;
    }
    return std::unexpected(ioError("Could not reserve archive staging directory", destination, ERROR_ALREADY_EXISTS));
}

Result<void> AtomicArchiveOutput::write(std::span<const std::byte> data) {
    while (!data.empty()) {
        const auto chunk = static_cast<DWORD>((std::min)(data.size(), static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(impl_->file.value, data.data(), chunk, &written, nullptr) || written == 0) {
            return std::unexpected(ioError("Could not write temporary archive", impl_->destination));
        }
        data = data.subspan(written);
    }
    return {};
}

Result<void> AtomicArchiveOutput::finish() {
    if (!FlushFileBuffers(impl_->file.value)) return std::unexpected(ioError("Could not flush completed archive", impl_->destination));
    const auto file = std::exchange(impl_->file.value, INVALID_HANDLE_VALUE);
    if (!CloseHandle(file)) return std::unexpected(ioError("Could not close completed archive", impl_->destination));
    return {};
}

Result<void> AtomicArchiveOutput::commit(CancellationToken cancellation) {
    auto valid = checkDestination(impl_->destination, impl_->overwrite);
    if (!valid) return valid;
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (impl_->overwrite ? MOVEFILE_REPLACE_EXISTING : 0U);
    if (cancellation.stop_requested()) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled,
                                            "Archive creation cancelled", {}, impl_->destination});
    }
    if (!MoveFileExW(impl_->temporaryPath.c_str(), impl_->destination.c_str(), flags)) {
        return std::unexpected(ioError("Could not atomically install archive destination", impl_->destination));
    }
    impl_->committed = true;
    auto cleaned = rollback();
    if (!cleaned) {
        cleaned.error().code = ArchiveErrorCode::CleanupFailed;
        cleaned.error().message = "Archive was committed, but temporary cleanup failed";
    }
    return cleaned;
}

Result<void> AtomicArchiveOutput::rollback() {
    if (impl_->cleaned) return {};
    impl_->file = {};
    if (impl_->ownsStage) {
        if (!DeleteFileW(impl_->temporaryPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            return std::unexpected(ioError("Could not clean temporary archive", impl_->temporaryPath));
        }
        impl_->stageLock = {};
        if (!RemoveDirectoryW(impl_->stagePath.c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND) {
            return std::unexpected(ioError("Could not clean archive staging directory", impl_->stagePath));
        }
    }
    impl_->cleaned = true;
    return {};
}

bool AtomicArchiveOutput::committed() const noexcept { return impl_->committed; }

} // namespace ananarchiver::detail
