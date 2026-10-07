#include "../../core/filesystem/compression_io.h"
#include "../../core/archive/archive_unicode_scope.h"

#include <fcntl.h>
#include <dirent.h>
#include <locale.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <optional>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

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
    return {ArchiveErrorCode::UnsafePath, "Compression path contains a link or unsafe output", {}, path};
}

Result<Descriptor> directory(const std::filesystem::path& absolute) {
    Descriptor current{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!current) return std::unexpected(ioError("Could not secure compression directory", absolute));
    for (const auto& part : absolute.relative_path()) {
        Descriptor next{openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (!next) return std::unexpected(ioError("Could not secure compression directory", absolute));
        current = std::move(next);
    }
    return current;
}

CompressionPathInfo metadata(const struct stat& value) {
#if defined(__APPLE__)
    const auto seconds = value.st_mtimespec.tv_sec;
    const auto nanos = value.st_mtimespec.tv_nsec;
#else
    const auto seconds = value.st_mtim.tv_sec;
    const auto nanos = value.st_mtim.tv_nsec;
#endif
    return {S_ISDIR(value.st_mode), S_ISREG(value.st_mode) ? static_cast<std::uint64_t>(value.st_size) : 0,
            static_cast<std::uint32_t>(value.st_mode & 07777),
            std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::seconds{seconds} + std::chrono::nanoseconds{nanos})},
            {static_cast<std::uint64_t>(value.st_dev), static_cast<std::uint64_t>(value.st_ino)}};
}

bool matches(const CompressionPathInfo& first, const CompressionPathInfo& second) {
    return first.directory == second.directory && first.size == second.size &&
           first.modified == second.modified && first.identity == second.identity &&
           first.permissions == second.permissions;
}

Result<void> regularOrDirectory(const struct stat& value, const std::filesystem::path& path) {
    if (S_ISLNK(value.st_mode)) return std::unexpected(unsafe(path));
    if (!S_ISREG(value.st_mode) && !S_ISDIR(value.st_mode)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
                                            "Compression input must be a regular file or directory", {}, path});
    }
    return {};
}

std::string uniqueName() {
    static std::atomic<std::uint64_t> serial{0};
    std::random_device entropy;
    return ".ananarchiver-create-" + std::to_string(getpid()) + "-" +
           std::to_string(entropy()) + "-" + std::to_string(++serial);
}

} // namespace

struct CompressionDirectoryCursor::Impl {
    DIR* stream = nullptr;
    std::filesystem::path directory;
    ~Impl() { if (stream != nullptr) closedir(stream); }
};

CompressionDirectoryCursor::CompressionDirectoryCursor(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CompressionDirectoryCursor::~CompressionDirectoryCursor() = default;

Result<std::unique_ptr<CompressionDirectoryCursor>> CompressionDirectoryCursor::open(
        const std::filesystem::path& path) {
    auto descriptor = directory(path);
    if (!descriptor) return std::unexpected(std::move(descriptor.error()));
    auto impl = std::make_unique<Impl>();
    impl->directory = path;
    impl->stream = fdopendir(descriptor->value);
    if (impl->stream == nullptr) return std::unexpected(ioError("Could not enumerate compression directory", path));
    descriptor->value = -1; // fdopendir owns the descriptor after success.
    return std::make_unique<CompressionDirectoryCursor>(std::move(impl));
}

Result<std::optional<std::filesystem::path>> CompressionDirectoryCursor::next() {
    while (true) {
        errno = 0;
        const auto* item = readdir(impl_->stream);
        if (item == nullptr) {
            if (errno != 0) return std::unexpected(ioError("Could not continue compression directory enumeration", impl_->directory));
            return std::optional<std::filesystem::path>{};
        }
        const std::string_view name{item->d_name};
        if (name == "." || name == "..") continue;
        return std::optional{impl_->directory / std::filesystem::path{name}};
    }
}

struct ArchiveUnicodeScope::Impl {
    locale_t locale = static_cast<locale_t>(nullptr);
    locale_t previous = static_cast<locale_t>(nullptr);
    ~Impl() {
        if (previous != static_cast<locale_t>(nullptr)) uselocale(previous);
        if (locale != static_cast<locale_t>(nullptr)) freelocale(locale);
    }
};

ArchiveUnicodeScope::ArchiveUnicodeScope(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ArchiveUnicodeScope::~ArchiveUnicodeScope() = default;

Result<std::unique_ptr<ArchiveUnicodeScope>> ArchiveUnicodeScope::enter() {
    auto impl = std::make_unique<Impl>();
    for (const auto* name : {"C.UTF-8", "C.utf8", "en_US.UTF-8", "UTF-8"}) {
        impl->locale = newlocale(LC_CTYPE_MASK, name, static_cast<locale_t>(nullptr));
        if (impl->locale != static_cast<locale_t>(nullptr)) break;
    }
    if (impl->locale == static_cast<locale_t>(nullptr)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
                                            "A UTF-8 worker locale is unavailable for archive pathname conversion", {}, {}});
    }
    auto result = std::make_unique<ArchiveUnicodeScope>(std::move(impl));
    result->impl_->previous = uselocale(result->impl_->locale);
    if (result->impl_->previous == static_cast<locale_t>(nullptr)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
                                            "Could not enter the UTF-8 archive worker locale", {}, {}});
    }
    return result;
}

std::filesystem::path compressionNativePath(const std::filesystem::path& path) { return path; }
std::filesystem::path compressionParentPath(const std::filesystem::path& path) { return path.parent_path(); }
bool compressionFilesystemRoot(const std::filesystem::path& path) { return path == path.root_path(); }
std::filesystem::path compressionRelativePath(const std::filesystem::path& path,
                                              const std::filesystem::path& root) {
    return path.lexically_relative(root);
}
Result<std::filesystem::path> canonicalCompressionInputPath(const std::filesystem::path& path) {
    // The caller resolved ancestor aliases; native inspection then validates
    // the final component without following it.
    // POSIX filenames are case-sensitive and hardlink names remain distinct.
    return path;
}

Result<std::filesystem::path> canonicalCompressionParentPath(const std::filesystem::path& path) {
    std::error_code native;
    const auto resolved = std::filesystem::canonical(path, native);
    if (native) return std::unexpected(ioError("Could not resolve compression parent", path, native.value()));
    const auto status = std::filesystem::status(resolved, native);
    if (native) return std::unexpected(ioError("Could not inspect compression parent", path, native.value()));
    if (!std::filesystem::is_directory(status)) return std::unexpected(unsafe(path));
    return resolved;
}

bool compressionPathContains(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto current = path.begin();
    for (const auto& part : root) {
        if (current == path.end() || *current != part) return false;
        ++current;
    }
    return true;
}

Result<CompressionPathInfo> inspectCompressionPath(const std::filesystem::path& path) {
    auto parent = directory(path.parent_path());
    if (!parent) return std::unexpected(std::move(parent.error()));
    struct stat value{};
    if (fstatat(parent->value, path.filename().c_str(), &value, AT_SYMLINK_NOFOLLOW) < 0) {
        return std::unexpected(ioError("Could not inspect compression input", path));
    }
    auto valid = regularOrDirectory(value, path);
    if (!valid) return std::unexpected(std::move(valid.error()));
    return metadata(value);
}

struct CompressionInputFile::Impl {
    Descriptor file;
    std::filesystem::path path;
    CompressionPathInfo scanned;
};

CompressionInputFile::CompressionInputFile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CompressionInputFile::~CompressionInputFile() = default;

Result<std::unique_ptr<CompressionInputFile>> CompressionInputFile::open(
        const std::filesystem::path& path, const CompressionPathInfo& scanned) {
    auto parent = directory(path.parent_path());
    if (!parent) return std::unexpected(std::move(parent.error()));
    // NONBLOCK prevents a race replacing a regular file with a FIFO from
    // turning an archive operation into an unbounded wait.
    Descriptor file{openat(parent->value, path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    if (!file) return std::unexpected(ioError("Could not open compression input", path));
    struct stat value{};
    if (fstat(file.value, &value) < 0) return std::unexpected(ioError("Could not inspect opened compression input", path));
    auto valid = regularOrDirectory(value, path);
    if (!valid) return std::unexpected(std::move(valid.error()));
    if (!S_ISREG(value.st_mode) || !matches(scanned, metadata(value))) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Compression input changed after scanning", {}, path});
    }
    auto impl = std::make_unique<Impl>();
    impl->file = std::move(file);
    impl->path = path;
    impl->scanned = scanned;
    return std::make_unique<CompressionInputFile>(std::move(impl));
}

Result<std::size_t> CompressionInputFile::read(std::span<std::byte> buffer) {
    const auto count = (std::min)(buffer.size(), static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)()));
    ssize_t read = -1;
    do { read = ::read(impl_->file.value, buffer.data(), count); } while (read < 0 && errno == EINTR);
    if (read < 0) return std::unexpected(ioError("Could not read compression input", impl_->path));
    return static_cast<std::size_t>(read);
}

Result<void> CompressionInputFile::verifyUnchanged() {
    struct stat value{};
    if (fstat(impl_->file.value, &value) < 0) {
        return std::unexpected(ioError("Could not recheck compression input", impl_->path));
    }
    if (!matches(impl_->scanned, metadata(value))) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                                            "Compression input changed while being archived", {}, impl_->path});
    }
    return {};
}

struct AtomicArchiveOutput::Impl {
    Descriptor parent;
    Descriptor stage;
    Descriptor file;
    std::filesystem::path destination;
    std::string stageName;
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
            if (!result) std::fputs("AnAnArchiver: compression staging cleanup failed\n", stderr);
        } catch (...) { std::fputs("AnAnArchiver: compression staging cleanup failed\n", stderr); }
    }
}

Result<std::unique_ptr<AtomicArchiveOutput>> AtomicArchiveOutput::open(
        const std::filesystem::path& destination, bool overwrite) {
    auto parent = directory(destination.parent_path());
    if (!parent) return std::unexpected(std::move(parent.error()));
    struct stat existing{};
    if (fstatat(parent->value, destination.filename().c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(existing.st_mode)) return std::unexpected(unsafe(destination));
        if (!overwrite) return std::unexpected(ioError("Archive destination already exists", destination, EEXIST));
    } else if (errno != ENOENT) {
        return std::unexpected(ioError("Could not inspect archive destination", destination));
    }
    auto impl = std::make_unique<Impl>();
    impl->parent = std::move(*parent);
    impl->destination = destination;
    impl->overwrite = overwrite;
    auto output = std::make_unique<AtomicArchiveOutput>(std::move(impl));
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        output->impl_->stageName = uniqueName();
        if (mkdirat(output->impl_->parent.value, output->impl_->stageName.c_str(), 0700) < 0) {
            if (errno == EEXIST) continue;
            return std::unexpected(ioError("Could not reserve archive staging directory", destination));
        }
        output->impl_->ownsStage = true;
        output->impl_->stage = Descriptor{openat(output->impl_->parent.value, output->impl_->stageName.c_str(),
                                                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (!output->impl_->stage) return std::unexpected(ioError("Could not secure archive staging directory", destination));
        output->impl_->file = Descriptor{openat(output->impl_->stage.value, "archive.tmp",
                                                O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (!output->impl_->file) return std::unexpected(ioError("Could not create temporary archive", destination));
        return output;
    }
    return std::unexpected(ioError("Could not reserve archive staging directory", destination, EEXIST));
}

Result<void> AtomicArchiveOutput::write(std::span<const std::byte> data) {
    while (!data.empty()) {
        const auto count = (std::min)(data.size(), static_cast<std::size_t>((std::numeric_limits<ssize_t>::max)()));
        const auto written = ::write(impl_->file.value, data.data(), count);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return std::unexpected(ioError("Could not write temporary archive", impl_->destination));
        data = data.subspan(static_cast<std::size_t>(written));
    }
    return {};
}

Result<void> AtomicArchiveOutput::finish() {
    if (fsync(impl_->file.value) < 0) return std::unexpected(ioError("Could not flush completed archive", impl_->destination));
    const int file = std::exchange(impl_->file.value, -1);
    if (::close(file) < 0) return std::unexpected(ioError("Could not close completed archive", impl_->destination));
    return {};
}

Result<void> AtomicArchiveOutput::commit(CancellationToken cancellation) {
    struct stat existing{};
    if (fstatat(impl_->parent.value, impl_->destination.filename().c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(existing.st_mode)) return std::unexpected(unsafe(impl_->destination));
        if (!impl_->overwrite) return std::unexpected(ioError("Archive destination already exists", impl_->destination, EEXIST));
    } else if (errno != ENOENT) {
        return std::unexpected(ioError("Could not inspect archive destination", impl_->destination));
    }
    if (cancellation.stop_requested()) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled,
                                            "Archive creation cancelled", {}, impl_->destination});
    }
    if (impl_->overwrite) {
        if (renameat(impl_->stage.value, "archive.tmp", impl_->parent.value,
                     impl_->destination.filename().c_str()) < 0) {
            return std::unexpected(ioError("Could not atomically replace archive destination", impl_->destination));
        }
    } else {
        // A portable atomic no-replace operation. Both names share one
        // filesystem; cleanup releases the private staging name afterwards.
        if (linkat(impl_->stage.value, "archive.tmp", impl_->parent.value,
                   impl_->destination.filename().c_str(), 0) < 0) {
            return std::unexpected(ioError("Could not atomically install archive destination", impl_->destination));
        }
    }
    impl_->committed = true;
    if (fsync(impl_->parent.value) < 0) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::CleanupFailed,
                                            "Archive was committed, but directory durability could not be confirmed",
                                            std::error_code{errno, std::generic_category()}.message(), impl_->destination});
    }
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
    if (impl_->stage && unlinkat(impl_->stage.value, "archive.tmp", 0) < 0 && errno != ENOENT) {
        return std::unexpected(ioError("Could not clean temporary archive",
                                       impl_->destination.parent_path() / impl_->stageName / "archive.tmp"));
    }
    impl_->stage = {};
    if (impl_->ownsStage && unlinkat(impl_->parent.value, impl_->stageName.c_str(), AT_REMOVEDIR) < 0 && errno != ENOENT) {
        return std::unexpected(ioError("Could not clean archive staging directory",
                                       impl_->destination.parent_path() / impl_->stageName));
    }
    impl_->cleaned = true;
    return {};
}

bool AtomicArchiveOutput::committed() const noexcept { return impl_->committed; }

} // namespace ananarchiver::detail
