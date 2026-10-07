#include "ananarchiver/libarchive_reader.h"
#include "ananarchiver/path_security.h"
#include "../security/extraction_fixtures.h"
#include "../security/windows_reparse_fixture.h"

#include <clocale>
#include <thread>

namespace {

using namespace extraction_tests;

std::unique_ptr<ArchiveReader> openedReader(const std::filesystem::path& archive) {
    auto reader = makeLibArchiveReader();
    succeeded(reader->open(archive), "Open extraction fixture");
    return reader;
}

ExtractionRequest requestFor(const std::filesystem::path& destination) {
    ExtractionRequest request;
    request.destination = destination;
    request.overwrite = OverwritePolicy::Overwrite;
    return request;
}

void failedWith(const Result<void>& result, ArchiveErrorCode code, const std::string& message) {
    check(!result, message + ": operation unexpectedly succeeded");
    check(result.error().code == code,
          message + ": " + result.error().message + "; " + result.error().backendMessage);
}

bool canCreateSymlinks(const std::filesystem::path& root) {
    const auto source = root / "symlink-privilege-source";
    const auto link = root / "symlink-privilege-link";
    std::filesystem::create_directory(source);
    std::error_code error;
    std::filesystem::create_directory_symlink(source, link, error);
    const bool supported = !error;
    std::filesystem::remove(link, error);
    std::filesystem::remove(source, error);
    return supported;
}

} // namespace

int main() {
    try {
#ifdef _WIN32
        check(std::setlocale(LC_CTYPE, ".UTF-8") != nullptr, "Initialize fixture process UTF-8 locale");
#elif defined(__APPLE__)
        check(std::setlocale(LC_CTYPE, "en_US.UTF-8") != nullptr, "Initialize fixture process UTF-8 locale");
#else
        check(std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr, "Initialize fixture process UTF-8 locale");
#endif
        TemporaryDirectory fixtures;
        Tests tests;
        const bool symlinksSupported = canCreateSymlinks(fixtures.path);
        if (!symlinksSupported) {
            std::cout << "Native symlink creation unavailable; creation-only symlink cases skipped\n";
        }

        tests.run("Portable paths reject traversal, Windows roots and filename aliases", [&] {
            const std::vector<std::string> malicious{
                "../evil.txt", "../../evil.txt", "/foo.txt", "C:\\evil.txt", "..\\..\\evil.exe",
                "folder/../../../evil.txt", "\\\\server\\share\\evil.txt", "C:evil.txt",
                "//server/share/evil.txt", "safe.txt:stream", "folder/NUL.txt", "CON", "aux.zip",
                "COM1.dat", "LPT9", "COM¹.txt", "LPT²", "CONIN$", "CONOUT$.txt",
                "folder./file", "folder /file", "file.", "file "
            };
            for (const auto& path : malicious) {
                auto result = secureArchivePath(path);
                check(!result && result.error().code == ArchiveErrorCode::UnsafePath,
                      "Reject unsafe portable path: " + path);
            }
            auto normalized = secureArchivePath("nested\\portable.txt");
            succeeded(normalized, "Accept backslash-separated safe path");
            check(*normalized == pathFromUtf8("nested/portable.txt"), "Normalize portable separators");
            check(!secureArchivePath(""), "Reject empty pathname");
            check(!secureArchivePath(std::string{"valid\0hidden", 12}), "Reject embedded pathname NUL");
            const std::string invalidUtf8{"bad\xc0\xaf.txt", 9};
            check(!secureArchivePath(invalidUtf8), "Reject invalid UTF-8 pathname");
            check(!secureArchivePath("a/b/c.txt", 1024, 2), "Enforce path depth before filesystem IO");
            check(!secureArchivePath("a-very-long-filename.txt", 5), "Enforce pathname byte budget");
        });

        tests.run("Contained link normalization never permits root escape", [&] {
            auto normalized = secureLinkTarget(pathFromUtf8("folder/link"), "../safe.txt", false);
            succeeded(normalized, "Symlink can refer to safe root sibling");
            check(pathToUtf8(*normalized) == "safe.txt", "Resolve link relative to its parent");
            for (const std::string target : {"../../evil", "/outside", "C:\\outside",
                                             "\\\\server\\outside", "safe.txt:stream"}) {
                check(!secureLinkTarget(pathFromUtf8("folder/link"), target, false),
                      "Reject symlink target escape: " + target);
                check(!secureLinkTarget(pathFromUtf8("folder/link"), target, true),
                      "Reject hardlink target escape: " + target);
            }
            check(!secureLinkTarget(pathFromUtf8("link"), "../outside", false),
                  "Root symlink cannot resolve parent");
        });

        tests.run("ZIP, TAR, TAR.GZ and 7z stream payloads with actual byte progress", [&] {
            const std::string payload(600'000, 'p');
            std::size_t fixtureNumber = 0;
            for (const auto format : {FixtureFormat::Zip, FixtureFormat::Tar,
                                     FixtureFormat::TarGzip, FixtureFormat::SevenZip}) {
                const auto stem = "stream-" + std::to_string(fixtureNumber++);
                const auto archive = fixtures.path / (stem + ".data");
                const auto destination = fixtures.path / (stem + "-out");
                createFixture(archive, format, {{"folder/", {}, EntryType::Directory},
                                              {"folder/large.bin", payload}, {"tail.txt", "tail"}});
                auto reader = openedReader(archive);
                auto request = requestFor(destination);
                request.totalBytes = 7; // Estimates never limit or replace actual accounting.
                request.totalEntries = 3;
                std::uint64_t lastBytes = 0;
                std::uint64_t lastEntries = 0;
                std::size_t dataUpdates = 0;
                auto result = reader->extract(request, [&](const ExtractionProgress& progress) {
                    check(progress.processedBytes >= lastBytes, "Byte progress is monotonic");
                    check(progress.processedEntries >= lastEntries, "Entry progress is monotonic");
                    if (progress.processedBytes > lastBytes) {
                        ++dataUpdates;
                    }
                    lastBytes = progress.processedBytes;
                    lastEntries = progress.processedEntries;
                    return true;
                });
                succeeded(result, "Extract stream format");
                check(readFile(destination / "folder/large.bin") == payload, "Large payload preserved");
                check(readFile(destination / "tail.txt") == "tail", "Tail payload preserved");
                check(lastBytes == payload.size() + 4, "Progress reports exact decompressed byte count");
                check(lastEntries == 3, "Progress counts finished directory and file entries");
                check(dataUpdates > 1, "Payload is reported in streaming batches");
                check(filesIn(destination) == std::vector<std::string>{"folder/large.bin", "tail.txt"},
                      "No staging or backup files remain after success");
            }
        });

        tests.run("Selected identities distinguish duplicate names and preserve empty selection", [&] {
            const auto archive = fixtures.path / "selection.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"same.txt", "first"}, {"same.txt", "second"},
                           {"folder/before.txt", "before"}, {"folder/", {}, EntryType::Directory},
                           {"folder/child.txt", "child"}});
            auto reader = openedReader(archive);
            std::vector<std::uint64_t> indices;
            succeeded(reader->enumerate([&](const ArchiveEntry& entry) {
                indices.push_back(entry.index);
                return true;
            }), "Enumerate stable ordinal identities");
            check(indices == std::vector<std::uint64_t>{0, 1, 2, 3, 4}, "Zero-based stable metadata identities");
            auto request = requestFor(fixtures.path / "selection-second");
            request.selectedIndices = std::vector<std::uint64_t>{1};
            succeeded(reader->extract(request), "Extract second duplicate only");
            check(readFile(request.destination / "same.txt") == "second", "Second duplicate selected by index");
            check(filesIn(request.destination) == std::vector<std::string>{"same.txt"}, "No unselected files");
            request.destination = fixtures.path / "selection-directory";
            request.selectedIndices = std::vector<std::uint64_t>{3};
            succeeded(reader->extract(request), "Extract selected directory and descendants");
            check(std::filesystem::is_directory(request.destination / "folder"), "Selected directory created");
            check(readFile(request.destination / "folder/before.txt") == "before", "Directory includes child before its header");
            check(readFile(request.destination / "folder/child.txt") == "child", "Directory includes child after its header");
            check(filesIn(request.destination) == std::vector<std::string>{"folder/before.txt", "folder/child.txt"},
                  "Directory selection includes only contained descendants");
            request.destination = fixtures.path / "selection-empty";
            request.selectedIndices = std::vector<std::uint64_t>{};
            succeeded(reader->extract(request), "Empty selection is valid");
            check(filesIn(request.destination).empty(), "Empty selection writes nothing");
            request.destination = fixtures.path / "selection-all";
            request.selectedIndices.reset();
            succeeded(reader->extract(request), "Extract all duplicates explicitly overwriting");
            check(readFile(request.destination / "same.txt") == "second", "All selection keeps last duplicate");
            check(readFile(request.destination / "folder/child.txt") == "child", "All selection includes descendants");
            request.destination = fixtures.path / "selection-stale";
            request.selectedIndices = std::vector<std::uint64_t>{999};
            failedWith(reader->extract(request), ArchiveErrorCode::InvalidArchive,
                       "Stale selected identity cannot silently succeed");
            check(filesIn(request.destination).empty(), "Stale identity creates no output");
        });

        tests.run("Unicode paths and paths beyond Windows legacy MAX_PATH", [&] {
            const auto archive = fixtures.path / pathFromUtf8("安安-こんにちは-🐈.tar");
            const std::string unicode = "中文/こんにちは/한글/可可🐄🐈/café.txt";
            const std::string longEntry = std::string(95, 'a') + "/" + std::string(95, 'b') +
                                          "/" + std::string(95, 'c') + "/long.txt";
            createFixture(archive, FixtureFormat::Tar, {{unicode, "unicode"}, {longEntry, "long"}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "unicode-out");
            succeeded(reader->extract(request), "Extract Unicode and long paths");
            check(readFile(request.destination / pathFromUtf8(unicode)) == "unicode", "Unicode file payload");
            check(readFile(request.destination / pathFromUtf8(longEntry)) == "long",
                  "Long native output path supports streamed file");
        });

        tests.run("Each malicious archive path is rejected without escaped or partial output", [&] {
            const std::vector<std::string> malicious{
                "../evil.txt", "../../evil.txt", "/foo.txt", "C:\\evil.txt", "..\\..\\evil.exe",
                "folder/../../../evil.txt", "\\\\server\\share\\evil.txt", "C:evil.txt",
                "file.txt:payload", "NUL.txt", "folder./evil.txt", "folder /evil.txt"
            };
            std::size_t fixtureNumber = 0;
            for (const auto& pathname : malicious) {
                const auto stem = "escape-" + std::to_string(fixtureNumber++);
                const auto archive = fixtures.path / (stem + ".tar");
                const auto destination = fixtures.path / (stem + "-out");
                createFixture(archive, FixtureFormat::Tar,
                              {{"safe.txt", "must rollback"}, {pathname, "untrusted"}});
                auto reader = openedReader(archive);
                auto result = reader->extract(requestFor(destination));
                failedWith(result, ArchiveErrorCode::UnsafePath, "Reject archive path " + pathname);
                check(filesIn(destination).empty(), "Unsafe extraction cleans newly created files");
                check(!std::filesystem::exists(fixtures.path / "evil.txt"), "No parent escape written");
            }
        });

        tests.run("Archive links cannot escape under Safe or Preserve", [&] {
            std::size_t fixtureNumber = 0;
            for (const auto type : {EntryType::Symlink, EntryType::Hardlink}) {
                for (const auto policy : {LinkPolicy::Safe, LinkPolicy::Preserve}) {
                    const auto stem = "link-escape-" + std::to_string(fixtureNumber++);
                    const auto archive = fixtures.path / (stem + ".tar");
                    createFixture(archive, FixtureFormat::Tar,
                                  {{"safe.txt", "safe"}, {"escape", {}, type, "../outside.txt"},
                                   {"escape/evil.txt", "evil"}});
                    auto reader = openedReader(archive);
                    auto request = requestFor(fixtures.path / (stem + "-out"));
                    request.links = policy;
                    auto result = reader->extract(request);
                    failedWith(result, ArchiveErrorCode::UnsafePath, "Reject escaping archive link");
                    check(filesIn(request.destination).empty(), "Link escape rolls back output");
                    check(!std::filesystem::exists(fixtures.path / "outside.txt"), "No escaped link target");
                }
            }
        });

        tests.run("A contained archive symlink never becomes a writable output ancestor", [&] {
            const auto archive = fixtures.path / "linked-ancestor.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"target/", {}, EntryType::Directory},
                           {"redirect", {}, EntryType::Symlink, "target"},
                           {"redirect/child.txt", "must not follow link"}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "linked-ancestor-out");
            auto result = reader->extract(request);
            failedWith(result, ArchiveErrorCode::UnsafePath, "Never follow archive links for subsequent files");
            check(filesIn(request.destination).empty(), "Linked ancestor rollback removes partial output");
        });

        tests.run("Skip ignores links while safe internal hardlinks retain target contents", [&] {
            const auto archive = fixtures.path / "internal-links.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"original.txt", "link contents"}, {"hard.txt", {}, EntryType::Hardlink, "original.txt"},
                           {"sym.txt", {}, EntryType::Symlink, "original.txt"}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "internal-links-skip");
            request.links = LinkPolicy::Skip;
            succeeded(reader->extract(request), "Skip archive links");
            check(filesIn(request.destination) == std::vector<std::string>{"original.txt"}, "Only regular file extracted");
            request.destination = fixtures.path / "internal-hardlink-safe";
            request.selectedIndices = std::vector<std::uint64_t>{0, 1};
            request.links = LinkPolicy::Safe;
            succeeded(reader->extract(request), "Create contained safe hardlink");
            check(readFile(request.destination / "hard.txt") == "link contents", "Hardlink contents");
            check(std::filesystem::equivalent(request.destination / "original.txt", request.destination / "hard.txt"),
                  "Hardlink shares contained target");
            if (symlinksSupported) {
                request.destination = fixtures.path / "internal-symlink-safe";
                request.selectedIndices.reset();
                succeeded(reader->extract(request), "Create contained safe symlink");
                check(std::filesystem::is_symlink(request.destination / "sym.txt"), "Archive symlink preserved");
                check(readFile(request.destination / "sym.txt") == "link contents", "Safe symlink contents");
            }
        });

        tests.run("Preserve allows contained dangling symlinks only when supported", [&] {
            if (!symlinksSupported) {
                return;
            }
            const auto archive = fixtures.path / "dangling-link.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"dangling", {}, EntryType::Symlink, "missing.txt"},
                           {"dangling-nested", {}, EntryType::Symlink, "missing-directory/missing.txt"}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "dangling-safe");
            check(!reader->extract(request), "Safe refuses target that is not a regular file or directory");
            request.destination = fixtures.path / "dangling-preserved";
            request.links = LinkPolicy::Preserve;
            succeeded(reader->extract(request), "Preserve contained dangling link");
            check(std::filesystem::is_symlink(request.destination / "dangling"), "Contained dangling link created");
            check(!std::filesystem::exists(request.destination / "dangling"), "Dangling target remains absent");
            check(std::filesystem::is_symlink(request.destination / "dangling-nested"), "Contained nested dangling link created");
            check(!std::filesystem::exists(request.destination / "missing-directory"), "Preserve does not create missing target directories");
        });

        tests.run("Preexisting directory symlinks and extraction-root links cannot redirect writes", [&] {
            if (!symlinksSupported) {
                return;
            }
            const auto archive = fixtures.path / "preexisting-symlink.tar";
            createFixture(archive, FixtureFormat::Tar, {{"redirect/evil.txt", "evil"}});
            const auto destination = fixtures.path / "preexisting-link-out";
            const auto outside = fixtures.path / "outside-link-target";
            std::filesystem::create_directories(destination);
            std::filesystem::create_directory(outside);
            std::filesystem::create_directory_symlink(outside, destination / "redirect");
            auto reader = openedReader(archive);
            auto result = reader->extract(requestFor(destination));
            failedWith(result, ArchiveErrorCode::UnsafePath, "Reject linked output ancestor");
            check(!std::filesystem::exists(outside / "evil.txt"), "Outside directory unchanged");
            const auto rootLink = fixtures.path / "root-link";
            std::filesystem::create_directory_symlink(outside, rootLink);
            result = reader->extract(requestFor(rootLink));
            failedWith(result, ArchiveErrorCode::UnsafePath, "Reject symlink extraction root");
            check(filesIn(outside).empty(), "Root link cannot write outside");
            const auto finalArchive = fixtures.path / "preexisting-final-symlink.tar";
            createFixture(finalArchive, FixtureFormat::Tar, {{"same.txt", "replacement"}});
            const auto outsideFile = outside / "original.txt";
            writeFile(outsideFile, "outside original");
            std::filesystem::create_symlink(outsideFile, destination / "same.txt");
            reader = openedReader(finalArchive);
            failedWith(reader->extract(requestFor(destination)), ArchiveErrorCode::UnsafePath,
                       "Reject existing symlink final output");
            check(readFile(outsideFile) == "outside original", "Final symlink never truncates outside target");
        });

        tests.run("An ancestor symlink introduced during streaming cannot redirect installation", [&] {
            if (!symlinksSupported) {
                return;
            }
            const auto archive = fixtures.path / "midstream-symlink.tar";
            createFixture(archive, FixtureFormat::Tar, {{"folder/payload.bin", std::string(600'000, 'x')}});
            const auto outside = fixtures.path / "midstream-outside";
            std::filesystem::create_directory(outside);
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "midstream-symlink-out");
            bool introduced = false;
            auto result = reader->extract(request, [&](const ExtractionProgress& progress) {
                if (progress.processedBytes > 0 && !introduced) {
                    std::filesystem::create_directory_symlink(outside, request.destination / "folder");
                    introduced = true;
                }
                return true;
            });
            failedWith(result, ArchiveErrorCode::CleanupFailed,
                       "Unsafe ancestor and externally populated root remain diagnostic");
            check(result.error().message.find("; extraction cleanup") > 0 &&
                  !result.error().backendMessage.empty(),
                  "Cleanup failure preserves the original error and filesystem detail");
            check(introduced, "Symlink appeared after streamed output began");
            check(filesIn(outside).empty(), "Midstream link target remains unchanged");
            // The newly introduced link is external to the extraction transaction.
            // It is removed by the fixture, never recursively by core rollback.
            std::filesystem::remove(request.destination / "folder");
            check(filesIn(request.destination).empty(), "No extraction staging files remain");
        });

        tests.run("Overwriting a preexisting hardlink never modifies its outside inode", [&] {
            const auto archive = fixtures.path / "preexisting-hardlink.tar";
            createFixture(archive, FixtureFormat::Tar, {{"output.txt", "replacement"}});
            const auto destination = fixtures.path / "preexisting-hardlink-out";
            const auto outside = fixtures.path / "hardlink-outside.txt";
            std::filesystem::create_directory(destination);
            writeFile(outside, "outside must remain");
            std::filesystem::create_hard_link(outside, destination / "output.txt");
            auto reader = openedReader(archive);
            auto result = reader->extract(requestFor(destination));
            succeeded(result, "Atomically replace hardlink output");
            check(readFile(outside) == "outside must remain", "Never truncate a linked outside inode");
            check(readFile(destination / "output.txt") == "replacement", "Atomic replacement separates hardlink");
            check(!std::filesystem::equivalent(outside, destination / "output.txt"), "Replacement owns new inode");
        });

#ifdef _WIN32
        tests.run("Windows junction reparse points cannot redirect root or ancestor writes", [&] {
            const auto archive = fixtures.path / "junction-escape.tar";
            createFixture(archive, FixtureFormat::Tar, {{"redirect/evil.txt", "evil"}});
            const auto destination = fixtures.path / "junction-out";
            const auto outside = fixtures.path / "junction-outside";
            std::filesystem::create_directory(destination);
            std::filesystem::create_directory(outside);
            DWORD error = ERROR_SUCCESS;
            if (!createDirectoryJunction(destination / "redirect", outside, error)) {
                check(error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION,
                      "Create junction fixture (Windows error " + std::to_string(error) + ")");
                std::cout << "Mount-point reparse fixtures unavailable on this filesystem\n";
                return;
            }
            auto reader = openedReader(archive);
            failedWith(reader->extract(requestFor(destination)), ArchiveErrorCode::UnsafePath,
                       "Reject junction output ancestor");
            check(filesIn(outside).empty(), "Junction target remains unchanged");
            const auto rootJunction = fixtures.path / "junction-root";
            check(createDirectoryJunction(rootJunction, outside, error), "Create extraction root junction");
            failedWith(reader->extract(requestFor(rootJunction)), ArchiveErrorCode::UnsafePath,
                       "Reject junction extraction root");
            check(filesIn(outside).empty(), "Root junction target remains unchanged");
        });
#endif

        tests.run("Overwrite Ask, Skip, Rename and Overwrite preserve intended outputs", [&] {
            const auto archive = fixtures.path / "overwrite.tar";
            createFixture(archive, FixtureFormat::Tar, {{"same.txt", "archive contents"}});
            auto reader = openedReader(archive);
            const auto prepare = [&](const std::string& name) {
                auto request = requestFor(fixtures.path / name);
                std::filesystem::create_directory(request.destination);
                writeFile(request.destination / "same.txt", "original contents");
                return request;
            };
            auto request = prepare("overwrite-ask");
            request.overwrite = OverwritePolicy::Ask;
            failedWith(reader->extract(request), ArchiveErrorCode::OutputExists, "Ask without a resolver reports conflict");
            check(readFile(request.destination / "same.txt") == "original contents", "Unresolved Ask preserves original");
            std::size_t conflicts = 0;
            request.onConflict = [&](const ExtractionConflict& conflict) {
                ++conflicts;
                check(conflict.entry.path == "same.txt" && conflict.entry.index == 0, "Conflict entry metadata");
                check(conflict.destination.filename() == "same.txt", "Conflict destination metadata");
                return OverwritePolicy::Overwrite;
            };
            succeeded(reader->extract(request), "Resolve Ask through callback");
            check(conflicts == 1 && readFile(request.destination / "same.txt") == "archive contents", "Ask decision applied once");
            request = prepare("overwrite-skip");
            request.overwrite = OverwritePolicy::Skip;
            succeeded(reader->extract(request), "Skip existing output");
            check(readFile(request.destination / "same.txt") == "original contents", "Skip preserves original");
            request = prepare("overwrite-rename");
            request.overwrite = OverwritePolicy::Rename;
            succeeded(reader->extract(request), "Rename extracted conflicting output");
            check(readFile(request.destination / "same.txt") == "original contents", "Rename preserves original name");
            const auto renamedFiles = filesIn(request.destination);
            check(renamedFiles.size() == 2, "Rename creates one additional output");
            bool foundPayload = false;
            for (const auto& name : renamedFiles) {
                foundPayload = foundPayload || readFile(request.destination / pathFromUtf8(name)) == "archive contents";
            }
            check(foundPayload, "Renamed output contains archive payload");
            request = prepare("overwrite-explicit");
            succeeded(reader->extract(request), "Explicitly overwrite output");
            check(readFile(request.destination / "same.txt") == "archive contents", "Overwrite applies content");
            check(filesIn(request.destination) == std::vector<std::string>{"same.txt"}, "No successful overwrite backup leaked");
        });

        tests.run("OverwriteIfNewer compares entry and destination timestamps", [&] {
            const auto archive = fixtures.path / "overwrite-newer.tar";
            createFixture(archive, FixtureFormat::Tar, {{"same.txt", "from archive"}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "overwrite-newer-out");
            request.overwrite = OverwritePolicy::OverwriteIfNewer;
            std::filesystem::create_directory(request.destination);
            const auto output = request.destination / "same.txt";
            writeFile(output, "recent original");
            std::filesystem::last_write_time(output, std::filesystem::file_time_type::clock::now());
            succeeded(reader->extract(request), "Skip older archive content");
            check(readFile(output) == "recent original", "Newer existing timestamp wins");
            const auto oldTime = std::filesystem::file_time_type::clock::now() -
                                 std::chrono::hours{24 * 365 * 30};
            std::filesystem::last_write_time(output, oldTime);
            succeeded(reader->extract(request), "Overwrite older destination content");
            check(readFile(output) == "from archive", "Newer archive timestamp wins");
        });

        tests.run("Directory conflicts redirect or skip complete subtrees independently of header order", [&] {
            std::size_t fixtureNumber = 0;
            for (const bool childBeforeDirectory : {false, true}) {
                const auto stem = "directory-conflict-" + std::to_string(fixtureNumber++);
                const auto archive = fixtures.path / (stem + ".tar");
                std::vector<FixtureEntry> entries;
                if (childBeforeDirectory) {
                    entries.emplace_back("folder/child.txt", "child contents");
                    entries.emplace_back("folder/", "", EntryType::Directory);
                } else {
                    entries.emplace_back("folder/", "", EntryType::Directory);
                    entries.emplace_back("folder/child.txt", "child contents");
                }
                if (symlinksSupported) {
                    entries.emplace_back("folder/inside-link", "", EntryType::Symlink, "child.txt");
                }
                entries.emplace_back("tail.txt", "tail contents");
                createFixture(archive, FixtureFormat::Tar, entries);
                auto reader = openedReader(archive);
                const auto prepare = [&](const std::string& suffix) {
                    auto request = requestFor(fixtures.path / (stem + suffix));
                    std::filesystem::create_directory(request.destination);
                    writeFile(request.destination / "folder", "existing file contents");
                    return request;
                };
                const auto checkRenamed = [&](const ExtractionRequest& request) {
                    check(readFile(request.destination / "folder") == "existing file contents",
                          "Directory rename retains conflicting existing regular file");
                    check(readFile(request.destination / "folder (1)/child.txt") == "child contents",
                          "Directory rename redirects all descendant outputs");
                    check(readFile(request.destination / "tail.txt") == "tail contents",
                          "Unrelated output still extracted");
                    std::vector<std::string> expected{"folder", "folder (1)/child.txt"};
                    if (symlinksSupported) {
                        check(std::filesystem::is_symlink(request.destination / "folder (1)/inside-link"),
                              "Directory rename relocates contained symlink");
                        check(readFile(request.destination / "folder (1)/inside-link") == "child contents",
                              "Renamed directory symlink target follows relocated subtree");
                        expected.emplace_back("folder (1)/inside-link");
                    }
                    expected.emplace_back("tail.txt");
                    check(filesIn(request.destination) == expected, "Directory rename leaves no backups or staging files");
                };
                auto request = prepare("-rename-out");
                request.overwrite = OverwritePolicy::Rename;
                succeeded(reader->extract(request), "Rename conflicting directory subtree");
                checkRenamed(request);
                request = prepare("-skip-out");
                request.overwrite = OverwritePolicy::Skip;
                succeeded(reader->extract(request), "Skip conflicting directory subtree");
                check(readFile(request.destination / "folder") == "existing file contents",
                      "Directory skip retains conflicting existing file");
                check(filesIn(request.destination) == std::vector<std::string>{"folder", "tail.txt"},
                      "Directory skip excludes all descendant files and links");
                request = prepare("-ask-out");
                request.overwrite = OverwritePolicy::Ask;
                std::size_t conflicts = 0;
                request.onConflict = [&](const ExtractionConflict& conflict) {
                    ++conflicts;
                    check(conflict.destination.filename() == "folder", "Directory conflict identifies conflicting ancestor");
                    return OverwritePolicy::Rename;
                };
                succeeded(reader->extract(request), "Ask resolves conflicting directory with Rename");
                check(conflicts == 1, "A directory conflict decision applies once to its subtree");
                checkRenamed(request);
            }
        });

        tests.run("Entry, path, depth and expanded-byte limits bound extraction", [&] {
            const auto archive = fixtures.path / "bounded.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"first.txt", "12345678"}, {"nested/deep/second.txt", "abcdefgh"}});
            auto reader = openedReader(archive);
            std::size_t fixtureNumber = 0;
            const auto rejected = [&](ExtractionLimits limits) {
                auto request = requestFor(fixtures.path / ("bounded-out-" + std::to_string(fixtureNumber++)));
                request.limits = limits;
                failedWith(reader->extract(request), ArchiveErrorCode::ResourceLimitExceeded, "Enforce extraction resource budget");
                check(filesIn(request.destination).empty(), "Budget failure leaves no incomplete files");
            };
            ExtractionLimits limits;
            limits.maxEntries = 1;
            rejected(limits);
            limits = {};
            limits.maxPathBytes = 10;
            rejected(limits);
            limits = {};
            limits.maxDepth = 2;
            rejected(limits);
            limits = {};
            limits.maxEntryBytes = 7;
            rejected(limits);
            limits = {};
            limits.maxTotalBytes = 15;
            rejected(limits);
            limits = {};
            limits.maxMetadataBytes = 1;
            rejected(limits);
        });

        tests.run("Unknown-size GZIP streams enforce actual expanded-byte limits", [&] {
            const auto archive = fixtures.path / "unknown-size.gz";
            createFixture(archive, FixtureFormat::Gzip, {{"payload.bin", std::string(800'000, 'x')}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "unknown-size-out");
            request.limits.maxEntryBytes = 200'000;
            auto result = reader->extract(request);
            failedWith(result, ArchiveErrorCode::ResourceLimitExceeded, "Limit an unknown declared stream size by actual bytes");
            check(filesIn(request.destination).empty(), "Unknown-size bomb output cleaned");
            request.destination = fixtures.path / "unknown-total-out";
            request.limits.maxEntryBytes = 1'000'000;
            request.limits.maxTotalBytes = 200'000;
            result = reader->extract(request);
            failedWith(result, ArchiveErrorCode::ResourceLimitExceeded, "Total actual expanded-byte limit");
            check(filesIn(request.destination).empty(), "Total-byte failure cleaned");
            request.destination = fixtures.path / "unknown-size-success";
            request.limits = {};
            std::uint64_t actualBytes = 0;
            bool observedUnknownTotal = false;
            succeeded(reader->extract(request, [&](const ExtractionProgress& progress) {
                actualBytes = progress.processedBytes;
                if (progress.totalBytes) {
                    check(*progress.totalBytes == 800'000 && progress.processedBytes == 800'000,
                          "Raw-stream total becomes known only after all bytes are processed");
                } else {
                    observedUnknownTotal = true;
                }
                return true;
            }), "Extract full unknown-size raw GZIP stream");
            const auto outputs = filesIn(request.destination);
            check(outputs.size() == 1 && readFile(request.destination / pathFromUtf8(outputs[0])) == std::string(800'000, 'x'),
                  "Raw GZIP has one complete payload");
            check(actualBytes == 800'000, "Unknown-size stream reports actual output bytes");
            check(observedUnknownTotal, "Raw-stream progress initially has unknown total");
        });

        tests.run("Sparse PAX streams preserve holes and enforce logical-byte budgets", [&] {
            constexpr std::uint64_t logicalSize = 1024 * 1024;
            const std::vector<SparseExtent> extents{{4096, "first"}, {logicalSize - 16, "taildata"}};
            const auto archive = fixtures.path / "sparse-logical.tar";
            createSparseFixture(archive, "sparse.bin", logicalSize, extents);
            check(std::filesystem::file_size(archive) < 32 * 1024,
                  "PAX writer omitted logical hole bytes from fixture archive");
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "sparse-success-out");
            request.totalBytes = logicalSize;
            std::uint64_t actualBytes = 0;
            succeeded(reader->extract(request, [&](const ExtractionProgress& progress) {
                actualBytes = progress.processedBytes;
                return true;
            }), "Extract sparse PAX payload");
            const auto output = request.destination / "sparse.bin";
            check(std::filesystem::file_size(output) == logicalSize, "Sparse output retains its logical EOF including trailing hole");
            std::string expected(static_cast<std::size_t>(logicalSize), '\0');
            for (const auto& extent : extents) {
                expected.replace(static_cast<std::size_t>(extent.offset), extent.data.size(), extent.data);
            }
            check(readFile(output) == expected, "Sparse holes read as zeros and nonzero extents retain contents");
            check(actualBytes == 13 && actualBytes < logicalSize, "Actual progress counts data blocks and excludes sparse holes");
            request.destination = fixtures.path / "sparse-entry-limit-out";
            request.limits.maxEntryBytes = logicalSize - 1;
            failedWith(reader->extract(request), ArchiveErrorCode::ResourceLimitExceeded,
                       "Per-entry resource limit charges sparse logical size rather than tiny payload");
            check(filesIn(request.destination).empty(), "Sparse entry limit cleans staging output");
            request.destination = fixtures.path / "sparse-total-limit-out";
            request.limits = {};
            request.limits.maxTotalBytes = logicalSize - 1;
            failedWith(reader->extract(request), ArchiveErrorCode::ResourceLimitExceeded,
                       "Total resource limit charges sparse logical size rather than tiny payload");
            check(filesIn(request.destination).empty(), "Sparse total limit cleans staging output");
        });

        tests.run("Elapsed-time budget cancels a stream at the next cooperative boundary", [&] {
            const auto archive = fixtures.path / "elapsed.tar";
            createFixture(archive, FixtureFormat::Tar, {{"large.bin", std::string(1'000'000, 'x')}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "elapsed-out");
            request.limits.maxElapsed = std::chrono::milliseconds{100};
            bool waited = false;
            auto result = reader->extract(request, [&](const ExtractionProgress& progress) {
                if (progress.processedBytes > 0 && !waited) {
                    waited = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds{150});
                }
                return true;
            });
            failedWith(result, ArchiveErrorCode::ResourceLimitExceeded, "Enforce elapsed extraction budget");
            check(waited, "Budget exhausted during real streamed progress");
            check(filesIn(request.destination).empty(), "Elapsed budget cleans partial stream");
        });

        tests.run("Truncated TAR and bad ZIP CRC fail and restore overwritten originals", [&] {
            const auto truncated = fixtures.path / "truncated-extract.tar";
            createFixture(truncated, FixtureFormat::Tar,
                          {{"same.txt", "new first"}, {"large.bin", std::string(600'000, 'x')}});
            std::filesystem::resize_file(truncated, 4096);
            const auto badCrc = fixtures.path / "bad-crc-extract.zip";
            createFixture(badCrc, FixtureFormat::Zip, {{"same.txt", std::string(600'000, 'x')}});
            corruptZipPayload(badCrc);
            std::size_t fixtureNumber = 0;
            for (const auto& archive : {truncated, badCrc}) {
                auto reader = openedReader(archive);
                auto request = requestFor(fixtures.path / ("corrupt-out-" + std::to_string(fixtureNumber++)));
                std::filesystem::create_directory(request.destination);
                writeFile(request.destination / "same.txt", "original contents");
                auto result = reader->extract(request);
                check(!result && (result.error().code == ArchiveErrorCode::CorruptedArchive ||
                                  result.error().code == ArchiveErrorCode::IoError),
                      "Detect corrupt payload during extraction");
                check(readFile(request.destination / "same.txt") == "original contents", "Corruption restores original overwrite target");
                check(filesIn(request.destination) == std::vector<std::string>{"same.txt"}, "No failed payload, backup or staging file remains");
            }
            auto reader = openedReader(badCrc);
            auto request = requestFor(fixtures.path / "crc-skip-existing-out");
            std::filesystem::create_directory(request.destination);
            writeFile(request.destination / "same.txt", "original contents");
            request.overwrite = OverwritePolicy::Skip;
            failedWith(reader->extract(request), ArchiveErrorCode::CorruptedArchive,
                       "Overwrite Skip still verifies archived payload integrity");
            check(readFile(request.destination / "same.txt") == "original contents", "Skipped corrupt payload preserves original");
        });

        tests.run("Unselected payload corruption is verified before any selected output is committed", [&] {
            const auto archive = fixtures.path / "unselected-crc.zip";
            createFixture(archive, FixtureFormat::Zip,
                          {{"unselected.txt", std::string(300'000, 'x')}, {"selected.txt", "selected contents"}});
            corruptZipPayload(archive);
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "unselected-crc-out");
            request.selectedIndices = std::vector<std::uint64_t>{1};
            failedWith(reader->extract(request), ArchiveErrorCode::CorruptedArchive,
                       "Unselected decoded payload must pass CRC verification");
            check(filesIn(request.destination).empty(), "Corrupt unselected payload prevents selected commit");
        });

        tests.run("Unsupported ZIP compression returns a structured error and leaves no output", [&] {
            const auto archive = fixtures.path / "unsupported-compression.zip";
            createFixture(archive, FixtureFormat::Zip, {{"payload.bin", "fixture payload"}});
            setUnsupportedZipCompression(archive);
            auto reader = makeLibArchiveReader();
            auto opened = reader->open(archive);
            const auto destination = fixtures.path / "unsupported-compression-out";
            if (opened) {
                auto result = reader->extract(requestFor(destination));
                failedWith(result, ArchiveErrorCode::UnsupportedCompression, "Unsupported payload codec");
            } else {
                check(opened.error().code == ArchiveErrorCode::UnsupportedCompression,
                      "Unsupported codec is diagnosed during open");
            }
            check(filesIn(destination).empty(), "Unsupported codec leaves no output or staging files");
        });

        tests.run("Encrypted ZIP distinguishes missing, wrong, correct and damaged passwords/data", [&] {
            const auto archive = fixtures.path / "password-extract.zip";
            createFixture(archive, FixtureFormat::Zip, {{"secret.txt", std::string(400'000, 's')}}, true);
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "password-out");
            auto result = reader->extract(request);
            failedWith(result, ArchiveErrorCode::PasswordRequired, "Missing password is explicit");
            check(filesIn(request.destination).empty(), "Missing password leaves no payload");
            request.password = "incorrect-test-password";
            result = reader->extract(request);
            failedWith(result, ArchiveErrorCode::WrongPassword, "Wrong password is explicit");
            check((result.error().message + result.error().backendMessage).find(*request.password) == std::string::npos,
                  "Password never appears in structured errors");
            check(filesIn(request.destination).empty(), "Wrong password leaves no payload");
            request.password = "test-only-passphrase";
            succeeded(reader->extract(request), "Correct password streams encrypted entry");
            check(readFile(request.destination / "secret.txt") == std::string(400'000, 's'), "Encrypted payload preserved");
            const auto damaged = fixtures.path / "password-damaged.zip";
            std::filesystem::copy_file(archive, damaged);
            // AES-256 payload starts after the 16-byte salt and 2-byte password verifier.
            corruptZipPayload(damaged, 32);
            reader = openedReader(damaged);
            request.destination = fixtures.path / "password-damaged-out";
            result = reader->extract(request);
            check(!result, "Invalid encrypted data cannot report successful extraction");
            check(result.error().code == ArchiveErrorCode::CorruptedArchive ||
                  result.error().code == ArchiveErrorCode::WrongPassword,
                  "Invalid encrypted data must return a corruption/password error: " +
                  result.error().message + "; " + result.error().backendMessage);
            check(filesIn(request.destination).empty(), "Invalid encrypted data leaves no output");
        });

        tests.run("Callback and stop-token cancellation clean partial files and preserve originals", [&] {
            const auto archive = fixtures.path / "cancel-extract.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"same.txt", "replacement"}, {"large.bin", std::string(2'000'000, 'x')}});
            auto reader = openedReader(archive);
            for (const bool tokenCancellation : {false, true}) {
                auto request = requestFor(fixtures.path / (tokenCancellation ? "cancel-token-out" : "cancel-callback-out"));
                std::filesystem::create_directory(request.destination);
                writeFile(request.destination / "same.txt", "original contents");
                std::stop_source source;
                bool interrupted = false;
                auto result = reader->extract(request, [&](const ExtractionProgress& progress) {
                    if (progress.currentFile == "large.bin" && progress.processedBytes > 100'000) {
                        interrupted = true;
                        if (tokenCancellation) {
                            source.request_stop();
                            return true;
                        }
                        return false;
                    }
                    return true;
                }, source.get_token());
                failedWith(result, ArchiveErrorCode::Cancelled, "Midstream cancellation code");
                check(interrupted, "Cancel after real payload bytes");
                check(readFile(request.destination / "same.txt") == "original contents", "Cancellation restores overwritten original");
                check(filesIn(request.destination) == std::vector<std::string>{"same.txt"}, "Cancellation removes output and temporary files");
            }
            std::stop_source source;
            source.request_stop();
            auto request = requestFor(fixtures.path / "cancel-preflight-out");
            failedWith(reader->extract(request, {}, source.get_token()), ArchiveErrorCode::Cancelled,
                       "Pre-cancelled extraction does not start");
            check(filesIn(request.destination).empty(), "Pre-cancelled extraction creates no payload");
            const auto renamed = fixtures.path / "cancel-released.tar";
            std::filesystem::rename(archive, renamed);
            std::filesystem::rename(renamed, archive);
            request.destination = fixtures.path / "after-cancel-out";
            succeeded(reader->extract(request), "Reader remains reusable after cancellation");
        });

        tests.run("Progress and conflict callback exceptions release resources and roll back", [&] {
            const auto archive = fixtures.path / "callback-extract.tar";
            createFixture(archive, FixtureFormat::Tar, {{"same.txt", "replacement"}, {"large.bin", std::string(500'000, 'x')}});
            auto reader = openedReader(archive);
            auto request = requestFor(fixtures.path / "callback-progress-out");
            auto result = reader->extract(request, [](const ExtractionProgress&) -> bool {
                throw std::runtime_error("test callback exception");
            });
            failedWith(result, ArchiveErrorCode::BackendError, "Progress exceptions become structured errors");
            check(filesIn(request.destination).empty(), "Exception rollback removes newly created output");
            request.destination = fixtures.path / "callback-conflict-out";
            std::filesystem::create_directory(request.destination);
            writeFile(request.destination / "same.txt", "original contents");
            request.overwrite = OverwritePolicy::Ask;
            request.onConflict = [](const ExtractionConflict&) -> OverwritePolicy {
                throw std::runtime_error("test conflict exception");
            };
            result = reader->extract(request);
            failedWith(result, ArchiveErrorCode::BackendError, "Conflict exceptions become structured errors");
            check(readFile(request.destination / "same.txt") == "original contents", "Conflict exception preserves original");
            check(filesIn(request.destination) == std::vector<std::string>{"same.txt"}, "Conflict callback leaves no temporary files");
        });

#ifdef _WIN32
        tests.run("Cancellation sharing violations expose retained original recovery files", [&] {
            struct LockedFile {
                HANDLE handle = INVALID_HANDLE_VALUE;
                ~LockedFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
            } locked;
            const auto archive = fixtures.path / "rollback-sharing.tar";
            createFixture(archive, FixtureFormat::Tar,
                          {{"same.txt", "replacement contents"}, {"tail.txt", "tail"}});
            auto request = requestFor(fixtures.path / "rollback-sharing-out");
            std::filesystem::create_directory(request.destination);
            writeFile(request.destination / "same.txt", "original contents");
            auto reader = openedReader(archive);
            bool stopped = false;
            const auto result = reader->extract(request, [&](const ExtractionProgress&) {
                if (!stopped && readFile(request.destination / "same.txt") == "replacement contents") {
                    locked.handle = CreateFileW((request.destination / "same.txt").c_str(),
                        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                    check(locked.handle != INVALID_HANDLE_VALUE, "Lock installed output against rollback");
                    stopped = true;
                    return false;
                }
                return true;
            });
            failedWith(result, ArchiveErrorCode::CleanupFailed, "Rollback sharing failure remains diagnostic");
            check(stopped && result.error().path.filename().native().starts_with(L".ananarchiver-"),
                  "Diagnostic identifies retained recovery directory");
            bool recoveredOriginal = false;
            for (const auto& file : std::filesystem::recursive_directory_iterator{result.error().path}) {
                if (!file.is_regular_file()) continue;
                const HANDLE backup = CreateFileW(file.path().c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                check(backup != INVALID_HANDLE_VALUE,
                      "Open retained recovery file: Windows error " + std::to_string(GetLastError()));
                std::array<char, 64> data{};
                DWORD size = 0;
                const bool read = ReadFile(backup, data.data(), static_cast<DWORD>(data.size()), &size, nullptr) != 0;
                CloseHandle(backup);
                check(read, "Read retained recovery file");
                if (std::string_view{data.data(), size} == "original contents") recoveredOriginal = true;
            }
            check(recoveredOriginal, "Failed rollback preserves the original backup");
            CloseHandle(locked.handle);
            locked.handle = INVALID_HANDLE_VALUE;
            // This protected, exclusively created directory belongs to this
            // fixture. Verify and clean Windows recovery files through native
            // APIs, independently of CRT extended-path/protected-ACL handling.
            check(result.error().path.parent_path().native() ==
                      nativeFixturePath(request.destination).native(),
                  "Recovery directory stays inside this fixture destination");
            for (const auto& file : std::filesystem::directory_iterator{result.error().path})
                check(DeleteFileW(file.path().c_str()) != 0, "Remove verified recovery fixture file");
            check(RemoveDirectoryW(result.error().path.c_str()) != 0, "Remove empty recovery fixture directory");
        });
#endif
        return tests.finish();
    } catch (const std::exception& error) {
        std::cerr << "Extraction test setup failed: " << error.what() << '\n';
        return 1;
    }
}
