#pragma once

#include "ananarchiver/archive_entry.h"
#include "ananarchiver/archive_error.h"
#include "ananarchiver/unicode.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace extraction_tests {

using namespace ananarchiver;

inline void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline std::filesystem::path nativeFixturePath(std::filesystem::path path) {
#ifdef _WIN32
    path = std::filesystem::absolute(path);
    path.make_preferred();
    const auto& absolute = path.native();
    if (absolute.starts_with(L"\\\\?\\")) {
        return path;
    }
    if (absolute.starts_with(L"\\\\")) {
        return std::filesystem::path{L"\\\\?\\UNC\\" + absolute.substr(2)};
    }
    return std::filesystem::path{L"\\\\?\\" + absolute};
#else
    return path;
#endif
}

template <class T>
void succeeded(const Result<T>& result, const std::string& message) {
    check(result.has_value(), message + (result ? "" : ": " + result.error().message +
                                                        "; " + result.error().backendMessage));
}

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

    [[nodiscard]] int finish() const {
        std::cout << passed_ << " extraction tests passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("ananarchiver-extraction-tests-" + std::to_string(stamp));
        check(std::filesystem::create_directory(path), "Create unique fixture directory");
    }

    ~TemporaryDirectory() {
        // The fixture owns this freshly created directory and all of its children.
        std::error_code error;
        std::filesystem::remove_all(nativeFixturePath(path), error);
        if (error) {
            std::cerr << "Could not clean fixture directory: " << error.message() << '\n';
        }
    }
};

struct FixtureEntry {
    std::string path;
    std::string data;
    EntryType type = EntryType::RegularFile;
    std::string linkTarget;
    std::int64_t modified = 1'700'000'000;

    FixtureEntry(std::string entryPath, std::string entryData = {},
                 EntryType entryType = EntryType::RegularFile, std::string target = {},
                 std::int64_t timestamp = 1'700'000'000)
        : path(std::move(entryPath)), data(std::move(entryData)), type(entryType),
          linkTarget(std::move(target)), modified(timestamp) {}
};

enum class FixtureFormat { Zip, Tar, TarGzip, SevenZip, Gzip };

struct ArchiveDeleter {
    void operator()(archive* handle) const noexcept { archive_write_free(handle); }
};

struct EntryDeleter {
    void operator()(archive_entry* entry) const noexcept { archive_entry_free(entry); }
};

inline la_ssize_t writeFixture(archive* handle, void* clientData, const void* data,
                              std::size_t size) {
    auto& output = *static_cast<std::ofstream*>(clientData);
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!output) {
        archive_set_error(handle, EIO, "Test fixture output failed");
        return -1;
    }
    return static_cast<la_ssize_t>(size);
}

inline void createFixture(const std::filesystem::path& path, FixtureFormat format,
                          const std::vector<FixtureEntry>& entries,
                          bool encrypted = false) {
    std::ofstream output{path, std::ios::binary};
    check(output.is_open(), "Open test archive output");
    std::unique_ptr<archive, ArchiveDeleter> writer{archive_write_new()};
    check(writer != nullptr, "Create fixture writer");
    int configured = ARCHIVE_FATAL;
    switch (format) {
    case FixtureFormat::Zip:
        configured = archive_write_set_format_zip(writer.get());
        check(archive_write_set_options(writer.get(), "zip:compression=store,zip:hdrcharset=UTF-8") == ARCHIVE_OK,
              "Configure stored UTF-8 ZIP payload for deterministic mutation");
        break;
    case FixtureFormat::Tar:
    case FixtureFormat::TarGzip:
        configured = archive_write_set_format_pax_restricted(writer.get());
        if (format == FixtureFormat::TarGzip) {
            check(archive_write_add_filter_gzip(writer.get()) == ARCHIVE_OK,
                  "Configure TAR.GZ fixture");
        }
        break;
    case FixtureFormat::SevenZip:
        configured = archive_write_set_format_7zip(writer.get());
        break;
    case FixtureFormat::Gzip:
        configured = archive_write_set_format_raw(writer.get());
        check(archive_write_add_filter_gzip(writer.get()) == ARCHIVE_OK,
              "Configure raw GZIP fixture");
        break;
    }
    check(configured == ARCHIVE_OK, "Configure archive fixture format");
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
        check(entry != nullptr, "Allocate archive fixture entry");
        archive_entry_set_pathname_utf8(entry.get(), fixture.path.c_str());
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(fixture.data.size()));
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_mtime(entry.get(), fixture.modified, 0);
        switch (fixture.type) {
        case EntryType::Directory:
            archive_entry_set_filetype(entry.get(), AE_IFDIR);
            break;
        case EntryType::Symlink:
            archive_entry_set_filetype(entry.get(), AE_IFLNK);
            archive_entry_set_symlink_utf8(entry.get(), fixture.linkTarget.c_str());
            break;
        case EntryType::Hardlink:
            archive_entry_set_filetype(entry.get(), AE_IFREG);
            archive_entry_set_hardlink_utf8(entry.get(), fixture.linkTarget.c_str());
            break;
        default:
            archive_entry_set_filetype(entry.get(), AE_IFREG);
            break;
        }
        const auto written = archive_write_header(writer.get(), entry.get());
        check(written == ARCHIVE_OK,
              "Write fixture header: " + std::string{archive_error_string(writer.get()) == nullptr
                  ? "" : archive_error_string(writer.get())});
        if (!fixture.data.empty()) {
            check(archive_write_data(writer.get(), fixture.data.data(), fixture.data.size()) ==
                      static_cast<la_ssize_t>(fixture.data.size()), "Write fixture payload");
        }
        check(archive_write_finish_entry(writer.get()) == ARCHIVE_OK, "Finish fixture entry");
    }
    check(archive_write_close(writer.get()) == ARCHIVE_OK, "Close fixture archive");
    output.close();
    check(!output.fail(), "Flush fixture archive output");
}

struct SparseExtent {
    std::uint64_t offset;
    std::string data;
};

inline void createSparseFixture(const std::filesystem::path& path, std::string_view pathname,
                                std::uint64_t logicalSize,
                                const std::vector<SparseExtent>& extents) {
    std::ofstream output{path, std::ios::binary};
    check(output.is_open(), "Open sparse fixture output");
    std::unique_ptr<archive, ArchiveDeleter> writer{archive_write_new()};
    check(writer != nullptr, "Create sparse fixture writer");
    check(archive_write_set_format_pax(writer.get()) == ARCHIVE_OK, "Configure sparse PAX format");
    check(archive_write_open(writer.get(), &output, nullptr, writeFixture, nullptr) == ARCHIVE_OK,
          "Open sparse writer stream");
    std::unique_ptr<archive_entry, EntryDeleter> entry{archive_entry_new()};
    check(entry != nullptr, "Allocate sparse fixture entry");
    const std::string name{pathname};
    archive_entry_set_pathname_utf8(entry.get(), name.c_str());
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    archive_entry_set_mtime(entry.get(), 1'700'000'000, 0);
    archive_entry_set_size(entry.get(), static_cast<la_int64_t>(logicalSize));
    std::uint64_t previousEnd = 0;
    for (const auto& extent : extents) {
        check(extent.offset >= previousEnd && extent.offset <= logicalSize &&
              extent.data.size() <= logicalSize - extent.offset, "Sparse fixture extent stays ordered and bounded");
        archive_entry_sparse_add_entry(entry.get(), static_cast<la_int64_t>(extent.offset),
                                       static_cast<la_int64_t>(extent.data.size()));
        previousEnd = extent.offset + extent.data.size();
    }
    check(archive_write_header(writer.get(), entry.get()) == ARCHIVE_OK, "Write sparse PAX header");
    std::array<char, 64 * 1024> buffer{};
    // libarchive's sparse writer consumes a logical stream including holes,
    // and omits hole bytes from the resulting PAX archive itself.
    for (std::uint64_t offset = 0; offset < logicalSize;) {
        const auto count = static_cast<std::size_t>((std::min)(
            static_cast<std::uint64_t>(buffer.size()), logicalSize - offset));
        std::fill(buffer.begin(), buffer.end(), '\0');
        for (const auto& extent : extents) {
            const auto start = (std::max)(offset, extent.offset);
            const auto end = (std::min)(offset + count, extent.offset + extent.data.size());
            if (start < end) {
                const auto dataOffset = static_cast<std::size_t>(start - extent.offset);
                const auto bufferOffset = static_cast<std::size_t>(start - offset);
                const auto bytes = static_cast<std::size_t>(end - start);
                std::copy_n(extent.data.begin() + static_cast<std::ptrdiff_t>(dataOffset), bytes,
                            buffer.begin() + static_cast<std::ptrdiff_t>(bufferOffset));
            }
        }
        check(archive_write_data(writer.get(), buffer.data(), count) == static_cast<la_ssize_t>(count),
              "Stream sparse fixture logical data");
        offset += count;
    }
    check(archive_write_finish_entry(writer.get()) == ARCHIVE_OK, "Finish sparse PAX entry");
    check(archive_write_close(writer.get()) == ARCHIVE_OK, "Close sparse PAX archive");
    output.close();
    check(!output.fail(), "Flush sparse archive fixture");
}

inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream input{nativeFixturePath(path), std::ios::binary};
    check(input.is_open(), "Read expected output: " + pathToUtf8(path));
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

inline void writeFile(const std::filesystem::path& path, std::string_view data) {
    std::ofstream output{path, std::ios::binary};
    check(output.is_open(), "Create expected output fixture");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(output.good(), "Write output fixture");
}

inline std::vector<std::string> filesIn(const std::filesystem::path& root) {
    std::vector<std::string> result;
    if (!std::filesystem::exists(root)) {
        return result;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file() || entry.is_symlink()) {
            const auto relative = entry.path().lexically_relative(root).generic_u8string();
            result.emplace_back(relative.begin(), relative.end());
        }
    }
    std::ranges::sort(result);
    return result;
}

inline std::size_t zipPayloadOffset(const std::string& bytes) {
    const std::string signature{"PK\x03\x04", 4};
    const auto header = bytes.find(signature);
    check(header != std::string::npos && header + 30 <= bytes.size(), "Find ZIP local header");
    const auto shortAt = [&](std::size_t offset) {
        return static_cast<std::size_t>(static_cast<unsigned char>(bytes[header + offset])) |
               (static_cast<std::size_t>(static_cast<unsigned char>(bytes[header + offset + 1])) << 8);
    };
    const auto offset = header + 30 + shortAt(26) + shortAt(28);
    check(offset < bytes.size(), "Locate fixture ZIP data");
    return offset;
}

inline void corruptZipPayload(const std::filesystem::path& path, std::size_t payloadOffset = 1) {
    auto bytes = readFile(path);
    const auto position = zipPayloadOffset(bytes) + payloadOffset;
    check(position < bytes.size(), "Mutation stays within fixture data");
    bytes[position] = static_cast<char>(static_cast<unsigned char>(bytes[position]) ^ 0x5aU);
    writeFile(path, bytes);
}

inline void setUnsupportedZipCompression(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    const auto local = bytes.find(std::string{"PK\x03\x04", 4});
    const auto central = bytes.find(std::string{"PK\x01\x02", 4});
    check(local != std::string::npos && central != std::string::npos && central + 12 < bytes.size(),
          "Find both ZIP fixture compression fields");
    bytes[local + 8] = static_cast<char>(0xff);
    bytes[local + 9] = 0;
    bytes[central + 10] = static_cast<char>(0xff);
    bytes[central + 11] = 0;
    writeFile(path, bytes);
}

} // namespace extraction_tests
