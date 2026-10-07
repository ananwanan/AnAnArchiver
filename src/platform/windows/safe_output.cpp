#include "../../core/filesystem/safe_output.h"
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
    } else if (code == ERROR_NOT_SUPPORTED) {
        kind = ArchiveErrorCode::UnsupportedOperation;
    }
    return {kind, std::move(message), "Windows error " + std::to_string(code), path};
}

ArchiveError unsafe(const std::filesystem::path& path) {
    return {ArchiveErrorCode::UnsafePath,
            "Extraction path contains a link, reparse point or special file", {}, path};
}

std::filesystem::path extended(const std::filesystem::path& path) {
    auto text = path.native();
    if (text.starts_with(L"\\\\?\\UNC\\")) text = L"\\\\" + text.substr(8);
    else if (text.starts_with(L"\\\\?\\")) text = text.substr(4);
    text = std::filesystem::absolute(std::filesystem::path{text}).lexically_normal().native();
    if (text.starts_with(L"\\\\?\\")) return std::filesystem::path{text};
    if (text.starts_with(L"\\\\")) return std::filesystem::path{L"\\\\?\\UNC\\" + text.substr(2)};
    return std::filesystem::path{L"\\\\?\\" + text};
}

Result<Handle> lockDirectory(const std::filesystem::path& path) {
    // No sharing of WRITE/DELETE denies replacement and reparse retargeting
    // while this ancestor is used. Child creation does not require an open
    // write handle to the parent directory.
    Handle handle{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr)};
    if (!handle) return std::unexpected(ioError("Could not secure extraction directory", path));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.value, &info)) {
        return std::unexpected(ioError("Could not inspect extraction directory", path));
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return std::unexpected(unsafe(path));
    }
    return handle;
}

std::optional<std::chrono::system_clock::time_point> modifiedTime(FILETIME time) {
    const auto ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) |
                       time.dwLowDateTime;
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    if (ticks < epoch) return {};
    const auto unixTicks = ticks - epoch;
    if (unixTicks > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) / 100U) {
        return {};
    }
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds{static_cast<std::int64_t>(unixTicks * 100U)})};
}

std::string uniqueName() {
    static std::atomic<std::uint64_t> counter{0};
    std::random_device entropy;
    return ".ananarchiver-" + std::to_string(GetCurrentProcessId()) + "-" +
           std::to_string(entropy()) + "-" + std::to_string(++counter);
}

struct PrivateDirectorySecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ~PrivateDirectorySecurity() { if (descriptor != nullptr) LocalFree(descriptor); }
    Result<void> initialize(const std::filesystem::path& path) {
        // OWNER RIGHTS is relative to the object owner. The protected DACL
        // also propagates to new staged files, so payloads do not inherit a
        // shared destination's write permissions while being verified.
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;OICI;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr)) {
            return std::unexpected(ioError("Could not protect extraction staging permissions", path));
        }
        return {};
    }
};

} // namespace

struct OutputFile::Impl {
    Handle handle;
    std::filesystem::path path;
};

OutputFile::OutputFile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OutputFile::~OutputFile() = default;

Result<void> OutputFile::write(std::span<const std::byte> data, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)())) {
        return std::unexpected(ioError("Output offset exceeds platform limits", impl_->path,
                                       ERROR_FILE_TOO_LARGE));
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(impl_->handle.value, position, nullptr, FILE_BEGIN)) {
        return std::unexpected(ioError("Could not seek extraction output", impl_->path));
    }
    while (!data.empty()) {
        const auto chunk = static_cast<DWORD>((std::min)(data.size(),
                                         static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(impl_->handle.value, data.data(), chunk, &written, nullptr) || written == 0) {
            return std::unexpected(ioError("Could not write extraction output", impl_->path));
        }
        data = data.subspan(written);
    }
    return {};
}

Result<void> OutputFile::finish(std::uint64_t logicalSize,
                               std::optional<std::chrono::system_clock::time_point> modified) {
    if (logicalSize > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)())) {
        return std::unexpected(ioError("Output size exceeds platform limits", impl_->path,
                                       ERROR_FILE_TOO_LARGE));
    }
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(logicalSize);
    if (!SetFilePointerEx(impl_->handle.value, end, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(impl_->handle.value)) {
        return std::unexpected(ioError("Could not finish extraction output", impl_->path));
    }
    if (modified) {
        constexpr std::int64_t epoch = 116444736000000000LL;
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      modified->time_since_epoch()).count();
        const auto ticks = nanoseconds / 100 + epoch;
        if (ticks >= 0) {
            const auto unsignedTicks = static_cast<std::uint64_t>(ticks);
            FILETIME time{static_cast<DWORD>(unsignedTicks), static_cast<DWORD>(unsignedTicks >> 32U)};
            if (!SetFileTime(impl_->handle.value, nullptr, nullptr, &time)) {
                return std::unexpected(ioError("Could not preserve output timestamp", impl_->path));
            }
        }
    }
    if (!FlushFileBuffers(impl_->handle.value)) {
        return std::unexpected(ioError("Could not flush extraction output", impl_->path));
    }
    const auto raw = std::exchange(impl_->handle.value, INVALID_HANDLE_VALUE);
    if (!CloseHandle(raw)) {
        return std::unexpected(ioError("Could not close extraction output", impl_->path));
    }
    return {};
}

struct SafeOutput::Impl {
    struct Change {
        std::filesystem::path relative;
        std::string backup;
        bool installed = false;
    };
    std::filesystem::path rootPath;
    std::filesystem::path stagePath;
    Handle stageLock;
    std::vector<Handle> ancestorLocks;
    std::vector<std::filesystem::path> createdDirectories;
    std::vector<std::filesystem::path> createdRootDirectories;
    std::vector<std::string> stageNames;
    std::vector<Change> changes;
    std::uint64_t serial = 0;
    bool completed = false;

    Result<std::vector<Handle>> parents(const std::filesystem::path& relative, bool create) {
        std::vector<Handle> locks;
        auto current = rootPath;
        auto accumulated = std::filesystem::path{};
        for (const auto& part : relative.parent_path()) {
            current /= part;
            accumulated /= part;
            if (create && CreateDirectoryW(current.c_str(), nullptr)) {
                createdDirectories.push_back(accumulated);
            } else if (create && GetLastError() != ERROR_ALREADY_EXISTS) {
                return std::unexpected(ioError("Could not create extraction directory", current));
            }
            auto locked = lockDirectory(current);
            if (!locked) return std::unexpected(std::move(locked.error()));
            locks.push_back(std::move(*locked));
        }
        return locks;
    }

    Result<void> cleanupStage() {
        std::optional<ArchiveError> first;
        for (const auto& name : stageNames) {
            const auto path = stagePath / pathFromUtf8(name);
            if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && !first) {
                first = ioError("Could not remove extraction staging file", path);
            }
        }
        stageLock = {};
        if (!stagePath.empty() && !RemoveDirectoryW(stagePath.c_str()) &&
            GetLastError() != ERROR_PATH_NOT_FOUND && !first) {
            first = ioError("Could not remove extraction staging directory", stagePath);
        }
        if (first) return std::unexpected(std::move(*first));
        return {};
    }
};

SafeOutput::SafeOutput(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SafeOutput::~SafeOutput() {
    if (!impl_->completed) {
        try { (void)rollback(); }
        catch (...) { std::fputs("AnAnArchiver: extraction rollback could not complete\n", stderr); }
    }
}
const std::filesystem::path& SafeOutput::root() const noexcept { return impl_->rootPath; }

Result<std::unique_ptr<SafeOutput>> SafeOutput::open(const std::filesystem::path& root) {
    if (root.empty()) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsafePath,
                                            "Extraction destination is empty", {}, root});
    }
    auto impl = std::make_unique<Impl>();
    impl->rootPath = extended(root);
    auto plainText = impl->rootPath.native();
    if (plainText.starts_with(L"\\\\?\\UNC\\")) plainText = L"\\\\" + plainText.substr(8);
    else if (plainText.starts_with(L"\\\\?\\")) plainText = plainText.substr(4);
    const std::filesystem::path plainRoot{plainText};
    auto current = extended(plainRoot.root_path());
    auto rootLock = lockDirectory(current);
    if (!rootLock) return std::unexpected(std::move(rootLock.error()));
    impl->ancestorLocks.push_back(std::move(*rootLock));
    // Construct the owning transaction before creating directories, so every
    // early return also removes newly-created empty root directories.
    auto result = std::make_unique<SafeOutput>(std::move(impl));
    for (const auto& part : plainRoot.relative_path()) {
        current /= part;
        if (CreateDirectoryW(current.c_str(), nullptr)) {
            result->impl_->createdRootDirectories.push_back(current);
        } else if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return std::unexpected(ioError("Could not create extraction root", current));
        }
        auto locked = lockDirectory(current);
        if (!locked) return std::unexpected(std::move(locked.error()));
        result->impl_->ancestorLocks.push_back(std::move(*locked));
    }
    PrivateDirectorySecurity security;
    auto secured = security.initialize(root);
    if (!secured) return std::unexpected(std::move(secured.error()));
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = security.descriptor;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        const auto path = result->impl_->rootPath / pathFromUtf8(uniqueName());
        if (!CreateDirectoryW(path.c_str(), &attributes)) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) continue;
            return std::unexpected(ioError("Could not create extraction staging directory", path));
        }
        result->impl_->stagePath = path;
        auto lock = lockDirectory(path);
        if (!lock) return std::unexpected(std::move(lock.error()));
        result->impl_->stageLock = std::move(*lock);
        return result;
    }
    return std::unexpected(ioError("Could not reserve extraction staging directory", root,
                                   ERROR_ALREADY_EXISTS));
}

Result<OutputStatus> SafeOutput::status(const std::filesystem::path& relative) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return std::unexpected(std::move(authorized.error()));
    if (relative.empty()) return OutputStatus{OutputKind::Directory, {}};
    auto parents = impl_->parents(relative, false);
    if (!parents) {
        const auto& error = parents.error();
        if (error.code == ArchiveErrorCode::FileNotFound) {
            return OutputStatus{};
        }
        return std::unexpected(std::move(parents.error()));
    }
    const auto path = impl_->rootPath / relative;
    Handle handle{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
            return OutputStatus{};
        }
        return std::unexpected(ioError("Could not inspect extraction output", path));
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.value, &info)) {
        return std::unexpected(ioError("Could not inspect extraction output", path));
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return std::unexpected(unsafe(path));
    }
    return OutputStatus{(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                            ? OutputKind::Directory : OutputKind::RegularFile,
                        modifiedTime(info.ftLastWriteTime)};
}

Result<void> SafeOutput::directory(const std::filesystem::path& relative) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return authorized;
    if (relative.empty()) return {};
    auto parents = impl_->parents(relative / L"child", true);
    if (!parents) return std::unexpected(std::move(parents.error()));
    return {};
}

Result<StagedFile> SafeOutput::stage() {
    const auto name = "data-" + std::to_string(++impl_->serial);
    impl_->stageNames.push_back(name);
    const auto path = impl_->stagePath / pathFromUtf8(name);
    Handle handle{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle) return std::unexpected(ioError("Could not create extraction staging file", path));
    auto file = std::make_unique<OutputFile::Impl>();
    file->handle = std::move(handle);
    file->path = path;
    return StagedFile{name, std::make_unique<OutputFile>(std::move(file))};
}

Result<void> SafeOutput::install(std::string_view stagedName,
                                const std::filesystem::path& relative, bool replace) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return authorized;
    auto parents = impl_->parents(relative, true);
    if (!parents) return std::unexpected(std::move(parents.error()));
    auto existing = status(relative);
    if (!existing) return std::unexpected(std::move(existing.error()));
    if (existing->kind == OutputKind::Directory) return std::unexpected(unsafe(impl_->rootPath / relative));
    if (existing->kind != OutputKind::Missing && !replace) {
        return std::unexpected(ioError("Extraction output already exists", impl_->rootPath / relative,
                                       ERROR_ALREADY_EXISTS));
    }
    Impl::Change change{relative, {}, false};
    if (existing->kind != OutputKind::Missing) {
        change.backup = "backup-" + std::to_string(++impl_->serial);
        impl_->stageNames.push_back(change.backup);
    }
    impl_->changes.push_back(std::move(change));
    auto& recorded = impl_->changes.back();
    const auto output = impl_->rootPath / relative;
    if (!recorded.backup.empty() &&
        !MoveFileExW(output.c_str(), (impl_->stagePath / pathFromUtf8(recorded.backup)).c_str(), 0)) {
        recorded.backup.clear();
        return std::unexpected(ioError("Could not preserve existing extraction output", output));
    }
    if (!stagedName.empty()) {
        if (!MoveFileExW((impl_->stagePath / pathFromUtf8(stagedName)).c_str(), output.c_str(),
                         MOVEFILE_WRITE_THROUGH)) {
            return std::unexpected(ioError("Could not install extraction output", output));
        }
        recorded.installed = true;
    }
    return {};
}

Result<void> SafeOutput::link(const std::filesystem::path& relative,
                             const std::filesystem::path& target,
                             bool hardlink, bool targetDirectory, bool replace) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return authorized;
    auto authorizedTarget = validateOutputPath(target);
    if (!authorizedTarget) return authorizedTarget;
    auto targetStatus = status(target);
    if (!targetStatus) return std::unexpected(std::move(targetStatus.error()));
    if (hardlink && targetStatus->kind != OutputKind::RegularFile) {
        return std::unexpected(unsafe(impl_->rootPath / target));
    }
    // A contained dangling symlink needs no source handle. A hardlink does:
    // pin its final source component against replacement/reparse retargeting.
    std::vector<Handle> targetParents;
    Handle targetHandle;
    if (targetStatus->kind != OutputKind::Missing) {
        auto lockedParents = impl_->parents(target, false);
        if (!lockedParents) return std::unexpected(std::move(lockedParents.error()));
        targetParents = std::move(*lockedParents);
        targetHandle = Handle{CreateFileW((impl_->rootPath / target).c_str(), FILE_READ_ATTRIBUTES,
                                         FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!targetHandle) return std::unexpected(ioError("Could not secure archive link target", impl_->rootPath / target));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(targetHandle.value, &info)) {
            return std::unexpected(ioError("Could not inspect archive link target", impl_->rootPath / target));
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (hardlink && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) {
            return std::unexpected(unsafe(impl_->rootPath / target));
        }
    }
    auto parents = impl_->parents(relative, true);
    if (!parents) return std::unexpected(std::move(parents.error()));
    auto prepared = install({}, relative, replace);
    if (!prepared) return prepared;
    const auto output = impl_->rootPath / relative;
    bool created = false;
    if (hardlink) {
        created = CreateHardLinkW(output.c_str(), (impl_->rootPath / target).c_str(), nullptr) != 0;
    } else {
        auto relativeTarget = target.lexically_relative(relative.parent_path());
        if (relativeTarget.empty()) relativeTarget = L".";
        const DWORD flags = (targetDirectory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0U) |
                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        created = CreateSymbolicLinkW(output.c_str(), relativeTarget.c_str(), flags) != 0;
        if (!created && GetLastError() == ERROR_INVALID_PARAMETER) {
            created = CreateSymbolicLinkW(output.c_str(), relativeTarget.c_str(),
                                          flags & ~SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
        }
    }
    if (!created) {
        return std::unexpected(ioError("Could not create archive link; check platform link permissions", output));
    }
    impl_->changes.back().installed = true;
    return {};
}

Result<void> SafeOutput::finish() {
    // After success backups may be deleted. Mark complete before cleanup: a
    // cleanup error must not roll back outputs whose backups were removed.
    impl_->completed = true;
    auto cleaned = impl_->cleanupStage();
    if (!cleaned) {
        cleaned.error().code = ArchiveErrorCode::CleanupFailed;
        cleaned.error().message = "Extraction output was committed, but temporary cleanup failed";
    }
    return cleaned;
}

Result<void> SafeOutput::rollback() {
    std::optional<ArchiveError> first;
    for (auto item = impl_->changes.rbegin(); item != impl_->changes.rend(); ++item) {
        auto parents = impl_->parents(item->relative, false);
        if (!parents) {
            if (!first) first = std::move(parents.error());
            continue;
        }
        const auto output = impl_->rootPath / item->relative;
        if (item->installed) {
            const DWORD attrs = GetFileAttributesW(output.c_str());
            const bool removed = attrs != INVALID_FILE_ATTRIBUTES &&
                                 (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0
                                     ? RemoveDirectoryW(output.c_str()) != 0
                                     : DeleteFileW(output.c_str()) != 0;
            if (removed || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
                item->installed = false;
            } else if (!first) first = ioError("Could not roll back extraction output", output);
        }
        if (!item->backup.empty() && !item->installed) {
            if (MoveFileExW((impl_->stagePath / pathFromUtf8(item->backup)).c_str(), output.c_str(), 0)) {
                item->backup.clear();
            } else if (!first) first = ioError("Could not restore original extraction output", output);
        }
    }
    // Preserve recovery backups whenever rollback failed instead of deleting
    // the user's last remaining copy. Return their staging path in the error.
    if (first) {
        first->message += "; recovery files were retained in the staging directory";
        first->path = impl_->stagePath;
        return std::unexpected(std::move(*first));
    }
    auto cleaned = impl_->cleanupStage();
    if (!cleaned) return cleaned;
    for (auto item = impl_->createdDirectories.rbegin(); item != impl_->createdDirectories.rend(); ++item) {
        if (!RemoveDirectoryW((impl_->rootPath / *item).c_str()) &&
            GetLastError() != ERROR_PATH_NOT_FOUND && !first) {
            first = ioError("Could not remove empty extraction directory", impl_->rootPath / *item);
        }
    }
    impl_->ancestorLocks.clear();
    for (auto item = impl_->createdRootDirectories.rbegin(); item != impl_->createdRootDirectories.rend(); ++item) {
        if (!RemoveDirectoryW(item->c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND && !first) {
            first = ioError("Could not remove empty extraction root", *item);
        }
    }
    impl_->completed = true;
    if (first) return std::unexpected(std::move(*first));
    return {};
}

} // namespace ananarchiver::detail
