#include "../../core/filesystem/safe_output.h"
#include "ananarchiver/unicode.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <random>
#include <system_error>
#include <utility>
#include <vector>

namespace ananarchiver::detail {
namespace {

struct Descriptor {
    int value = -1;
    Descriptor() = default;
    explicit Descriptor(int descriptor) : value(descriptor) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(Descriptor&& other) noexcept : value(std::exchange(other.value, -1)) {}
    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this != &other) {
            if (value >= 0) ::close(value);
            value = std::exchange(other.value, -1);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return value >= 0; }
};

ArchiveError ioError(std::string message, const std::filesystem::path& path, int code = errno) {
    ArchiveErrorCode kind = ArchiveErrorCode::IoError;
    if (code == EACCES || code == EPERM) kind = ArchiveErrorCode::PermissionDenied;
    else if (code == ENOSPC || code == EDQUOT) kind = ArchiveErrorCode::DiskFull;
    else if (code == EEXIST) kind = ArchiveErrorCode::OutputExists;
    else if (code == ELOOP || code == ENOTDIR) kind = ArchiveErrorCode::UnsafePath;
    else if (code == ENOENT) kind = ArchiveErrorCode::FileNotFound;
    return {kind, std::move(message), std::error_code{code, std::generic_category()}.message(), path};
}

ArchiveError unsafe(const std::filesystem::path& path) {
    return {ArchiveErrorCode::UnsafePath,
            "Extraction path contains a link or special file", {}, path};
}

std::string uniqueName() {
    static std::atomic<std::uint64_t> serial{0};
    std::random_device entropy;
    return ".ananarchiver-" + std::to_string(getpid()) + "-" +
           std::to_string(entropy()) + "-" + std::to_string(++serial);
}

} // namespace

struct OutputFile::Impl {
    Descriptor descriptor;
    std::filesystem::path path;
};

OutputFile::OutputFile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OutputFile::~OutputFile() = default;

Result<void> OutputFile::write(std::span<const std::byte> data, std::uint64_t offset) {
    while (!data.empty()) {
        if (offset > static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)())) {
            return std::unexpected(ioError("Output offset exceeds platform limits", impl_->path, EFBIG));
        }
        const auto count = static_cast<std::size_t>((std::min)(data.size(),
                               static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)())));
        const auto written = pwrite(impl_->descriptor.value, data.data(), count, static_cast<off_t>(offset));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return std::unexpected(ioError("Could not write extraction output", impl_->path));
        offset += static_cast<std::uint64_t>(written);
        data = data.subspan(static_cast<std::size_t>(written));
    }
    return {};
}

Result<void> OutputFile::finish(std::uint64_t logicalSize,
                               std::optional<std::chrono::system_clock::time_point> modified) {
    if (logicalSize > static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)())) {
        return std::unexpected(ioError("Output size exceeds platform limits", impl_->path, EFBIG));
    }
    if (ftruncate(impl_->descriptor.value, static_cast<off_t>(logicalSize)) < 0) {
        return std::unexpected(ioError("Could not finish extraction output", impl_->path));
    }
    if (modified) {
        const auto seconds = std::chrono::floor<std::chrono::seconds>(modified->time_since_epoch());
        const auto remainder = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   modified->time_since_epoch() - seconds);
        const timespec times[2]{{0, UTIME_OMIT},
                              {static_cast<time_t>(seconds.count()), static_cast<long>(remainder.count())}};
        if (futimens(impl_->descriptor.value, times) < 0) {
            return std::unexpected(ioError("Could not preserve output timestamp", impl_->path));
        }
    }
    if (fsync(impl_->descriptor.value) < 0) {
        return std::unexpected(ioError("Could not flush extraction output", impl_->path));
    }
    const auto descriptor = std::exchange(impl_->descriptor.value, -1);
    if (::close(descriptor) < 0) {
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
    std::string stageName;
    Descriptor rootDescriptor;
    Descriptor stageDescriptor;
    std::vector<std::filesystem::path> createdDirectories;
    std::vector<std::filesystem::path> createdRootDirectories;
    std::vector<std::string> stageNames;
    std::vector<Change> changes;
    std::uint64_t serial = 0;
    bool completed = false;

    Result<Descriptor> parent(const std::filesystem::path& relative, bool create) {
        Descriptor directory{dup(rootDescriptor.value)};
        if (!directory) return std::unexpected(ioError("Could not retain extraction root", rootPath));
        std::filesystem::path accumulated;
        for (const auto& part : relative.parent_path()) {
            accumulated /= part;
            if (create && mkdirat(directory.value, part.c_str(), 0700) == 0) {
                createdDirectories.push_back(accumulated);
            } else if (create && errno != EEXIST) {
                return std::unexpected(ioError("Could not create extraction directory", rootPath / accumulated));
            }
            Descriptor next{openat(directory.value, part.c_str(),
                                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            if (!next) return std::unexpected(ioError("Could not secure extraction directory", rootPath / accumulated));
            directory = std::move(next);
        }
        return directory;
    }

    Result<void> cleanupStage() {
        std::optional<ArchiveError> first;
        for (const auto& name : stageNames) {
            if (unlinkat(stageDescriptor.value, name.c_str(), 0) < 0 && errno != ENOENT && !first) {
                first = ioError("Could not remove extraction staging file", rootPath / stageName / name);
            }
        }
        stageDescriptor = {};
        if (!stageName.empty() && unlinkat(rootDescriptor.value, stageName.c_str(), AT_REMOVEDIR) < 0 &&
            errno != ENOENT && !first) {
            first = ioError("Could not remove extraction staging directory", rootPath / stageName);
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
    auto absolute = std::filesystem::absolute(root).lexically_normal();
    while (absolute != absolute.root_path() && absolute.filename().empty()) {
        absolute = absolute.parent_path();
    }
    std::error_code statusError;
    const auto selectedStatus = std::filesystem::symlink_status(absolute, statusError);
    if (std::filesystem::is_symlink(selectedStatus)) {
        return std::unexpected(unsafe(absolute));
    }
    if (statusError && statusError != std::errc::no_such_file_or_directory) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Could not inspect extraction root", statusError.message(), absolute});
    }
    // A user-selected destination authorizes its resolved parent location.
    // Resolve OS/user aliases before entering the no-follow descriptor walk
    // (e.g. macOS's standard temporary-directory aliases), while rejecting the
    // selected final root itself if it is a link. Links inside the secured root
    // remain forbidden in every subsequent operation.
    const auto parent = std::filesystem::weakly_canonical(absolute.parent_path(), statusError);
    if (statusError) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Could not resolve extraction root parent", statusError.message(), absolute});
    }
    impl->rootPath = absolute == absolute.root_path() ? absolute : parent / absolute.filename();
    impl->rootDescriptor = Descriptor{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!impl->rootDescriptor) return std::unexpected(ioError("Could not open filesystem root", root));
    auto output = std::make_unique<SafeOutput>(std::move(impl));
    std::filesystem::path accumulated{"/"};
    for (const auto& part : output->impl_->rootPath.relative_path()) {
        accumulated /= part;
        if (mkdirat(output->impl_->rootDescriptor.value, part.c_str(), 0700) == 0) {
            output->impl_->createdRootDirectories.push_back(accumulated);
        } else if (errno != EEXIST) {
            return std::unexpected(ioError("Could not create extraction root", accumulated));
        }
        Descriptor next{openat(output->impl_->rootDescriptor.value, part.c_str(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (!next) return std::unexpected(ioError("Could not secure extraction root", accumulated));
        output->impl_->rootDescriptor = std::move(next);
    }
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        const auto name = uniqueName();
        if (mkdirat(output->impl_->rootDescriptor.value, name.c_str(), 0700) < 0) {
            if (errno == EEXIST) continue;
            return std::unexpected(ioError("Could not create extraction staging directory", root / name));
        }
        output->impl_->stageName = name;
        output->impl_->stageDescriptor = Descriptor{openat(output->impl_->rootDescriptor.value,
                        name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (!output->impl_->stageDescriptor) {
            return std::unexpected(ioError("Could not secure extraction staging directory", root / name));
        }
        return output;
    }
    return std::unexpected(ioError("Could not reserve extraction staging directory", root, EEXIST));
}

Result<OutputStatus> SafeOutput::status(const std::filesystem::path& relative) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return std::unexpected(std::move(authorized.error()));
    if (relative.empty()) return OutputStatus{OutputKind::Directory, {}};
    auto parent = impl_->parent(relative, false);
    if (!parent) {
        if (parent.error().code == ArchiveErrorCode::FileNotFound) return OutputStatus{};
        return std::unexpected(std::move(parent.error()));
    }
    struct stat info{};
    if (fstatat(parent->value, relative.filename().c_str(), &info, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT) return OutputStatus{};
        return std::unexpected(ioError("Could not inspect extraction output", impl_->rootPath / relative));
    }
    if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) {
        return std::unexpected(unsafe(impl_->rootPath / relative));
    }
    return OutputStatus{S_ISDIR(info.st_mode) ? OutputKind::Directory : OutputKind::RegularFile,
                        std::chrono::system_clock::from_time_t(info.st_mtime)};
}

Result<void> SafeOutput::directory(const std::filesystem::path& relative) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return authorized;
    if (relative.empty()) return {};
    auto parent = impl_->parent(relative / "child", true);
    if (!parent) return std::unexpected(std::move(parent.error()));
    return {};
}

Result<StagedFile> SafeOutput::stage() {
    const auto name = "data-" + std::to_string(++impl_->serial);
    impl_->stageNames.push_back(name);
    Descriptor descriptor{openat(impl_->stageDescriptor.value, name.c_str(),
                                 O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (!descriptor) return std::unexpected(ioError("Could not create extraction staging file",
                                                   impl_->rootPath / impl_->stageName / name));
    auto file = std::make_unique<OutputFile::Impl>();
    file->descriptor = std::move(descriptor);
    file->path = impl_->rootPath / impl_->stageName / name;
    return StagedFile{name, std::make_unique<OutputFile>(std::move(file))};
}

Result<void> SafeOutput::install(std::string_view stagedName,
                                const std::filesystem::path& relative, bool replace) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return authorized;
    auto parent = impl_->parent(relative, true);
    if (!parent) return std::unexpected(std::move(parent.error()));
    auto existing = status(relative);
    if (!existing) return std::unexpected(std::move(existing.error()));
    if (existing->kind == OutputKind::Directory) return std::unexpected(unsafe(impl_->rootPath / relative));
    if (existing->kind != OutputKind::Missing && !replace) {
        return std::unexpected(ioError("Extraction output already exists", impl_->rootPath / relative, EEXIST));
    }
    Impl::Change change{relative, {}, false};
    if (existing->kind != OutputKind::Missing) {
        change.backup = "backup-" + std::to_string(++impl_->serial);
        impl_->stageNames.push_back(change.backup);
    }
    impl_->changes.push_back(std::move(change));
    auto& recorded = impl_->changes.back();
    if (!recorded.backup.empty() && renameat(parent->value, relative.filename().c_str(),
                                           impl_->stageDescriptor.value, recorded.backup.c_str()) < 0) {
        recorded.backup.clear();
        return std::unexpected(ioError("Could not preserve existing extraction output", impl_->rootPath / relative));
    }
    if (!stagedName.empty()) {
        // linkat gives a portable atomic no-replace operation. Both locations
        // are on the extraction root's filesystem; unlink then releases the
        // staging name without changing the installed file contents.
        const std::string name{stagedName};
        if (linkat(impl_->stageDescriptor.value, name.c_str(), parent->value,
                   relative.filename().c_str(), 0) < 0) {
            return std::unexpected(ioError("Could not install extraction output", impl_->rootPath / relative));
        }
        recorded.installed = true;
        if (unlinkat(impl_->stageDescriptor.value, name.c_str(), 0) < 0) {
            return std::unexpected(ioError("Could not release extraction staging file", impl_->rootPath / relative));
        }
    }
    if (fsync(parent->value) < 0) return std::unexpected(ioError("Could not flush extraction directory", impl_->rootPath / relative));
    return {};
}

Result<void> SafeOutput::link(const std::filesystem::path& relative,
                             const std::filesystem::path& target,
                             bool hardlink, bool, bool replace) {
    auto authorized = validateOutputPath(relative);
    if (!authorized) return authorized;
    auto authorizedTarget = validateOutputPath(target);
    if (!authorizedTarget) return authorizedTarget;
    auto targetStatus = status(target);
    if (!targetStatus) return std::unexpected(std::move(targetStatus.error()));
    if (hardlink && targetStatus->kind != OutputKind::RegularFile) {
        return std::unexpected(unsafe(impl_->rootPath / target));
    }
    Descriptor targetParent;
    if (hardlink) {
        auto openedTargetParent = impl_->parent(target, false);
        if (!openedTargetParent) return std::unexpected(std::move(openedTargetParent.error()));
        targetParent = std::move(*openedTargetParent);
    }
    auto parent = impl_->parent(relative, true);
    if (!parent) return std::unexpected(std::move(parent.error()));
    auto prepared = install({}, relative, replace);
    if (!prepared) return prepared;
    int created = -1;
    if (hardlink) {
        created = linkat(targetParent.value, target.filename().c_str(), parent->value,
                         relative.filename().c_str(), 0);
    } else {
        auto relativeTarget = target.lexically_relative(relative.parent_path());
        if (relativeTarget.empty()) relativeTarget = ".";
        created = symlinkat(relativeTarget.c_str(), parent->value, relative.filename().c_str());
    }
    if (created < 0) return std::unexpected(ioError("Could not create archive link", impl_->rootPath / relative));
    impl_->changes.back().installed = true;
    if (hardlink) {
        struct stat info{};
        if (fstatat(parent->value, relative.filename().c_str(), &info, AT_SYMLINK_NOFOLLOW) < 0 ||
            !S_ISREG(info.st_mode)) {
            return std::unexpected(unsafe(impl_->rootPath / relative));
        }
    }
    if (fsync(parent->value) < 0) return std::unexpected(ioError("Could not flush extraction directory", impl_->rootPath / relative));
    return {};
}

Result<void> SafeOutput::finish() {
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
        auto parent = impl_->parent(item->relative, false);
        if (!parent) {
            if (!first) first = std::move(parent.error());
            continue;
        }
        if (item->installed) {
            if (unlinkat(parent->value, item->relative.filename().c_str(), 0) == 0 || errno == ENOENT) {
                item->installed = false;
            } else if (!first) first = ioError("Could not roll back extraction output", impl_->rootPath / item->relative);
        }
        if (!item->backup.empty() && !item->installed) {
            if (renameat(impl_->stageDescriptor.value, item->backup.c_str(),
                         parent->value, item->relative.filename().c_str()) == 0) {
                item->backup.clear();
            } else if (!first) first = ioError("Could not restore original extraction output", impl_->rootPath / item->relative);
        }
    }
    if (first) {
        first->message += "; recovery files were retained in the staging directory";
        first->path = impl_->rootPath / impl_->stageName;
        return std::unexpected(std::move(*first));
    }
    auto cleaned = impl_->cleanupStage();
    if (!cleaned) return cleaned;
    for (auto item = impl_->createdDirectories.rbegin(); item != impl_->createdDirectories.rend(); ++item) {
        auto parent = impl_->parent(*item, false);
        if (!parent) {
            if (!first) first = std::move(parent.error());
            continue;
        }
        if (unlinkat(parent->value, item->filename().c_str(), AT_REMOVEDIR) < 0 && errno != ENOENT && !first) {
            first = ioError("Could not remove empty extraction directory", impl_->rootPath / *item);
        }
    }
    impl_->rootDescriptor = {};
    // These paths were created by us and contain no output after rollback;
    // rmdir never follows a symlink and never recursively removes user data.
    for (auto item = impl_->createdRootDirectories.rbegin(); item != impl_->createdRootDirectories.rend(); ++item) {
        if (::rmdir(item->c_str()) < 0 && errno != ENOENT && !first) {
            first = ioError("Could not remove empty extraction root", *item);
        }
    }
    impl_->completed = true;
    if (first) return std::unexpected(std::move(*first));
    return {};
}

} // namespace ananarchiver::detail
