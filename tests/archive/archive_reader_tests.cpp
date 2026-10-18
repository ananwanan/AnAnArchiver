#include "ananarchiver/libarchive_reader.h"
#include "ananarchiver/unicode.h"

#include <archive.h>
#include <archive_entry.h>
#include <clocale>

#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace ananarchiver;

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class T>
void succeeded(const Result<T>& result, const std::string& message) {
    check(result.has_value(), message + (result ? "" : ": " + result.error().message +
                                                        "; " + result.error().backendMessage));
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("ananarchiver-archive-tests-" + std::to_string(stamp));
        check(std::filesystem::create_directory(path), "Create unique temporary fixture directory");
    }

    ~TemporaryDirectory() {
        // Only this object's newly created test directory is ever removed.
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) {
            std::cerr << "Could not clean test directory: " << error.message() << '\n';
        }
    }
};

struct WriteArchiveDeleter {
    void operator()(archive* handle) const noexcept { archive_write_free(handle); }
};

struct EntryDeleter {
    void operator()(archive_entry* entry) const noexcept { archive_entry_free(entry); }
};

la_ssize_t writeFixture(archive* handle, void* clientData, const void* data,
                       std::size_t size) {
    auto& output = *static_cast<std::ofstream*>(clientData);
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!output) {
        archive_set_error(handle, EIO, "Test fixture output failed");
        return -1;
    }
    return static_cast<la_ssize_t>(size);
}

struct FixtureEntry {
    std::string path;
    std::string data;
    EntryType type = EntryType::RegularFile;
    std::string linkTarget;

    FixtureEntry(std::string entryPath, std::string entryData = {},
                 EntryType entryType = EntryType::RegularFile, std::string target = {})
        : path(std::move(entryPath)), data(std::move(entryData)), type(entryType),
          linkTarget(std::move(target)) {}
};

enum class FixtureFormat { Zip, Tar, SevenZip, Gzip };

void createFixture(const std::filesystem::path& path, FixtureFormat format,
                   const std::vector<FixtureEntry>& entries, bool encrypted = false,
                   const char* headerCharset = nullptr) {
    std::ofstream output{path, std::ios::binary};
    check(output.is_open(), "Open test fixture output");
    std::unique_ptr<archive, WriteArchiveDeleter> writer{archive_write_new()};
    check(writer != nullptr, "Create test fixture writer");
    int configured = ARCHIVE_FATAL;
    switch (format) {
    case FixtureFormat::Zip: configured = archive_write_set_format_zip(writer.get()); break;
    case FixtureFormat::Tar: configured = archive_write_set_format_pax_restricted(writer.get()); break;
    case FixtureFormat::SevenZip: configured = archive_write_set_format_7zip(writer.get()); break;
    case FixtureFormat::Gzip:
        configured = archive_write_set_format_raw(writer.get());
        check(archive_write_add_filter_gzip(writer.get()) == ARCHIVE_OK, "Configure gzip fixture");
        break;
    }
    check(configured == ARCHIVE_OK, "Configure fixture archive format");
    if (headerCharset != nullptr) {
        check(archive_write_set_format_option(writer.get(), "zip", "hdrcharset", headerCharset) == ARCHIVE_OK,
              "Configure ZIP fixture pathname encoding");
    }
    if (encrypted) {
        check(archive_write_set_options(writer.get(), "zip:encryption=aes256") == ARCHIVE_OK,
              "Configure AES fixture encryption");
        check(archive_write_set_passphrase(writer.get(), "test-only-passphrase") == ARCHIVE_OK,
              "Configure fixture passphrase");
    }
    check(archive_write_open(writer.get(), &output, nullptr, writeFixture, nullptr) == ARCHIVE_OK,
          "Open fixture writer stream");
    for (const auto& fixture : entries) {
        std::unique_ptr<archive_entry, EntryDeleter> entry{archive_entry_new()};
        check(entry != nullptr, "Allocate fixture entry");
        archive_entry_set_pathname_utf8(entry.get(), fixture.path.c_str());
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(fixture.data.size()));
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_mtime(entry.get(), 1'700'000'000, 123'000'000);
        switch (fixture.type) {
        case EntryType::Directory: archive_entry_set_filetype(entry.get(), AE_IFDIR); break;
        case EntryType::Symlink:
            archive_entry_set_filetype(entry.get(), AE_IFLNK);
            archive_entry_set_symlink_utf8(entry.get(), fixture.linkTarget.c_str());
            break;
        case EntryType::Hardlink:
            archive_entry_set_filetype(entry.get(), AE_IFREG);
            archive_entry_set_hardlink_utf8(entry.get(), fixture.linkTarget.c_str());
            break;
        default: archive_entry_set_filetype(entry.get(), AE_IFREG); break;
        }
        const auto written = archive_write_header(writer.get(), entry.get());
        check(written == ARCHIVE_OK,
              "Write fixture header: " + std::string{archive_error_string(writer.get()) == nullptr
                                                        ? "" : archive_error_string(writer.get())});
        if (!fixture.data.empty()) {
            check(archive_write_data(writer.get(), fixture.data.data(), fixture.data.size()) ==
                      static_cast<la_ssize_t>(fixture.data.size()), "Write fixture data");
        }
        check(archive_write_finish_entry(writer.get()) == ARCHIVE_OK, "Finish fixture entry");
    }
    check(archive_write_close(writer.get()) == ARCHIVE_OK, "Close fixture archive");
    output.close();
    check(!output.fail(), "Flush fixture output");
}

std::vector<ArchiveEntry> entriesIn(ArchiveReader& reader) {
    std::vector<ArchiveEntry> entries;
    auto result = reader.enumerate([&](const ArchiveEntry& entry) {
        entries.push_back(entry);
        return true;
    });
    succeeded(result, "Enumerate fixture");
    return entries;
}

#ifdef _WIN32
class WindowsThreadLocale {
public:
    explicit WindowsThreadLocale(const char* name)
        : previousMode_(_configthreadlocale(0)),
          previousLocale_(std::setlocale(LC_CTYPE, nullptr)) {
        check(_configthreadlocale(_ENABLE_PER_THREAD_LOCALE) != -1,
              "Enable private test thread locale");
        if (std::setlocale(LC_CTYPE, name) == nullptr) {
            _configthreadlocale(previousMode_);
            throw std::runtime_error("Select private test thread locale");
        }
    }

    ~WindowsThreadLocale() {
        std::setlocale(LC_CTYPE, previousLocale_.c_str());
        _configthreadlocale(previousMode_);
    }

    WindowsThreadLocale(const WindowsThreadLocale&) = delete;
    WindowsThreadLocale& operator=(const WindowsThreadLocale&) = delete;

private:
    int previousMode_;
    std::string previousLocale_;
};

std::string nativeOemFixturePath() {
    // Choose an exactly representable non-ASCII name instead of assuming the
    // test machine uses GBK. These cover common East Asian and OEM codepages.
    const auto codepage = GetOEMCP();
    for (const std::wstring candidate : {L"资料/安安.txt", L"資料/名前.txt", L"자료/이름.txt",
                                         L"данные/имя.txt", L"café/élève.txt", L"Α/α.txt",
                                         L"נתונים/שם.txt", L"بيانات/اسم.txt"}) {
        const auto byteCount = WideCharToMultiByte(codepage, 0, candidate.data(),
                                                   static_cast<int>(candidate.size()),
                                                   nullptr, 0, nullptr, nullptr);
        if (byteCount == 0) continue;
        std::string encoded(static_cast<std::size_t>(byteCount), '\0');
        check(WideCharToMultiByte(codepage, 0, candidate.data(), static_cast<int>(candidate.size()),
                                  encoded.data(), byteCount, nullptr, nullptr) == byteCount,
              "Encode native OEM fixture filename");
        const auto characterCount = MultiByteToWideChar(codepage, 0, encoded.data(), byteCount,
                                                        nullptr, 0);
        if (characterCount == 0) continue;
        std::wstring decoded(static_cast<std::size_t>(characterCount), L'\0');
        check(MultiByteToWideChar(codepage, 0, encoded.data(), byteCount,
                                  decoded.data(), characterCount) == characterCount,
              "Decode native OEM fixture filename");
        if (decoded == candidate) return pathToUtf8(std::filesystem::path{candidate});
    }
    throw std::runtime_error("No regression filename is representable in OEM codepage " +
                             std::to_string(codepage));
}

void checkZipUtf8Flag(const std::filesystem::path& path, bool expected) {
    std::ifstream input{path, std::ios::binary};
    std::array<unsigned char, 8> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    check(input.good() && header[0] == 'P' && header[1] == 'K' && header[2] == 3 && header[3] == 4,
          "Read generated ZIP local header");
    check(((header[7] & 0x08) != 0) == expected,
          "Generated ZIP has the requested Unicode filename flag");
}
#endif

class Tests {
public:
    void run(const std::string& name, const std::function<void()>& callback) {
        try {
            callback();
            ++passed_;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " archive tests passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};

} // namespace

int main() {
#ifdef _WIN32
    std::setlocale(LC_CTYPE, ".UTF-8");
#else
    if (!std::setlocale(LC_CTYPE, "C.UTF-8"))
        std::setlocale(LC_CTYPE, "en_US.UTF-8");
#endif
    try {
        TemporaryDirectory fixtures;
        Tests tests;
        const auto zip = fixtures.path / "content-detected.unknown";
        const auto tar = fixtures.path / "stream.tar";
        createFixture(zip, FixtureFormat::Zip,
                      {{"folder/", {}, EntryType::Directory, {}}, {"folder/first.txt", "first"},
                       {"second.txt", "second"}});
        createFixture(tar, FixtureFormat::Tar,
                      {{"first.txt", "first"}, {"second.txt", "second"}});

        tests.run("Content detection and ZIP metadata", [&] {
            auto reader = makeLibArchiveReader();
            auto info = reader->open(zip);
            succeeded(info, "Open ZIP with unrelated extension");
            check(info->format == ArchiveFormat::Zip, "Detect ZIP content");
            auto entries = entriesIn(*reader);
            check(entries.size() == 3, "ZIP entry count");
            check(entries[0].type == EntryType::Directory, "Directory metadata");
            check(entries[1].path == "folder/first.txt" && entries[1].size == 5,
                  "File name and size metadata");
            check(entries[1].type == EntryType::RegularFile, "File metadata type");
            check(entries[1].modified.has_value(), "File timestamp metadata");
            check(!entries[1].compressedSize, "Unknown compressed size stays unknown");
        });

        tests.run("Repeat enumeration reopens stream", [&] {
            auto reader = makeLibArchiveReader();
            succeeded(reader->open(tar), "Open TAR");
            check(entriesIn(*reader).size() == 2, "First enumeration");
            check(entriesIn(*reader).size() == 2, "Second enumeration");
        });

        tests.run("Missing file and invalid reader state", [&] {
            auto reader = makeLibArchiveReader();
            auto result = reader->open(fixtures.path / "missing.zip");
            check(!result && result.error().code == ArchiveErrorCode::FileNotFound, "Missing file code");
            auto listed = reader->enumerate([](const auto&) { return true; });
            check(!listed && listed.error().code == ArchiveErrorCode::InvalidArchive,
                  "Cannot enumerate unopened reader");
        });

        tests.run("Invalid data and zero length input", [&] {
            const auto invalid = fixtures.path / "invalid.zip";
            std::ofstream{invalid, std::ios::binary} << "ordinary text is not an archive";
            const auto empty = fixtures.path / "empty.zip";
            std::ofstream{empty, std::ios::binary}.close();
            auto reader = makeLibArchiveReader();
            auto result = reader->open(invalid);
            check(!result && result.error().code == ArchiveErrorCode::InvalidArchive,
                  "Reject uncompressed raw junk");
            check(!reader->open(empty), "Reject empty input file");
            check(!reader->open(fixtures.path), "Reject directory as archive");
        });

        tests.run("Failed replacement open clears prior archive", [&] {
            auto reader = makeLibArchiveReader();
            succeeded(reader->open(zip), "Open prior archive");
            check(!reader->open(fixtures.path / "absent.zip"), "Failed replacement open");
            check(!reader->enumerate([](const auto&) { return true; }), "No stale archive after failed open");
        });

        tests.run("Empty ZIP is a valid archive", [&] {
            const auto emptyZip = fixtures.path / "valid-empty.zip";
            createFixture(emptyZip, FixtureFormat::Zip, {});
            auto reader = makeLibArchiveReader();
            auto info = reader->open(emptyZip);
            succeeded(info, "Open valid empty ZIP");
            check(info->format == ArchiveFormat::Zip, "Empty ZIP format");
            check(entriesIn(*reader).empty(), "Empty ZIP entries");
        });

        tests.run("Unicode archive path and entry names", [&] {
            const auto unicodeArchive = fixtures.path / pathFromUtf8("安安-こんにちは-🐈.tar");
            const std::string unicodeEntry = "中文/こんにちは/한글/可可🐄🐈/café.txt";
            const std::string decomposedEntry =
                "中文/こんにちは/\u1112\u1161\u11ab\u1100\u1173\u11af/可可🐄🐈/cafe\u0301.txt";
            createFixture(unicodeArchive, FixtureFormat::Tar,
                          {{unicodeEntry, "unicode"}, {decomposedEntry, "decomposed unicode"}});
            auto reader = makeLibArchiveReader();
            succeeded(reader->open(unicodeArchive), "Open Unicode native path");
            const auto entries = entriesIn(*reader);
            // libarchive deliberately returns decomposed UTF-8 pathnames on
            // macOS (archive_string.c); other platforms use composed names.
            // Keep exact comparisons for both encoded inputs, without changing
            // production metadata or relying on filesystem normalization.
#ifdef __APPLE__
            const auto& expected = decomposedEntry;
#else
            const auto& expected = unicodeEntry;
#endif
            check(entries.size() == 2, "Both Unicode metadata entries are enumerated");
            check(entries[0].path == expected, "Composed Unicode metadata has the backend's native representation");
            check(entries[1].path == expected, "Decomposed Unicode metadata has the backend's native representation");
            check(entries[0].size == 7 && entries[1].size == 18, "Unicode entries retain independent payload metadata");
        });

#ifdef _WIN32
        tests.run("Windows legacy OEM and UTF-8 ZIP names survive caller locales", [&] {
            const auto legacyZip = fixtures.path / "legacy-oem.zip";
            const auto unicodeZip = fixtures.path / "flagged-utf8.zip";
            const auto legacyName = nativeOemFixturePath();
            const std::string unicodeName = "中文/こんにちは/한글/可可🐈.txt";
            {
                WindowsThreadLocale locale{".UTF-8"};
                createFixture(legacyZip, FixtureFormat::Zip,
                              {{legacyName, "legacy payload"}, {"unselected.txt", "leave behind"}},
                              false, "CP_OEMCP");
                createFixture(unicodeZip, FixtureFormat::Zip,
                              {{unicodeName, "unicode payload"}, {"unselected.txt", "leave behind"}},
                              false, "UTF-8");
            }
            checkZipUtf8Flag(legacyZip, false);
            checkZipUtf8Flag(unicodeZip, true);
            std::size_t outputIndex = 0;
            for (const auto* callerLocale : {"C", ".UTF-8"}) {
                WindowsThreadLocale locale{callerLocale};
                const std::string expectedLocale = std::setlocale(LC_CTYPE, nullptr);
                const auto checkLocale = [&] {
                    check(std::string{std::setlocale(LC_CTYPE, nullptr)} == expectedLocale,
                          "Archive operation restores caller LC_CTYPE");
                    check(_configthreadlocale(0) == _ENABLE_PER_THREAD_LOCALE,
                          "Archive operation restores caller locale mode");
                };
                for (const bool legacy : {true, false}) {
                    auto reader = makeLibArchiveReader();
                    succeeded(reader->open(legacy ? legacyZip : unicodeZip), "Open encoded ZIP fixture");
                    checkLocale();
                    const auto& expectedName = legacy ? legacyName : unicodeName;
                    for (int repeat = 0; repeat < 2; ++repeat) {
                        const auto entries = entriesIn(*reader);
                        checkLocale();
                        check(entries.size() == 2 && entries[0].path == expectedName &&
                              entries[1].path == "unselected.txt",
                              "Repeated enumeration preserves exact ZIP filenames");
                        ExtractionRequest request;
                        request.destination = fixtures.path / ("encoded-out-" + std::to_string(outputIndex++));
                        request.selectedIndices = std::vector<std::uint64_t>{0};
                        request.overwrite = OverwritePolicy::Overwrite;
                        succeeded(reader->extract(request), "Extract selected encoded filename");
                        checkLocale();
                        std::ifstream payload{request.destination / pathFromUtf8(expectedName), std::ios::binary};
                        check(payload.is_open(), "Selected output has its exact Unicode filename");
                        const std::string actual{std::istreambuf_iterator<char>{payload},
                                                 std::istreambuf_iterator<char>{}};
                        check(actual == (legacy ? "legacy payload" : "unicode payload"),
                              "Selected output retains exact payload");
                        check(!std::filesystem::exists(request.destination / "unselected.txt"),
                              "Index selection does not extract other entries");
                    }
                }
            }
        });

        tests.run("Windows archive locale scope restores success, failure and cancellation", [&] {
            std::atomic<bool> observerReady{false};
            std::atomic<bool> observerUnchanged{true};
            std::jthread observer{[&](std::stop_token stop) {
                try {
                    WindowsThreadLocale locale{"C"};
                    const std::string expected = std::setlocale(LC_CTYPE, nullptr);
                    observerReady = true;
                    while (!stop.stop_requested()) {
                        if (std::string{std::setlocale(LC_CTYPE, nullptr)} != expected ||
                            _configthreadlocale(0) != _ENABLE_PER_THREAD_LOCALE) {
                            observerUnchanged = false;
                        }
                        std::this_thread::yield();
                    }
                } catch (...) {
                    observerUnchanged = false;
                    observerReady = true;
                }
            }};
            while (!observerReady.load()) std::this_thread::yield();
            const auto invalid = fixtures.path / "locale-invalid.zip";
            std::ofstream{invalid, std::ios::binary} << "not an archive";
            const auto exercise = [&] {
                const std::string expectedLocale = std::setlocale(LC_CTYPE, nullptr);
                const auto expectedMode = _configthreadlocale(0);
                const auto unchanged = [&] {
                    check(std::string{std::setlocale(LC_CTYPE, nullptr)} == expectedLocale,
                          "Restore LC_CTYPE on every exit path");
                    check(_configthreadlocale(0) == expectedMode,
                          "Restore enabled or disabled caller locale mode");
                };
                auto reader = makeLibArchiveReader();
                check(!reader->open(invalid), "Invalid archive fails inside locale scope");
                unchanged();
                succeeded(reader->open(zip), "Open after failed format detection");
                unchanged();
                check(entriesIn(*reader).size() == 3, "Successful enumeration after failure");
                unchanged();
                auto result = reader->enumerate([](const auto&) -> bool {
                    throw std::runtime_error("locale regression callback failure");
                });
                check(!result && result.error().code == ArchiveErrorCode::BackendError,
                      "Callback exception fails inside locale scope");
                unchanged();
                result = reader->enumerate([](const auto&) { return false; });
                check(!result && result.error().code == ArchiveErrorCode::Cancelled,
                      "Callback cancellation exits active locale scope");
                unchanged();
                std::stop_source stopped;
                stopped.request_stop();
                auto opened = reader->open(zip, stopped.get_token());
                check(!opened && opened.error().code == ArchiveErrorCode::Cancelled, "Cancelled open");
                unchanged();
                succeeded(reader->open(zip), "Reopen after cancelled operation");
                ExtractionRequest request;
                request.destination = fixtures.path / "locale-cancelled-output";
                result = reader->extract(request, {}, stopped.get_token());
                check(!result && result.error().code == ArchiveErrorCode::Cancelled, "Cancelled extraction");
                unchanged();
            };
            // Also exercise the default disabled mode inherited from main().
            exercise();
            for (const auto* callerLocale : {"C", ".UTF-8"}) {
                WindowsThreadLocale locale{callerLocale};
                exercise();
            }
            observer.request_stop();
            observer.join();
            check(observerUnchanged.load(), "Concurrent thread retains its independent C locale");
        });
#endif

        tests.run("7z format and enumeration", [&] {
            const auto sevenZip = fixtures.path / "fixture.7z";
            createFixture(sevenZip, FixtureFormat::SevenZip, {{"hello.txt", "seven zip"}});
            auto reader = makeLibArchiveReader();
            auto info = reader->open(sevenZip);
            succeeded(info, "Open 7z");
            check(info->format == ArchiveFormat::SevenZip, "Detect 7z");
            check(entriesIn(*reader).size() == 1, "7z metadata count");
        });

        tests.run("GZIP single stream detection", [&] {
            const auto gzip = fixtures.path / "single.data";
            createFixture(gzip, FixtureFormat::Gzip, {{"hello.txt", "gzip stream"}});
            auto reader = makeLibArchiveReader();
            auto info = reader->open(gzip);
            succeeded(info, "Open raw GZIP stream");
            check(info->format == ArchiveFormat::Gzip, "Detect GZIP stream");
            const auto entries = entriesIn(*reader);
            check(entries.size() == 1, "GZIP metadata entry");
            check(!entries[0].sizeKnown, "Raw stream size stays unknown");
        });

        tests.run("Encrypted ZIP metadata without password persistence", [&] {
            const auto encrypted = fixtures.path / "encrypted.zip";
            createFixture(encrypted, FixtureFormat::Zip, {{"secret.txt", "encrypted payload"}}, true);
            auto reader = makeLibArchiveReader();
            auto info = reader->open(encrypted);
            succeeded(info, "Open encrypted ZIP metadata");
            check(info->encrypted, "Encryption detected in metadata");
            const auto entries = entriesIn(*reader);
            check(entries.size() == 1 && entries[0].path == "secret.txt", "Encrypted entry names listed");
        });

        tests.run("Cancellation before open and enumeration", [&] {
            auto reader = makeLibArchiveReader();
            std::stop_source stopped;
            stopped.request_stop();
            auto opened = reader->open(zip, stopped.get_token());
            check(!opened && opened.error().code == ArchiveErrorCode::Cancelled, "Cancelled open");
            succeeded(reader->open(zip), "Open after cancellation");
            auto listed = reader->enumerate([](const auto&) { return true; }, stopped.get_token());
            check(!listed && listed.error().code == ArchiveErrorCode::Cancelled, "Cancelled enumeration");
        });

        tests.run("Callback and in-flight cancellation", [&] {
            auto reader = makeLibArchiveReader();
            succeeded(reader->open(zip), "Open cancellable ZIP");
            std::size_t count = 0;
            auto result = reader->enumerate([&](const auto&) { ++count; return false; });
            check(!result && result.error().code == ArchiveErrorCode::Cancelled && count == 1,
                  "Callback stops after first entry");
            std::stop_source source;
            count = 0;
            result = reader->enumerate([&](const auto&) { ++count; source.request_stop(); return true; },
                                       source.get_token());
            check(!result && result.error().code == ArchiveErrorCode::Cancelled && count == 1,
                  "Stop token stops before reading next entry");
            const auto renamed = fixtures.path / "cancelled-handle-check.zip";
            std::filesystem::rename(zip, renamed);
            std::filesystem::rename(renamed, zip);
            check(entriesIn(*reader).size() == 3, "Reader remains reusable after cancellation");
        });

        tests.run("Entry, pathname and metadata budgets", [&] {
            auto reader = makeLibArchiveReader({1, 64 * 1024, 512ULL * 1024 * 1024});
            succeeded(reader->open(zip), "Open bounded archive");
            std::size_t count = 0;
            auto result = reader->enumerate([&](const auto&) { ++count; return true; });
            check(!result && result.error().code == ArchiveErrorCode::ResourceLimitExceeded && count == 1,
                  "Enforce entry limit before callback");
            reader = makeLibArchiveReader({100, 2, 512ULL * 1024 * 1024});
            auto opened = reader->open(zip);
            check(!opened && opened.error().code == ArchiveErrorCode::ResourceLimitExceeded,
                  "Enforce pathname limit");
            reader = makeLibArchiveReader({100, 64 * 1024, 1});
            succeeded(reader->open(zip), "Open metadata-budget archive");
            result = reader->enumerate([](const auto&) { return true; });
            check(!result && result.error().code == ArchiveErrorCode::ResourceLimitExceeded,
                  "Enforce aggregate metadata budget");
        });

        tests.run("Traversal and links remain metadata only", [&] {
            const auto malicious = fixtures.path / "metadata-only.tar";
            createFixture(malicious, FixtureFormat::Tar,
                          {{"../evil.txt", "one"}, {"../../evil.txt", "two"}, {"/foo.txt", "three"},
                           {"C:\\evil.txt", "four"}, {"..\\..\\evil.exe", "five"},
                           {"folder/../../../evil.txt", "six"},
                           {"link", {}, EntryType::Symlink, "../../"},
                           {"hardlink", {}, EntryType::Hardlink, "../evil.txt"}});
            auto reader = makeLibArchiveReader();
            succeeded(reader->open(malicious), "Open malicious-name metadata fixture");
            const auto entries = entriesIn(*reader);
            check(entries.size() == 8, "Enumerate all untrusted paths without extraction");
            check(entries[0].path == "../evil.txt", "Preserve untrusted entry pathname");
            check(entries[6].type == EntryType::Symlink, "Symlink metadata type");
            check(entries[7].type == EntryType::Hardlink, "Hardlink metadata type");
            check(!std::filesystem::exists(fixtures.path / "evil.txt"), "No file was extracted");
        });

        tests.run("Truncated archive fails without unchecked EOF", [&] {
            const auto truncated = fixtures.path / "truncated.tar";
            createFixture(truncated, FixtureFormat::Tar,
                          {{"large.txt", std::string(200'000, 'x')}, {"tail.txt", "tail"}});
            std::filesystem::resize_file(truncated, 1024);
            auto reader = makeLibArchiveReader();
            auto opened = reader->open(truncated);
            if (opened) {
                auto result = reader->enumerate([](const auto&) { return true; });
                check(!result, "Truncated content cannot enumerate successfully");
                check(result.error().code == ArchiveErrorCode::CorruptedArchive ||
                      result.error().code == ArchiveErrorCode::IoError, "Truncation error code");
            } else {
                check(opened.error().code == ArchiveErrorCode::CorruptedArchive ||
                      opened.error().code == ArchiveErrorCode::IoError, "Truncated open error code");
            }
        });

        tests.run("Callback exception and empty callback release session", [&] {
            auto reader = makeLibArchiveReader();
            succeeded(reader->open(tar), "Open callback exception fixture");
            auto result = reader->enumerate([](const auto&) -> bool {
                throw std::runtime_error("fixture callback failure");
            });
            check(!result && result.error().code == ArchiveErrorCode::BackendError, "Callback exception code");
            check(!reader->enumerate({}), "Empty callback rejected");
            check(entriesIn(*reader).size() == 2, "Archive usable after callback exception");
        });

        return tests.finish();
    } catch (const std::exception& error) {
        std::cerr << "Archive test setup failed: " << error.what() << '\n';
        return 1;
    }
}
