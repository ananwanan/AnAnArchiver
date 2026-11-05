#include <algorithm>
#include <ananarchiver/application/browser_service.h>
#include <ananarchiver/unicode.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>

using namespace ananarchiver;
using namespace ananarchiver::application;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T value(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(*result);
}
ArchiveEntry item(std::uint64_t id, std::string path, EntryType type = EntryType::RegularFile) {
    ArchiveEntry entry;
    entry.index = id;
    entry.path = std::move(path);
    entry.type = type;
    entry.size = id;
    return entry;
}
std::shared_ptr<const MetadataIndex> index(std::vector<ArchiveEntry> entries,
                                           MetadataLimits limits = {}) {
    return value(MetadataIndex::build(
        {std::make_shared<const std::vector<ArchiveEntry>>(std::move(entries))}, limits));
}
TaskSnapshot wait(TaskManager& tasks, TaskId id) {
    const auto until = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < until) {
        auto result = tasks.snapshot(id);
        check(result.has_value(), "Missing task");
        if (result->state == TaskState::Succeeded || result->state == TaskState::Failed ||
            result->state == TaskState::Cancelled)
            return *result;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Browser task timeout");
}
void directoryAndQueries() {
    auto source =
        index({item(7, "docs/sub/CAFÉ🐈.txt"), item(8, "docs/sub/CAFÉ🐈.txt"),
               item(9, "empty/", EntryType::Directory), item(10, "docs/", EntryType::Directory),
               item(11, "docs-other/a"), item(12, "안녕.txt"), item(13, "こんにちは.txt"),
               item(14, "安安.txt"), item(15, "Straße.txt"), item(16, "σ.txt"), item(17, "Σ.txt"),
               item(18, "İ.txt"), item(19, "back\\child.txt")});
    auto root = value(source->query({}));
    auto page = value(source->page(root, 0, 512, 4096));
    check(page.rows.size() == 11 && page.rows[0].folder && page.rows[0].name == "back",
          "Direct folder rows/order/deduplication");
    check(source->hierarchy().folders.size() == 6, "Implicit or empty folders missing");
    auto rows = value(source->query({"docs/sub", "café🐈", 0, true}));
    check(rows.size() == 2, "Scoped Unicode simple folding failed");
    auto selected = value(source->resolve(rows, std::vector<MetadataRange>{{0, 2}}));
    check(selected.indices == std::vector<std::uint64_t>{7, 8}, "Duplicate original ordinal lost");
    auto folderView = value(source->query({"", "docs", 0, false}));
    auto directory = value(source->resolve(folderView, std::vector<MetadataRange>{{0, 1}}));
    check(directory.indices.empty() && directory.directories == std::vector<std::string>{"docs"},
          "Folder identity is not separate");
    auto folded = value(source->query({"", "STRASSE", -1}));
    check(folded.empty(), "Simple folding unexpectedly expanded sharp S");
    check(value(source->query({"", "STRAẞE", -1})).size() == 1, "Sharp S simple folding failed");
    check(value(source->query({"", "σ"})).size() == 2, "Greek Unicode folding missing");
    check(value(source->query({"", "i̇"})).empty() && value(source->query({"", "İ"})).size() == 1,
          "Dotted I simple folding changed");
    check(value(source->query({"", "안녕"})).size() == 1 &&
              value(source->query({"", "こんにちは"})).size() == 1 &&
              value(source->query({"", "安安"})).size() == 1,
          "CJK search failed");
    auto normalized = index({item(1, "scope/./CAFÉ.txt/./"), item(2, "scope\\other.txt")});
    auto normalizedRows = value(normalized->query({"scope", "café", 0}));
    check(normalizedRows.size() == 1 && normalized->name(normalizedRows.front()) == "CAFÉ.txt",
          "Normalized direct child basename/search differed");
    auto unsafe = index({item(1, "../evil")});
    auto unsafeRows = value(unsafe->query({}));
    check(!unsafe->resolve(unsafeRows, std::vector<MetadataRange>{{0, 1}}),
          "Unsafe folder selected");
    auto invalid = source->query({"missing"});
    check(!invalid, "Missing folder accepted");
    check(!source->resolve(rows, std::vector<MetadataRange>{{0, 2}, {1, 1}}),
          "Overlapping ranges accepted");
    check(!source->resolve(rows, std::vector<MetadataRange>{{1, UINT64_MAX}}),
          "Overflow range accepted");
    std::stop_source cancelled;
    cancelled.request_stop();
    check(!source->query({}, cancelled.get_token()) &&
              !source->page(root, 0, 512, 4096, cancelled.get_token()) &&
              !source->resolve(rows, {}, cancelled.get_token()),
          "Cancelled operation succeeded");
    std::cout << "PASS hierarchy, Unicode, duplicate identities, directory authorization, ranges "
                 "and cancellation\n";
}
void sortAndPages() {
    auto unknown = item(20, "unknown");
    unknown.sizeKnown = false;
    auto a = item(40, "same");
    a.size = 7;
    a.modified = std::chrono::system_clock::time_point{2s};
    auto b = a;
    b.index = 30;
    auto c = item(10, "A");
    c.size = 2;
    c.modified = std::chrono::system_clock::time_point{1s};
    auto source = index({unknown, a, b, c});
    for (bool descending : {false, true}) {
        auto rows = value(source->query({"", "", 1, descending}));
        auto result = value(source->resolve(rows, std::vector<MetadataRange>{{0, rows.size()}}));
        check(result.indices == (descending ? std::vector<std::uint64_t>{40, 30, 10, 20}
                                            : std::vector<std::uint64_t>{10, 40, 30, 20}),
              "Size known-last stable sorting failed");
        rows = value(source->query({"", "", 3, descending}));
        check(source->entry(rows.back()).index == 20, "Timestamp unknown-last sorting failed");
    }
    auto rows = value(source->query({}));
    check(value(source->page(rows, 4, 1, 1024)).rows.empty(), "EOF page failed");
    check(!source->page(rows, 5, 1, 1024), "Beyond EOF accepted");
    check(!source->page(rows, 0, 513, 1024) && !source->page(rows, 0, 0, 1024),
          "Row budget ignored");
    check(!source->page(rows, 0, 1, 160), "Too small byte budget did not error");
    check(value(source->page(rows, 0, 512, 320)).rows.size() == 1, "Byte-limited page failed");
    check(!source->page(rows, 0, 1, MetadataIndex::maxPageBytes + 1), "Maximum page bytes ignored");
    std::cout << "PASS stable sorts and page/count/byte/EOF boundaries\n";
}
void budgets() {
    MetadataLimits noRoot;
    noRoot.maxFolders = 0;
    check(!MetadataIndex::build({}, noRoot), "Root folder was not charged to node budget");
    auto batch = std::make_shared<const std::vector<ArchiveEntry>>(
        std::vector<ArchiveEntry>{item(0, "a/b/file")});
    for (int mode = 0; mode < 6; ++mode) {
        MetadataLimits limits;
        if (mode == 0)
            limits.maxEntries = 0;
        if (mode == 1)
            limits.maxFolders = 2;
        if (mode == 2)
            limits.maxDepth = 2;
        if (mode == 3)
            limits.maxPathBytes = 3;
        if (mode == 4)
            limits.maxFolderTextBytes = 1;
        if (mode == 5)
            limits.maxIndexBytes = 1;
        auto built = MetadataIndex::build({batch}, limits);
        check(!built && built.error().code == ArchiveErrorCode::ResourceLimitExceeded,
              "Resource budget did not fail atomically");
    }
    auto invalid = MetadataIndex::build({std::make_shared<const std::vector<ArchiveEntry>>(
        std::vector<ArchiveEntry>{item(0, std::string("bad\0name", 8))})});
    check(!invalid && invalid.error().code == ArchiveErrorCode::InvalidPathEncoding,
          "NUL metadata accepted");
    std::cout << "PASS entry/folder/depth/path/text/index memory budgets\n";
}
class Reader final : public ArchiveReader {
  public:
    std::shared_ptr<std::atomic<bool>> entered;
    bool slow{};
    explicit Reader(std::shared_ptr<std::atomic<bool>> marker) : entered(std::move(marker)) {}
    Result<ArchiveInfo> open(const std::filesystem::path& path, CancellationToken) override {
        slow = path == "slow";
        ArchiveInfo result;
        result.path = path;
        return result;
    }
    Result<void> enumerate(const EntryCallback& callback, CancellationToken stop) override {
        *entered = true;
        for (std::uint64_t i = 0; i < 100; ++i) {
            if (slow) {
                while (!stop.stop_requested())
                    std::this_thread::sleep_for(1ms);
            }
            if (stop.stop_requested() || !callback(item(i, "docs/" + std::to_string(i))))
                return std::unexpected(
                    ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
        }
        return {};
    }
};
void generations() {
    TaskManager tasks;
    auto entered = std::make_shared<std::atomic<bool>>();
    BrowserService browser(tasks, [entered] { return std::make_unique<Reader>(entered); });
    auto first = value(browser.open("one"));
    check(wait(tasks, first.task).state == TaskState::Succeeded, "Native browser open failed");
    auto result = tasks.browserResult(first.task);
    check(result && result->entryCount == 100, "Open result missing");
    tasks.release(first.task);
    auto q = value(browser.query(first.archive, {"docs"}));
    check(wait(tasks, q.task).state == TaskState::Succeeded, "Query failed");
    tasks.release(q.task);
    auto select = value(browser.select(first.archive, q.query, {}, true));
    check(wait(tasks, select.task).state == TaskState::Succeeded, "Native all selection failed");
    tasks.release(select.task);
    auto next = value(browser.query(first.archive, {"docs", "99"}));
    check(!browser.valid(first.archive, q.query), "Old query not invalidated immediately");
    check(!browser.page(first.archive, q.query, 0, 1, 1024) &&
              !browser.select(first.archive, q.query, {}, true),
          "Stale view accepted selected operation");
    check(!browser.extract(first.archive, q.query, select.selection, {}),
          "Stale native selection accepted");
    check(wait(tasks, next.task).state == TaskState::Succeeded, "Replacement query failed");
    tasks.release(next.task);
    auto bad = value(browser.query(first.archive, {"missing"}));
    check(wait(tasks, bad.task).state == TaskState::Failed, "Missing query succeeded");
    check(!browser.page(first.archive, bad.query, 0, 1, 1024), "Failed query exposed partial view");
    tasks.release(bad.task);
    browser.reclaim();
    *entered = false;
    auto slow = value(browser.open("slow"));
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!entered->load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    check(browser.close(slow.archive), "Close failed");
    check(wait(tasks, slow.task).state == TaskState::Cancelled, "Close did not cancel worker");
    tasks.release(slow.task);
    browser.reclaim();
    auto cancelled = value(browser.open("slow"));
    tasks.cancel(cancelled.task);
    check(wait(tasks, cancelled.task).state == TaskState::Cancelled, "Cancel failed");
    check(!browser.query(cancelled.archive, {}), "Cancelled archive exposed index");
    tasks.release(cancelled.task);
    browser.close(cancelled.archive);
    browser.reclaim();
    check(result->entryCount == 100, "Owned result invalid after closing archive");
    std::cout << "PASS native generations, stale selection, failed view, cancel, close and owned "
                 "results\n";
}
void scale(std::size_t count) {
    const auto start = std::chrono::steady_clock::now();
    std::vector<MetadataBatch> batches;
    for (std::size_t first = 0; first < count; first += 512) {
        std::vector<ArchiveEntry> entries;
        entries.reserve(512);
        for (auto i = first; i < std::min(first + 512, count); ++i)
            entries.push_back(item(i, "docs/entry-" + std::to_string(i) + "-CAFÉ.txt"));
        batches.push_back(std::make_shared<const std::vector<ArchiveEntry>>(std::move(entries)));
    }
    const auto generated = std::chrono::steady_clock::now();
    auto source = value(MetadataIndex::build(std::move(batches)));
    const auto built = std::chrono::steady_clock::now();
    auto rows = value(source->query({"docs", "café", 1, true}));
    check(rows.size() == count, "Large query row count");
    check(source->entry(rows.front()).index == count - 1 && source->entry(rows.back()).index == 0,
          "Large query original identities/order");
    const auto queried = std::chrono::steady_clock::now();
    std::size_t offset = 0, maxBytes = 0, pages = 0;
    while (offset < rows.size()) {
        auto page = value(source->page(rows, offset, 512, 65536));
        check(!page.rows.empty() && page.rows.size() <= 512 && page.bytes <= 65536,
              "Large bounded page failed");
        check(page.rows.front().entry.index == count - offset - 1, "Large page identity");
        offset += page.rows.size();
        maxBytes = std::max(maxBytes, static_cast<std::size_t>(page.bytes));
        ++pages;
    }
    const auto paged = std::chrono::steady_clock::now();
    auto selection = value(source->resolve(rows, std::vector<MetadataRange>{{0, count}}));
    check(selection.indices.size() == count, "Large native select-all");
    auto exact = value(source->query({"docs", "entry-" + std::to_string(count - 1) + "-", 0}));
    check(exact.size() == 1, "Large selective query");
    const auto ended = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    std::cout << "SCALE entries=" << count << " generated_ms=" << ms(start, generated)
              << " build_ms=" << ms(generated, built) << " query_ms=" << ms(built, queried)
              << " pages_ms=" << ms(queried, paged)
              << " selection_and_search_ms=" << ms(paged, ended)
              << " accounted_bytes=" << source->accountedBytes() << " pages=" << pages
              << " max_transfer_bytes=" << maxBytes << "\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            scale(static_cast<std::size_t>(std::stoull(argv[1])));
            return 0;
        }
        directoryAndQueries();
        sortAndPages();
        budgets();
        generations();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
