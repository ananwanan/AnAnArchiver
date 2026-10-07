#include "ananarchiver/libarchive_reader.h"
#include "ananarchiver/libarchive_writer.h"
#include "compression_fixtures.h"
#include "../security/windows_reparse_fixture.h"

#include <archive.h>
#include <archive_entry.h>

#include <clocale>
#include <map>
#include <set>
#include <stop_token>
#include <thread>

#ifndef _WIN32
#include <locale.h>
#include <sys/stat.h>
#endif

namespace {

using namespace compression_tests;

CompressionRequest requestFor(const std::filesystem::path& input,
                              const std::filesystem::path& destination) {
    CompressionRequest request;
    request.inputPaths = {input};
    request.destination = destination;
    return request;
}

std::vector<ArchiveEntry> entriesIn(const std::filesystem::path& archive) {
    auto reader = makeLibArchiveReader();
    succeeded(reader->open(archive), "Open created archive by contents");
    std::vector<ArchiveEntry> entries;
    succeeded(reader->enumerate([&](const ArchiveEntry& entry) {
        entries.push_back(entry);
        return true;
    }), "Enumerate created archive");
    return entries;
}

std::string entryPath(const ArchiveEntry& entry) {
    auto name = entry.path;
    if (entry.type == EntryType::Directory && !name.ends_with('/')) name += '/';
    return name;
}

std::string describeNames(const std::vector<std::string>& names) {
    std::string description;
    constexpr std::size_t maximum = 16;
    for (std::size_t index = 0; index < (std::min)(names.size(), maximum); ++index) {
        if (!description.empty()) description += ", ";
        description += '"' + names[index] + '"';
    }
    if (names.size() > maximum) description += ", ...";
    return '[' + description + ']';
}

class CTypeLocale {
public:
    explicit CTypeLocale(const char* value) : previous_(std::setlocale(LC_CTYPE, nullptr)) {
        check(std::setlocale(LC_CTYPE, value) != nullptr, "Select test process locale");
    }
    ~CTypeLocale() { std::setlocale(LC_CTYPE, previous_.c_str()); }
private:
    std::string previous_;
};

Result<void> createAndCheckLocale(ArchiveWriter& writer, const CompressionRequest& request,
                                 const CompressionProgressCallback& progress = {},
                                 CancellationToken cancellation = {}) {
    const std::string processLocale = std::setlocale(LC_CTYPE, nullptr);
#ifndef _WIN32
    const locale_t threadLocale = uselocale(static_cast<locale_t>(nullptr));
#endif
    auto result = writer.create(request, progress, cancellation);
    check(std::string{std::setlocale(LC_CTYPE, nullptr)} == processLocale,
          "Archive creation does not change the process LC_CTYPE");
#ifndef _WIN32
    check(uselocale(static_cast<locale_t>(nullptr)) == threadLocale,
          "Archive creation restores the exact caller thread locale");
#endif
    return result;
}

void extractTo(const std::filesystem::path& archive, const std::filesystem::path& output) {
    auto reader = makeLibArchiveReader();
    succeeded(reader->open(archive), "Open newly created archive");
    ExtractionRequest request;
    request.destination = output;
    request.overwrite = OverwritePolicy::Overwrite;
    succeeded(reader->extract(request), "Extract and verify created payloads");
}

unsigned int archivedPermissions(const std::filesystem::path& path, std::string_view name) {
    struct ReaderDeleter {
        void operator()(archive* handle) const noexcept { if (handle != nullptr) archive_read_free(handle); }
    };
    std::unique_ptr<archive, ReaderDeleter> reader{archive_read_new()};
    check(reader != nullptr && archive_read_support_filter_all(reader.get()) == ARCHIVE_OK &&
          archive_read_support_format_all(reader.get()) == ARCHIVE_OK,
          "Configure test-only archive header inspection");
#ifdef _WIN32
    const int opened = archive_read_open_filename_w(reader.get(), nativeFixturePath(path).c_str(), 128 * 1024);
#else
    const int opened = archive_read_open_filename(reader.get(), path.c_str(), 128 * 1024);
#endif
    check(opened == ARCHIVE_OK, "Open archive for test-only permission metadata inspection");
    archive_entry* entry = nullptr;
    int status = ARCHIVE_OK;
    while ((status = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const auto* pathname = archive_entry_pathname_utf8(entry);
        if (pathname != nullptr && std::string_view{pathname} == name)
            return static_cast<unsigned int>(archive_entry_perm(entry)) & 0777U;
        check(archive_read_data_skip(reader.get()) == ARCHIVE_OK, "Skip unrelated archive header payload");
    }
    check(status == ARCHIVE_EOF, "Read archive permission metadata without backend error");
    throw std::runtime_error("Requested permission metadata entry was not found");
}

void roundTripTree(ArchiveWriter& writer, const CompressionRequest& request,
                   const std::filesystem::path& input, const std::filesystem::path& output) {
    succeeded(writer.create(request), "Create " + pathToUtf8(request.destination.filename()));
    auto expected = treeIn(input);
    const auto basename = pathToUtf8(input.filename());
    for (auto& name : expected) name = basename + '/' + name;
    expected.push_back(basename + '/');
    std::ranges::sort(expected);
    std::vector<std::string> actual;
    for (const auto& entry : entriesIn(request.destination)) actual.push_back(entryPath(entry));
    std::ranges::sort(actual);
    check(actual == expected, "Archive has each directory and file once, including empty directories; expected=" +
                              describeNames(expected) + "; actual=" + describeNames(actual));
    extractTo(request.destination, output);
    const auto extracted = treeIn(output);
    check(extracted == expected, "Extracted tree has no extra temporary or synthetic files; expected=" +
                                describeNames(expected) + "; actual=" + describeNames(extracted));
    const auto nativeInput = nativeFixturePath(input);
    for (const auto& file : fixtureTreeEntries(nativeInput)) {
        if (file.directory) continue;
        const auto relative = relativeFixturePath(file.path, nativeInput);
        check(equalFiles(file.path, output / input.filename() / relative),
              "Created archive preserves bytes: " + pathToUtf8(relative));
    }
}

void untouched(const std::filesystem::path& output, std::string_view contents) {
    check(readSmallFile(output) == contents, "Existing destination retains exact original bytes");
    check(childrenIn(output.parent_path()) == std::vector<std::string>{pathToUtf8(output.filename())},
          "Failed operation removes every temporary output sibling");
}

bool canCreateSymlinks(const std::filesystem::path& root) {
    const auto source = root / "symlink-probe-source";
    const auto link = root / "symlink-probe-link";
    std::filesystem::create_directory(source);
    std::error_code error;
    std::filesystem::create_directory_symlink(source, link, error);
    const bool supported = !error;
    std::filesystem::remove(link, error);
    std::filesystem::remove(source, error);
    return supported;
}

void setSourceTimestamp(const std::filesystem::path& path, std::chrono::sys_seconds timestamp) {
#ifdef _WIN32
    const auto ticks = static_cast<std::uint64_t>(timestamp.time_since_epoch().count() * 10'000'000LL +
                                                  116444736000000000LL);
    const FILETIME nativeTimestamp{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32U)};
    const HANDLE file = CreateFileW(nativeFixturePath(path).c_str(), FILE_WRITE_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
    check(file != INVALID_HANDLE_VALUE, "Open timestamp fixture through native Windows API");
    const bool set = SetFileTime(file, nullptr, nullptr, &nativeTimestamp) != 0;
    const auto error = GetLastError();
    CloseHandle(file);
    check(set, "Set source timestamp: Windows error " + std::to_string(error));
#else
    const auto nativeTimestamp = std::filesystem::file_time_type::clock::now() +
                                 (timestamp - std::chrono::system_clock::now());
    std::filesystem::last_write_time(path, nativeTimestamp);
#endif
}

#ifdef _WIN32
bool enableCaseSensitiveDirectory(const std::filesystem::path& path, DWORD& error) {
    const HANDLE directory = CreateFileW(nativeFixturePath(path).c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    // The Windows SDK FILE_INFO_BY_HANDLE_CLASS ABI assigns 23 to
    // FileCaseSensitiveInfo. Its payload is one DWORD Flags with bit 0 enabling
    // case-sensitive names. Use that fixed ABI in this test-only feature probe
    // so older MinGW headers need not declare the newer enum/structure names.
    struct CaseSensitiveInformation { DWORD Flags; } information{1U};
    static_assert(sizeof(CaseSensitiveInformation) == sizeof(DWORD));
    constexpr auto informationClass = static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);
    const bool enabled = SetFileInformationByHandle(directory, informationClass,
        &information, sizeof(information)) != 0;
    error = enabled ? ERROR_SUCCESS : GetLastError();
    CloseHandle(directory);
    return enabled;
}
#endif

} // namespace

int main() {
    try {
        std::cout << std::unitbuf;
#ifdef _WIN32
        check(std::setlocale(LC_CTYPE, ".UTF-8") != nullptr, "Initialize fixture UTF-8 locale");
#elif defined(__APPLE__)
        check(std::setlocale(LC_CTYPE, "en_US.UTF-8") != nullptr, "Initialize fixture UTF-8 locale");
#else
        check(std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr, "Initialize fixture UTF-8 locale");
#endif
        TemporaryDirectory fixtures;
        Tests tests;
        auto writer = makeLibArchiveWriter();
        const auto capabilities = writer->capabilities();
        const auto input = fixtures.path / pathFromUtf8("input-安安🐈");
        writeFile(input / "plain.txt", "plain contents\n");
        writeFile(input / "empty.bin", {});
        writeFile(input / pathFromUtf8("nested/こんにちは.txt"), "Japanese pathname\n");
        writeFile(input / pathFromUtf8("nested/한국어-é-可可🐄.txt"), "Unicode pathname\n");
        std::filesystem::create_directories(input / "empty-directory");
        generateFile(input / "nested/payload.bin", 128 * 1024 + 13);

        tests.run("Capabilities describe only real writer options and required unfiltered formats", [&] {
            std::set<CompressionFormat> formats;
            for (const auto& capability : capabilities) {
                check(formats.insert(capability.format).second, "Capabilities contain no duplicate format");
                check(!capability.name.empty() && !capability.extension.empty() && !capability.methods.empty(),
                      "Available format has a display name, extension and method");
                check(!capability.supportsPassword && !capability.supportsMultithreading,
                      "M3 does not advertise unsupported encryption or parallel archive writing");
                std::set<CompressionMethod> methods;
                for (const auto& method : capability.methods) {
                    check(methods.insert(method.method).second && !method.name.empty(),
                          "Method capabilities are named and unique");
                    check(method.minLevel.has_value() == method.maxLevel.has_value() &&
                          method.minLevel.has_value() == method.defaultLevel.has_value(),
                          "Level bounds and default are available together");
                    if (method.minLevel) check(*method.minLevel <= *method.defaultLevel &&
                                              *method.defaultLevel <= *method.maxLevel,
                                              "Default compression level belongs to supported interval");
                }
            }
            check(formats.contains(CompressionFormat::Zip) && formats.contains(CompressionFormat::Tar),
                  "ZIP and plain TAR creation are available");
        });

        std::size_t formatIndex = 0;
        for (const auto& capability : capabilities) {
            for (const auto& method : capability.methods) {
                const auto index = std::to_string(formatIndex++);
                tests.run(capability.name + " / " + method.name + " creates a Unicode tree that round-trips", [&] {
                    auto request = requestFor(input, fixtures.path / ("roundtrip-" + index + ".opaque"));
                    request.format = capability.format;
                    request.method = method.method;
                    request.level = method.defaultLevel.value_or(0);
                    roundTripTree(*writer, request, input, fixtures.path / ("roundtrip-out-" + index));
                });
            }
        }

        for (const auto& capability : capabilities) {
            for (const auto& method : capability.methods) {
                const auto index = std::to_string(formatIndex++);
                tests.run(capability.name + " / " + method.name + " creates Unicode paths under the C locale", [&] {
                    auto request = requestFor(input, fixtures.path / ("c-locale-" + index + ".opaque"));
                    request.format = capability.format;
                    request.method = method.method;
                    request.level = method.defaultLevel.value_or(0);
                    {
                        const CTypeLocale locale{"C"};
                        succeeded(createAndCheckLocale(*writer, request), "Unicode creation is independent of process LC_CTYPE");
                    }
                    auto expected = treeIn(input);
                    const auto basename = pathToUtf8(input.filename());
                    for (auto& name : expected) name = basename + '/' + name;
                    expected.push_back(basename + '/');
                    std::ranges::sort(expected);
                    std::vector<std::string> actual;
                    for (const auto& entry : entriesIn(request.destination)) actual.push_back(entryPath(entry));
                    std::ranges::sort(actual);
                    check(actual == expected, "C-locale creation retains Unicode entry names; expected=" +
                                              describeNames(expected) + "; actual=" + describeNames(actual));
                    const auto output = fixtures.path / ("c-locale-out-" + index);
                    extractTo(request.destination, output);
                    check(treeIn(output) == expected, "C-locale-created archive preserves every Unicode path");
                    const auto nativeInput = nativeFixturePath(input);
                    for (const auto& file : fixtureTreeEntries(nativeInput)) {
                        if (file.directory) continue;
                        const auto relative = relativeFixturePath(file.path, nativeInput);
                        check(equalFiles(file.path, output / input.filename() / relative),
                              "C-locale-created archive retains exact payload bytes");
                    }
                });
            }
        }

        tests.run("Worker locale is restored on invalid requests, cancellation and callback failures", [&] {
            auto request = requestFor(input, fixtures.path / "locale-restore.zip");
            const CTypeLocale locale{"C"};
#ifndef _WIN32
            const locale_t originalThreadLocale = uselocale(static_cast<locale_t>(nullptr));
#endif
            const auto cCapabilities = writer->capabilities();
            check(std::string{std::setlocale(LC_CTYPE, nullptr)} == "C", "Capability probing preserves the process C locale");
#ifndef _WIN32
            check(uselocale(static_cast<locale_t>(nullptr)) == originalThreadLocale,
                  "Capability probing restores the exact caller thread locale");
#endif
            check(cCapabilities.size() == capabilities.size(), "C locale does not remove available archive formats");
            for (std::size_t index = 0; index < capabilities.size(); ++index) {
                check(cCapabilities[index].format == capabilities[index].format &&
                      cCapabilities[index].methods.size() == capabilities[index].methods.size(),
                      "C locale preserves available formats and compression methods");
                for (std::size_t method = 0; method < capabilities[index].methods.size(); ++method) {
                    const auto& before = capabilities[index].methods[method];
                    const auto& after = cCapabilities[index].methods[method];
                    check(before.method == after.method && before.minLevel == after.minLevel &&
                          before.maxLevel == after.maxLevel && before.defaultLevel == after.defaultLevel,
                          "C locale preserves exact advertised compression levels");
                }
            }
            request.level = 999;
            failedWith(createAndCheckLocale(*writer, request), ArchiveErrorCode::UnsupportedCompression,
                       "Invalid request restores caller locale");
            request.level = 6;
            failedWith(createAndCheckLocale(*writer, request, [](const CompressionProgress&) { return false; }),
                       ArchiveErrorCode::Cancelled, "Callback cancellation restores caller locale");
            std::stop_source stop;
            failedWith(createAndCheckLocale(*writer, request, [&](const CompressionProgress&) {
                stop.request_stop();
                return true;
            }, stop.get_token()), ArchiveErrorCode::Cancelled, "Stop cancellation restores caller locale");
            failedWith(createAndCheckLocale(*writer, request, [](const CompressionProgress&) -> bool {
                throw std::runtime_error("test-only locale callback failure");
            }), ArchiveErrorCode::BackendError, "Callback failure restores caller locale");
            check(!std::filesystem::exists(request.destination), "Locale restoration error paths create no archive");
        });

        tests.run("Compression level endpoints produce readable archives and reject out-of-range levels", [&] {
            const auto source = fixtures.path / "level-input.bin";
            generateFile(source, 24 * 1024 + 7);
            std::size_t caseIndex = 0;
            for (const auto& capability : capabilities) {
                for (const auto& method : capability.methods) {
                    if (!method.minLevel) {
                        const auto output = fixtures.path / ("unsupported-level-" + std::to_string(caseIndex++));
                        const auto destination = output / "original.archive";
                        writeFile(destination, "original level fixture");
                        auto request = requestFor(source, destination);
                        request.overwriteExisting = true;
                        request.format = capability.format;
                        request.method = method.method;
                        request.level = 1;
                        failedWith(writer->create(request), ArchiveErrorCode::UnsupportedCompression,
                                   "Methods without level support reject nonzero compression level");
                        untouched(destination, "original level fixture");
                        continue;
                    }
                    for (const auto level : {*method.minLevel, *method.maxLevel}) {
                        const auto stem = "level-" + std::to_string(caseIndex++);
                        auto request = requestFor(source, fixtures.path / (stem + ".opaque"));
                        request.format = capability.format;
                        request.method = method.method;
                        request.level = level;
                        succeeded(writer->create(request), "Create supported compression level endpoint");
                        extractTo(request.destination, fixtures.path / (stem + "-out"));
                        check(equalFiles(source, fixtures.path / (stem + "-out") / source.filename()),
                              "Compression level preserves byte contents");
                    }
                    for (const auto level : {*method.minLevel - 1, *method.maxLevel + 1}) {
                        const auto output = fixtures.path / ("invalid-level-" + std::to_string(caseIndex++));
                        const auto destination = output / "original.archive";
                        writeFile(destination, "original level fixture");
                        auto request = requestFor(source, destination);
                        request.overwriteExisting = true;
                        request.format = capability.format;
                        request.method = method.method;
                        request.level = level;
                        failedWith(writer->create(request), ArchiveErrorCode::UnsupportedCompression,
                                   "Reject level outside advertised interval");
                        untouched(destination, "original level fixture");
                    }
                }
            }
        });

        tests.run("Unsupported format, codec, level and password requests preserve destination", [&] {
            const auto output = fixtures.path / "invalid-options";
            const auto destination = output / "original.zip";
            writeFile(destination, "untouched options original");
            auto request = requestFor(input, destination);
            request.overwriteExisting = true;
            request.method = CompressionMethod::Lzma2;
            failedWith(writer->create(request), ArchiveErrorCode::UnsupportedCompression, "ZIP rejects LZMA2 method");
            request.method = CompressionMethod::Deflate;
            request.format = static_cast<CompressionFormat>(255);
            failedWith(writer->create(request), ArchiveErrorCode::UnsupportedFormat, "Unknown format is structured error");
            request.format = CompressionFormat::Zip;
            request.password = "test-only-never-log-secret";
            const auto result = writer->create(request);
            failedWith(result, ArchiveErrorCode::UnsupportedOperation, "Unsupported archive encryption is rejected");
            check((result.error().message + result.error().backendMessage).find(*request.password) == std::string::npos,
                  "Password is absent from diagnostics");
            untouched(destination, "untouched options original");
        });

        tests.run("Existing destination requires explicit overwrite and successful replacement is valid", [&] {
            const auto output = fixtures.path / "replace-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "existing contents");
            auto request = requestFor(input / "plain.txt", destination);
            failedWith(writer->create(request), ArchiveErrorCode::OutputExists, "Overwrite requires explicit consent");
            untouched(destination, "existing contents");
            request.overwriteExisting = true;
            succeeded(writer->create(request), "Atomically replace explicitly authorized destination");
            check(childrenIn(output) == std::vector<std::string>{"original.zip"}, "Success removes staging siblings");
            extractTo(destination, fixtures.path / "replace-extracted");
            check(equalFiles(input / "plain.txt", fixtures.path / "replace-extracted/plain.txt"),
                  "Atomic replacement contains requested file");
        });

        tests.run("Unicode native destination path is supported end-to-end", [&] {
            const auto output = fixtures.path / pathFromUtf8("压缩输出-可可🐄");
            std::filesystem::create_directory(output);
            auto request = requestFor(input, output / pathFromUtf8("こんにちは-é-안안.zip"));
            roundTripTree(*writer, request, input, fixtures.path / "unicode-destination-out");
            check(childrenIn(output) == std::vector<std::string>{pathToUtf8(request.destination.filename())},
                  "Unicode archive destination leaves no temporary files");
        });

        tests.run("Destination appearing during finalization cannot be overwritten without authorization", [&] {
            const auto output = fixtures.path / "raced-destination";
            std::filesystem::create_directory(output);
            const auto destination = output / "raced.zip";
            auto request = requestFor(input / "plain.txt", destination);
            bool appeared = false;
            const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                if (!appeared && progress.phase == CompressionPhase::Finalizing) {
                    appeared = true;
                    writeFile(destination, "raced original");
                }
                return true;
            });
            failedWith(result, ArchiveErrorCode::OutputExists, "Atomic install honors no-overwrite after destination race");
            check(appeared, "Other destination appears after real payload IO");
            untouched(destination, "raced original");
        });

        tests.run("Independent duplicate basenames are rejected while overlap is deduplicated", [&] {
            const auto first = fixtures.path / "duplicate-a/same.txt";
            const auto second = fixtures.path / "duplicate-b/same.txt";
            writeFile(first, "first");
            writeFile(second, "second");
            const auto output = fixtures.path / "duplicate-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "duplicate original");
            auto request = requestFor(first, destination);
            request.inputPaths.push_back(second);
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::OutputExists, "Distinct same-name roots cannot silently overwrite");
            untouched(destination, "duplicate original");
            request.inputPaths = {input / "nested/payload.bin", input, input / "nested", input};
            roundTripTree(*writer, request, input, fixtures.path / "overlap-output");
        });

        tests.run("Distinct selected hardlink names are both retained while repeated selections are deduplicated", [&] {
            const auto first = fixtures.path / "hardlink-selection/first.txt";
            const auto second = fixtures.path / "hardlink-selection/second.txt";
            writeFile(first, "hardlinked selected payload");
            std::error_code error;
            std::filesystem::create_hard_link(first, second, error);
            check(!error, "Create same-volume selected hardlink fixture");
            auto request = requestFor(first, fixtures.path / "hardlink-selection.zip");
            request.inputPaths = {first, second, first, second};
            succeeded(writer->create(request), "Create both selected names without silent inode deduplication");
            auto entries = entriesIn(request.destination);
            std::vector<std::string> names;
            for (const auto& entry : entries) {
                check(entry.type == EntryType::RegularFile, "Selected hardlinks are archived as regular payload entries");
                names.push_back(entry.path);
            }
            std::ranges::sort(names);
            check(names == std::vector<std::string>{"first.txt", "second.txt"},
                  "Archive contains exactly both distinct selected basenames once");
            const auto output = fixtures.path / "hardlink-selection-out";
            extractTo(request.destination, output);
            check(equalFiles(first, output / "first.txt") && equalFiles(second, output / "second.txt"),
                  "Both selected hardlink names retain exact original bytes");
        });

        tests.run("Missing and empty input selections return errors without touching output", [&] {
            const auto output = fixtures.path / "missing-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "missing original");
            auto request = requestFor(fixtures.path / "absent.txt", destination);
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::FileNotFound, "Missing input is explicit");
            request.inputPaths.clear();
            failedWith(writer->create(request), ArchiveErrorCode::InvalidArchive, "Empty selection does not invent archive entries");
            untouched(destination, "missing original");
        });

        tests.run("Native source and destination paths containing NUL are rejected before truncated filesystem IO", [&] {
            const auto output = fixtures.path / "nul-output";
            const auto destination = output / "legit.zip";
            writeFile(destination, "NUL-path original");
            auto request = requestFor(input / "plain.txt", destination);
            request.overwriteExisting = true;
            auto malformed = destination.native();
            malformed += std::filesystem::path::value_type{};
            malformed += pathFromUtf8("hidden").native();
            request.destination = std::filesystem::path{malformed};
            failedWith(writer->create(request), ArchiveErrorCode::InvalidPathEncoding, "Destination NUL cannot truncate to existing archive");
            untouched(destination, "NUL-path original");
            malformed = (input / "plain.txt").native();
            malformed += std::filesystem::path::value_type{};
            malformed += pathFromUtf8("hidden").native();
            request.destination = destination;
            request.inputPaths = {std::filesystem::path{malformed}};
            failedWith(writer->create(request), ArchiveErrorCode::InvalidPathEncoding, "Source NUL cannot select unintended truncated path");
            untouched(destination, "NUL-path original");
            check(readSmallFile(input / "plain.txt") == "plain contents\n", "NUL-path input remains untouched");
        });

        tests.run("Private staging names cannot create archives that later fail safe extraction", [&] {
            const auto source = fixtures.path / ".ananarchiver-private.txt";
            writeFile(source, "reserved source");
            const auto output = fixtures.path / "reserved-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "reserved original");
            auto request = requestFor(source, destination);
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Reserved extraction staging namespace is rejected");
            untouched(destination, "reserved original");
        });

        tests.run("Directory destination and missing output parent are diagnosed without modifying inputs", [&] {
            const auto output = fixtures.path / "directory-destination";
            writeFile(output / "keep.txt", "directory contents");
            auto request = requestFor(input / "plain.txt", output);
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Destination must be a regular file path");
            check(childrenIn(output) == std::vector<std::string>{"keep.txt"} &&
                  readSmallFile(output / "keep.txt") == "directory contents", "Directory contents are preserved");
            request.destination = fixtures.path / "absent-output-parent/archive.zip";
            failedWith(writer->create(request), ArchiveErrorCode::FileNotFound, "Missing parent does not create unexpected directories");
            check(!std::filesystem::exists(request.destination.parent_path()), "Failed destination preflight creates no output directory");
        });

        tests.run("Output inside input, input aliases and input hardlink destination are rejected", [&] {
            auto request = requestFor(input, input / "recursive.zip");
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Cannot create archive inside selected directory");
            check(!std::filesystem::exists(input / "recursive.zip"), "Recursive output was never created");
            request = requestFor(input / "plain.txt", input / "plain.txt");
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Cannot replace selected input with archive");
            request.destination = input / "nested/../plain.txt";
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Lexical alias of selected input is rejected");
            const auto alias = fixtures.path / "input-hardlink.zip";
            std::error_code error;
            std::filesystem::create_hard_link(input / "plain.txt", alias, error);
            check(!error, "Create same-volume input hardlink fixture");
            request.destination = alias;
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Hardlink identity cannot overwrite selected input");
            check(readSmallFile(input / "plain.txt") == "plain contents\n", "Input content remains intact");
        });

        tests.run("24 MiB generated payload is streamed with measured progress and byte-perfect output", [&] {
            constexpr std::uint64_t size = 24ULL * 1024 * 1024 + 73;
            const auto source = fixtures.path / "large-stream.bin";
            generateFile(source, size);
            auto request = requestFor(source, fixtures.path / "large-stream.zip");
            request.method = CompressionMethod::Store;
            request.level = 0;
            std::uint64_t previousBytes = 0;
            std::uint64_t previousEntries = 0;
            std::size_t chunks = 0;
            bool monotonic = true;
            bool sawUnknownScan = false;
            bool sawFinalizing = false;
            bool totalsExact = true;
            succeeded(writer->create(request, [&](const CompressionProgress& progress) {
                if (progress.phase == CompressionPhase::Scanning) {
                    sawUnknownScan = !progress.totalBytes.has_value() && !progress.totalEntries.has_value();
                    return true;
                }
                monotonic = monotonic && progress.processedBytes >= previousBytes &&
                            progress.processedEntries >= previousEntries;
                if (progress.processedBytes > previousBytes) ++chunks;
                previousBytes = progress.processedBytes;
                previousEntries = progress.processedEntries;
                totalsExact = totalsExact && progress.totalBytes == size && progress.totalEntries == 1;
                sawFinalizing = sawFinalizing || progress.phase == CompressionPhase::Finalizing;
                return true;
            }), "Create generated streamed payload");
            check(monotonic && chunks > 2, "Payload progress is monotonic and emitted in streamed chunks");
            check(sawUnknownScan && totalsExact, "Unknown scan totals become measured writing totals");
            check(sawFinalizing && previousBytes == size && previousEntries == 1,
                  "Progress reaches complete payload accounting and finalization phase");
            extractTo(request.destination, fixtures.path / "large-stream-out");
            check(equalFiles(source, fixtures.path / "large-stream-out" / source.filename()),
                  "Generated large payload survives streaming create/read/extract byte comparison");
        });

        tests.run("Zero-byte files and empty-directory-only selections finish with exact entry totals", [&] {
            const auto emptyDirectory = fixtures.path / "only-empty-directory";
            std::filesystem::create_directory(emptyDirectory);
            const auto emptyFile = fixtures.path / "only-empty-file.txt";
            writeFile(emptyFile, {});
            for (const auto format : {CompressionFormat::Zip, CompressionFormat::Tar}) {
                const auto stem = format == CompressionFormat::Zip ? "zero-byte-zip" : "zero-byte-tar";
                auto request = requestFor(emptyFile, fixtures.path / (std::string{stem} + ".opaque"));
                request.inputPaths.push_back(emptyDirectory);
                request.format = format;
                request.method = CompressionMethod::Store;
                request.level = 0;
                std::uint64_t finishedEntries = 0;
                bool exactFinalTotals = false;
                succeeded(writer->create(request, [&](const CompressionProgress& progress) {
                    if (progress.phase == CompressionPhase::Finalizing) {
                        finishedEntries = progress.processedEntries;
                        exactFinalTotals = progress.processedBytes == 0 &&
                                           progress.totalBytes == 0 && progress.totalEntries == 2;
                    }
                    return true;
                }), "Create archive that contains no payload bytes");
                check(exactFinalTotals && finishedEntries == 2, "Zero-byte archive completes using real entry totals");
                const auto entries = entriesIn(request.destination);
                check(entries.size() == 2, "Zero-byte archive retains both selected objects");
                const auto output = fixtures.path / (std::string{stem} + "-out");
                extractTo(request.destination, output);
                check(std::filesystem::is_directory(output / emptyDirectory.filename()) &&
                      std::filesystem::is_regular_file(output / emptyFile.filename()) &&
                      std::filesystem::file_size(output / emptyFile.filename()) == 0,
                      "Zero-byte regular file and empty directory are distinct retained entries");
            }
        });

        tests.run("Paths beyond 260 characters and long PAX names survive create and extract", [&] {
            const auto longRoot = fixtures.path / "long-tree";
            auto relative = std::filesystem::path{};
            std::vector<std::string> expected{"long-tree/"};
            for (int index = 0; index < 5; ++index) {
                relative /= "segment-" + std::to_string(index) + std::string(50, 'x');
                const auto bytes = relative.generic_u8string();
                expected.push_back("long-tree/" + std::string{bytes.begin(), bytes.end()} + '/');
            }
            relative /= pathFromUtf8("安安-可可🐈.txt");
            const auto bytes = relative.generic_u8string();
            expected.push_back("long-tree/" + std::string{bytes.begin(), bytes.end()});
            std::ranges::sort(expected);
            writeFile(longRoot / relative, "long native path payload");
            check(pathToUtf8(longRoot / relative).size() > 260, "Fixture exceeds legacy Windows path length");
            for (const auto format : {CompressionFormat::Zip, CompressionFormat::Tar}) {
                const auto stem = format == CompressionFormat::Zip ? "long-zip" : "long-tar";
                auto request = requestFor(longRoot, fixtures.path / (std::string{stem} + ".opaque"));
                request.format = format;
                request.method = format == CompressionFormat::Zip ? CompressionMethod::Deflate : CompressionMethod::Store;
                request.level = format == CompressionFormat::Zip ? 6 : 0;
                const auto output = fixtures.path / (std::string{stem} + "-out");
                roundTripTree(*writer, request, longRoot, output);
                std::vector<std::string> actual;
                for (const auto& entry : entriesIn(request.destination)) actual.push_back(entryPath(entry));
                std::ranges::sort(actual);
                check(actual == expected, "Long archive matches generator-owned names rather than directory enumeration guesses");
                check(equalFiles(longRoot / relative, output / longRoot.filename() / relative),
                      "Known generated long pathname retains its exact original payload");
            }
        });

        tests.run("Cancellation during scanning, streaming and finalizing preserves original and cleans temps", [&] {
            const auto source = fixtures.path / "cancel-input.bin";
            generateFile(source, 2 * 1024 * 1024);
            for (const auto phase : {CompressionPhase::Scanning, CompressionPhase::Writing, CompressionPhase::Finalizing}) {
                for (const bool byToken : {false, true}) {
                    const auto stem = "cancel-" + std::to_string(static_cast<int>(phase)) + (byToken ? "-token" : "-callback");
                    const auto output = fixtures.path / stem;
                    const auto destination = output / "original.zip";
                    writeFile(destination, "cancel original");
                    auto request = requestFor(source, destination);
                    request.overwriteExisting = true;
                    request.method = CompressionMethod::Store;
                    request.level = 0;
                    std::stop_source cancellation;
                    bool interrupted = false;
                    const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                        if (progress.phase == phase &&
                            (phase != CompressionPhase::Writing || progress.processedBytes > 100'000)) {
                            interrupted = true;
                            if (byToken) cancellation.request_stop();
                            return byToken;
                        }
                        return true;
                    }, cancellation.get_token());
                    failedWith(result, ArchiveErrorCode::Cancelled, "Cooperative cancellation before atomic commit");
                    check(interrupted, "Requested cancellation reached real operation phase");
                    untouched(destination, "cancel original");
                }
            }
            std::stop_source cancellation;
            cancellation.request_stop();
            auto request = requestFor(source, fixtures.path / "pre-cancelled.zip");
            failedWith(writer->create(request, {}, cancellation.get_token()), ArchiveErrorCode::Cancelled,
                       "Pre-cancelled creation performs no payload IO");
            check(!std::filesystem::exists(request.destination), "Pre-cancellation creates no destination");
            succeeded(writer->create(request), "Writer remains reusable after every cancellation path");
        });

        tests.run("Thrown progress callbacks become structured errors and release all resources", [&] {
            const auto source = fixtures.path / "exception-input.bin";
            generateFile(source, 512 * 1024);
            for (const auto phase : {CompressionPhase::Scanning, CompressionPhase::Writing, CompressionPhase::Finalizing}) {
                const auto output = fixtures.path / ("exception-" + std::to_string(static_cast<int>(phase)));
                const auto destination = output / "original.zip";
                writeFile(destination, "exception original");
                auto request = requestFor(source, destination);
                request.overwriteExisting = true;
                bool threw = false;
                const auto result = writer->create(request, [&](const CompressionProgress& progress) -> bool {
                    if (progress.phase == phase &&
                        (phase != CompressionPhase::Writing || progress.processedBytes > 0)) {
                        threw = true;
                        throw std::runtime_error("test-only callback failure");
                    }
                    return true;
                });
                failedWith(result, ArchiveErrorCode::BackendError, "Callback exception cannot escape worker API");
                check(threw, "Exception occurred in requested phase");
                untouched(destination, "exception original");
            }
            const auto renamed = fixtures.path / "exception-input-renamed.bin";
            std::filesystem::rename(source, renamed);
            std::filesystem::rename(renamed, source);
            succeeded(writer->create(requestFor(source, fixtures.path / "after-exception.zip")),
                      "Writer and source file handles remain reusable after exceptions");
        });

        tests.run("Input size changes after scanning abort transaction and retain original", [&] {
            for (const bool grow : {false, true}) {
                const auto source = fixtures.path / (grow ? "changed-grow.bin" : "changed-shrink.bin");
                generateFile(source, 512 * 1024);
                const auto output = fixtures.path / (grow ? "changed-grow-out" : "changed-shrink-out");
                const auto destination = output / "original.zip";
                writeFile(destination, "mutation original");
                auto request = requestFor(source, destination);
                request.overwriteExisting = true;
                bool changed = false;
                const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                    if (!changed && progress.phase == CompressionPhase::Writing && progress.processedBytes == 0) {
                        changed = true;
                        std::filesystem::resize_file(source, grow ? 600 * 1024 : 400 * 1024);
                    }
                    return true;
                });
                check(changed, "Mutate input after metadata scan and before reading file bytes");
                check(!result && result.error().code == ArchiveErrorCode::IoError,
                      "Changed source size is a structured IO failure");
                untouched(destination, "mutation original");
            }
        });

        tests.run("Input deletion after scanning is diagnosed and rolls back staged archive", [&] {
            const auto source = fixtures.path / "deleted-input.txt";
            writeFile(source, "input removed after scan");
            const auto output = fixtures.path / "deleted-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "deleted original");
            auto request = requestFor(source, destination);
            request.overwriteExisting = true;
            bool removed = false;
            const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                if (!removed && progress.phase == CompressionPhase::Writing && progress.processedBytes == 0) {
                    removed = std::filesystem::remove(source);
                }
                return true;
            });
            failedWith(result, ArchiveErrorCode::FileNotFound, "Removed source cannot silently become an empty archived file");
            check(removed, "Input is removed after scanning and before payload open");
            untouched(destination, "deleted original");
        });

        tests.run("Input and scan resource limits fail safely before committing output", [&] {
            const auto output = fixtures.path / "limits-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "limits original");
            auto request = requestFor(input, destination);
            request.overwriteExisting = true;
            request.limits.maxEntries = 1;
            failedWith(writer->create(request), ArchiveErrorCode::ResourceLimitExceeded, "Entry count budget bounds scanning");
            request.limits = {};
            request.limits.maxEntryBytes = 1024;
            failedWith(writer->create(request), ArchiveErrorCode::ResourceLimitExceeded, "Per-file byte budget enforced");
            request.limits = {};
            request.limits.maxTotalBytes = 1024;
            failedWith(writer->create(request), ArchiveErrorCode::ResourceLimitExceeded, "Total source byte budget enforced");
            request.limits = {};
            request.limits.maxPathBytes = 5;
            failedWith(writer->create(request), ArchiveErrorCode::ResourceLimitExceeded, "Path metadata byte budget enforced");
            request.limits = {};
            request.limits.maxDepth = 1;
            failedWith(writer->create(request), ArchiveErrorCode::ResourceLimitExceeded, "Directory depth budget bounds scanning");
            request.limits = {};
            request.limits.maxMetadataBytes = 1;
            failedWith(writer->create(request), ArchiveErrorCode::ResourceLimitExceeded, "Metadata memory budget enforced");
            untouched(destination, "limits original");
        });

        tests.run("Elapsed-time limit interrupts real writing and removes staging output", [&] {
            const auto source = fixtures.path / "elapsed-input.bin";
            generateFile(source, 1024 * 1024);
            const auto output = fixtures.path / "elapsed-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "elapsed original");
            auto request = requestFor(source, destination);
            request.overwriteExisting = true;
            request.limits.maxElapsed = std::chrono::milliseconds{100};
            bool waited = false;
            const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                if (!waited && progress.phase == CompressionPhase::Writing && progress.processedBytes > 0) {
                    waited = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds{150});
                }
                return true;
            });
            failedWith(result, ArchiveErrorCode::ResourceLimitExceeded, "Elapsed budget is enforced at next stream boundary");
            check(waited, "Budget is exhausted while writing actual source bytes");
            untouched(destination, "elapsed original");
        });

        tests.run("TAR creation records source timestamp and POSIX permission metadata", [&] {
            const auto source = fixtures.path / "timestamp.txt";
            writeFile(source, "timestamp fixture");
            const auto timestamp = std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}};
            setSourceTimestamp(source, timestamp);
#ifndef _WIN32
            std::filesystem::permissions(source, std::filesystem::perms::owner_read |
                                                   std::filesystem::perms::owner_write |
                                                   std::filesystem::perms::group_read,
                                         std::filesystem::perm_options::replace);
#endif
            auto request = requestFor(source, fixtures.path / "timestamp.tar");
            request.format = CompressionFormat::Tar;
            request.method = CompressionMethod::Store;
            request.level = 0;
            succeeded(writer->create(request), "Create metadata preserving TAR");
            const auto entries = entriesIn(request.destination);
            check(entries.size() == 1 && entries.front().modified.has_value(), "TAR contains timestamp metadata");
            check(std::chrono::abs(*entries.front().modified - timestamp) <= std::chrono::seconds{2},
                  "Source modification timestamp is retained within native clock conversion precision");
            const auto output = fixtures.path / "timestamp-out";
            extractTo(request.destination, output);
            check(equalFiles(source, output / source.filename()), "Metadata preservation retains content");
#ifndef _WIN32
            const auto mask = std::filesystem::perms::owner_all | std::filesystem::perms::group_all |
                              std::filesystem::perms::others_all;
            check(archivedPermissions(request.destination, pathToUtf8(source.filename())) ==
                  static_cast<unsigned int>(std::filesystem::status(source).permissions() & mask),
                  "TAR archive metadata preserves source POSIX permissions");
#else
            check(archivedPermissions(request.destination, pathToUtf8(source.filename())) == 0644U,
                  "TAR archive metadata records the Windows read-write file mode");
#endif
            request.preservePermissions = false;
            request.destination = fixtures.path / "timestamp-default-mode.tar";
            succeeded(writer->create(request), "Create TAR with permission preservation disabled");
            check(archivedPermissions(request.destination, pathToUtf8(source.filename())) == 0644U,
                  "Disabled permission preservation records regular default mode");
        });

        tests.run("TAR preserves valid source timestamps before the Unix epoch", [&] {
            const auto source = fixtures.path / "pre-epoch.txt";
            writeFile(source, "pre-epoch timestamp fixture");
            const auto timestamp = std::chrono::sys_seconds{std::chrono::seconds{-315'619'200}}; // 1960-01-01 UTC.
            setSourceTimestamp(source, timestamp);
            auto request = requestFor(source, fixtures.path / "pre-epoch.tar");
            request.format = CompressionFormat::Tar;
            request.method = CompressionMethod::Store;
            request.level = 0;
            succeeded(writer->create(request), "Create TAR with negative Unix timestamp");
            const auto entries = entriesIn(request.destination);
            check(entries.size() == 1 && entries.front().modified.has_value(), "Created entry has pre-epoch timestamp metadata");
            check(std::chrono::abs(*entries.front().modified - timestamp) <= std::chrono::seconds{2},
                  "Pre-1970 source modification time is retained rather than replaced with Unix epoch");
        });

        const bool symlinksSupported = canCreateSymlinks(fixtures.path);
        if (symlinksSupported) {
            tests.run("Input symlinks, directory cycles and destination symlinks are rejected without traversal", [&] {
                const auto linkedRoot = fixtures.path / "symlink-input";
                writeFile(linkedRoot / "ordinary.txt", "safe source");
                std::filesystem::create_directory_symlink(linkedRoot, linkedRoot / "loop");
                const auto output = fixtures.path / "symlink-output";
                const auto destination = output / "original.zip";
                writeFile(destination, "symlink original");
                auto request = requestFor(linkedRoot, destination);
                request.overwriteExisting = true;
                failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Directory symlink cycle is not followed");
                untouched(destination, "symlink original");
                request.inputPaths = {linkedRoot / "loop"};
                failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Selected root symlink is rejected");
                const auto target = fixtures.path / "destination-target.txt";
                writeFile(target, "target contents");
                const auto symlink = fixtures.path / "destination-symlink.zip";
                std::filesystem::create_symlink(target, symlink);
                request.inputPaths = {input / "plain.txt"};
                request.destination = symlink;
                failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Destination symlink cannot redirect output");
                check(readSmallFile(target) == "target contents" && std::filesystem::is_symlink(symlink),
                      "Destination link and link target survive failed creation");
            });

            tests.run("Source replacement with a symlink after scanning cannot expose target payload", [&] {
                const auto source = fixtures.path / "replaced-source.txt";
                const auto secret = fixtures.path / "unselected-secret.txt";
                writeFile(source, "selected source");
                writeFile(secret, "unselected private contents");
                const auto output = fixtures.path / "replaced-link-output";
                const auto destination = output / "original.zip";
                writeFile(destination, "replaced original");
                auto request = requestFor(source, destination);
                request.overwriteExisting = true;
                bool replaced = false;
                const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                    if (!replaced && progress.phase == CompressionPhase::Writing && progress.processedBytes == 0) {
                        std::filesystem::remove(source);
                        std::filesystem::create_symlink(secret, source);
                        replaced = true;
                    }
                    return true;
                });
                failedWith(result, ArchiveErrorCode::UnsafePath, "Source no-follow open rejects late symlink replacement");
                check(replaced, "Source becomes symlink between scan and read");
                check(readSmallFile(secret) == "unselected private contents", "Unselected target remains intact");
                untouched(destination, "replaced original");
            });
        } else {
            std::cout << "Native symlink creation unavailable; symlink-creation cases skipped\n";
        }

#ifdef _WIN32
        const auto caseSensitive = fixtures.path / "case-sensitive-input";
        std::filesystem::create_directory(caseSensitive);
        DWORD caseSensitiveError = 0;
        if (enableCaseSensitiveDirectory(caseSensitive, caseSensitiveError)) {
            tests.run("Windows case-sensitive directories retain differently cased selected siblings", [&] {
                const auto upper = caseSensitive / "Foo/child.txt";
                const auto lower = caseSensitive / "foo/sibling.txt";
                writeFile(upper, "uppercase directory child");
                writeFile(lower, "lowercase directory sibling");
                check(!std::filesystem::equivalent(upper.parent_path(), lower.parent_path()),
                      "Native case-sensitive fixture has distinct Foo and foo directories");
                auto request = requestFor(upper, fixtures.path / "case-sensitive-selection.zip");
                request.inputPaths.push_back(lower.parent_path());
                succeeded(writer->create(request), "Selected upper sibling is not incorrectly covered by lower directory");
                std::vector<std::string> names;
                for (const auto& entry : entriesIn(request.destination)) names.push_back(entryPath(entry));
                std::ranges::sort(names);
                check(names == std::vector<std::string>{"child.txt", "foo/", "foo/sibling.txt"},
                      "Case-sensitive selection preserves the exact selected roots and children");
                const auto output = fixtures.path / "case-sensitive-selection-out";
                extractTo(request.destination, output);
                check(equalFiles(upper, output / "child.txt") && equalFiles(lower, output / "foo/sibling.txt"),
                      "Both differently cased source directories retain their selected payloads");
            });
        } else {
            std::cout << "Windows case-sensitive directory fixture unavailable; Windows error "
                      << caseSensitiveError << "; optional case skipped\n";
        }

        tests.run("Windows sharing violation during atomic install preserves original and releases staging file", [&] {
            struct LockedFile {
                HANDLE handle = INVALID_HANDLE_VALUE;
                ~LockedFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
            } locked;
            const auto output = fixtures.path / "sharing-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "sharing original");
            locked.handle = CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        nullptr, OPEN_EXISTING, 0, nullptr);
            check(locked.handle != INVALID_HANDLE_VALUE, "Hold original archive against replacement");
            auto request = requestFor(input / "plain.txt", destination);
            request.overwriteExisting = true;
            bool finishedPayload = false;
            const auto result = writer->create(request, [&](const CompressionProgress& progress) {
                finishedPayload = finishedPayload || progress.phase == CompressionPhase::Finalizing;
                return true;
            });
            check(!result && (result.error().code == ArchiveErrorCode::PermissionDenied ||
                              result.error().code == ArchiveErrorCode::IoError),
                  "Atomic install sharing error is structured");
            check(finishedPayload, "Failure occurs after real archive payload finalization");
            untouched(destination, "sharing original");
            CloseHandle(locked.handle);
            locked.handle = INVALID_HANDLE_VALUE;
            succeeded(writer->create(request), "Atomic replacement succeeds after OS lock release");
            check(childrenIn(output) == std::vector<std::string>{"original.zip"}, "Retry leaves only completed archive");
        });

        tests.run("Windows input junction loop is rejected without requiring symlink privileges", [&] {
            const auto root = fixtures.path / "junction-input";
            writeFile(root / "ordinary.txt", "junction source");
            DWORD error = 0;
            check(extraction_tests::createDirectoryJunction(root / "loop", root, error),
                  "Create fixture junction: Windows error " + std::to_string(error));
            const auto output = fixtures.path / "junction-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "junction original");
            auto request = requestFor(root, destination);
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::UnsafePath, "Input junction is never recursively followed");
            untouched(destination, "junction original");
            check(RemoveDirectoryW((root / "loop").c_str()) != 0, "Remove owned junction fixture without following target");
        });
#else
        tests.run("POSIX special inputs fail without blocking on FIFO reads", [&] {
            const auto fifo = fixtures.path / "special-input.fifo";
            check(mkfifo(fifo.c_str(), 0600) == 0, "Create FIFO fixture");
            const auto output = fixtures.path / "fifo-output";
            const auto destination = output / "original.zip";
            writeFile(destination, "fifo original");
            auto request = requestFor(fifo, destination);
            request.overwriteExisting = true;
            failedWith(writer->create(request), ArchiveErrorCode::UnsupportedOperation, "Reject special file before reading");
            untouched(destination, "fifo original");
        });
#endif
        return tests.finish();
    } catch (const std::exception& error) {
        std::cerr << "Compression test setup failed: " << error.what() << '\n';
        return 1;
    }
}
