#include "../security/extraction_fixtures.h"
#include <ananarchiver/c_api/ananarchiver.h>
#include <atomic>
#include <thread>
using namespace extraction_tests;
using namespace std::chrono_literals;
namespace {
aa_string input(std::string_view text) { return {text.data(), text.size()}; }
std::string text(aa_string s) {
    return s.size ? std::string(s.data, static_cast<std::size_t>(s.size)) : "";
}
struct Engine {
    aa_engine* handle{};
    Engine() {
        check(aa_engine_create(AA_ABI_VERSION, &handle, nullptr) == AA_OK, "engine create");
    }
    ~Engine() { aa_engine_destroy(handle); }
};
using ResultPage = std::unique_ptr<const aa_browser_result, decltype(&aa_browser_result_release)>;
std::unique_ptr<const aa_task_snapshot, decltype(&aa_snapshot_release)> wait(aa_engine* engine,
                                                                             aa_task_id task) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        const aa_task_snapshot* result = nullptr;
        check(aa_task_poll(engine, task, &result, nullptr) == AA_OK && result, "poll browser task");
        std::unique_ptr<const aa_task_snapshot, decltype(&aa_snapshot_release)> owned(
            result, aa_snapshot_release);
        if (result->state == AA_TASK_SUCCEEDED || result->state == AA_TASK_CANCELLED ||
            result->state == AA_TASK_FAILED)
            return owned;
        check(std::chrono::steady_clock::now() < deadline, "browser task timed out");
        std::this_thread::sleep_for(1ms);
    }
}
ResultPage result(aa_engine* engine, aa_task_id task) {
    auto state = wait(engine, task);
    check(state->state == AA_TASK_SUCCEEDED,
          "browser task not successful: " + (state->error ? text(state->error->message) : ""));
    const aa_browser_result* value = nullptr;
    check(aa_browser_task_result(engine, task, &value, nullptr) == AA_OK && value,
          "browser result");
    return {value, aa_browser_result_release};
}
void release(aa_engine* engine, aa_task_id task) {
    check(aa_task_release(engine, task) == AA_OK, "release browser task");
}
struct Browser {
    aa_archive_id archive{};
    aa_query_id query{};
};
Browser open(aa_engine* engine, const std::filesystem::path& file) {
    const auto path = pathToUtf8(file);
    aa_open_request request{sizeof(request), 0, input(path), {}};
    Browser browser;
    aa_task_id task{};
    check(aa_browser_open(engine, &request, &browser.archive, &task, nullptr) == AA_OK,
          "submit native open");
    auto opened = result(engine, task);
    check(opened->entry_count > 0 && opened->index_bytes > 0, "native index stats");
    const aa_entry_batch* stream = nullptr;
    check(aa_task_take_entries(engine, task, &stream, nullptr) == AA_OK && !stream->count,
          "browser exported a full index stream");
    aa_entry_batch_release(stream);
    release(engine, task);
    return browser;
}
std::uint64_t query(aa_engine* engine, Browser& browser, std::string_view folder = {},
                    std::string_view search = {}, int sort = -1, bool descending = false) {
    aa_browser_query_request request{
        sizeof(request), sort, browser.archive, input(folder), input(search), descending, 0};
    aa_task_id task{};
    check(aa_browser_query(engine, &request, &browser.query, &task, nullptr) == AA_OK,
          "submit query");
    auto page = result(engine, task);
    auto total = page->total_rows;
    release(engine, task);
    return total;
}
ResultPage page(aa_engine* engine, Browser browser, std::uint64_t offset = 0,
                std::uint32_t bytes = 4096) {
    aa_browser_page_request request{sizeof(request), 512, browser.archive, browser.query, offset,
                                    bytes,           0};
    aa_task_id task{};
    check(aa_browser_request_page(engine, &request, &task, nullptr) == AA_OK, "submit page");
    auto value = result(engine, task);
    check(value->count <= 512 && value->transfer_bytes <= bytes, "unbounded page");
    release(engine, task);
    return value;
}
aa_selection_id select(aa_engine* engine, Browser browser,
                       std::vector<aa_browser_range> ranges = {}, bool all = false) {
    aa_browser_select_request request{sizeof(request), all,           browser.archive,
                                      browser.query,   ranges.data(), ranges.size()};
    aa_task_id task{};
    aa_selection_id selected{};
    check(aa_browser_select(engine, &request, &selected, &task, nullptr) == AA_OK,
          "submit native selection");
    auto value = result(engine, task);
    check(value->selection == selected, "selection result handle");
    release(engine, task);
    return selected;
}
void extract(aa_engine* engine, Browser browser, aa_selection_id selection,
             const std::filesystem::path& destination) {
    auto path = pathToUtf8(destination);
    aa_extract_request request{};
    request.struct_size = sizeof(request);
    request.destination = input(path);
    request.overwrite = AA_OVERWRITE_SKIP;
    aa_task_id task{};
    check(aa_browser_extract(engine, browser.archive, browser.query, selection, &request, &task,
                             nullptr) == AA_OK,
          "native extraction submit");
    auto state = wait(engine, task);
    check(state->state == AA_TASK_SUCCEEDED, "native selected extraction failed");
    release(engine, task);
}
void behavior() {
    TemporaryDirectory temp;
    auto file = temp.path / "browser.bin";
    createFixture(file, FixtureFormat::Zip,
                  {{"docs/sub/CAFÉ🐈.txt", "first"},
                   {"docs/sub/CAFÉ🐈.txt", "second"},
                   {"docs/other.txt", "other"},
                   {"docs-other.txt", "boundary"},
                   {"empty/", {}, EntryType::Directory},
                   {"root.txt", "root"}});
    Engine engine;
    auto browser = open(engine.handle, file);
    check(query(engine.handle, browser) == 4, "root direct children");
    auto root = page(engine.handle, browser);
    check(root->count == 4 && root->rows[0].is_folder && text(root->rows[0].name) == "docs" &&
              root->rows[0].identity != 0 && root->rows[0].entry.index == AA_NO_ENTRY_INDEX,
          "folder row identity");
    check(root->rows[1].is_folder && text(root->rows[1].name) == "empty", "explicit empty folder");
    aa_browser_folder_page_request treeRequest{
        sizeof(treeRequest), 1, browser.archive, {}, 0, 1024, 0};
    aa_task_id treeTask{};
    check(aa_browser_request_folders(engine.handle, &treeRequest, &treeTask, nullptr) == AA_OK,
          "lazy tree submit");
    auto tree = result(engine.handle, treeTask);
    check(tree->query == 0 && tree->count == 1 && tree->total_rows == 2 && tree->rows[0].is_folder,
          "bounded lazy tree window");
    release(engine.handle, treeTask);
    auto directory = select(engine.handle, browser, {{0, 1}});
    extract(engine.handle, browser, directory, temp.path / "selected");
    check(std::filesystem::exists(temp.path / pathFromUtf8("selected/docs/sub/CAFÉ🐈.txt")) &&
              std::filesystem::exists(temp.path / "selected/docs/other.txt") &&
              !std::filesystem::exists(temp.path / "selected/docs-other.txt"),
          "directory component selection boundary");
    auto old = browser;
    check(query(engine.handle, browser, "docs/sub", "café🐈", 0, true) == 2,
          "scoped unicode query");
    auto duplicates = page(engine.handle, browser);
    check(duplicates->count == 2 && duplicates->rows[0].entry.index == 0 &&
              duplicates->rows[1].entry.index == 1 &&
              duplicates->rows[0].identity != duplicates->rows[1].identity,
          "duplicate original identities");
    auto exact = select(engine.handle, browser, {{1, 1}});
    extract(engine.handle, browser, exact, temp.path / "exact");
    check(readFile(temp.path / pathFromUtf8("exact/docs/sub/CAFÉ🐈.txt")) == "second",
          "selected wrong duplicate");
    auto all = select(engine.handle, browser, {}, true);
    extract(engine.handle, browser, all, temp.path / "visible");
    check(!std::filesystem::exists(temp.path / "visible/docs/other.txt"),
          "visible all selected hidden sibling");
    aa_browser_page_request stale{sizeof(stale), 1, old.archive, old.query, 0, 4096, 0};
    aa_task_id task = 99;
    check(aa_browser_request_page(engine.handle, &stale, &task, nullptr) == AA_STALE_VIEW &&
              task == 0,
          "stale page accepted");
    aa_browser_select_request staleSelection{
        sizeof(staleSelection), 1, old.archive, old.query, nullptr, 0};
    aa_selection_id selected = 99;
    check(aa_browser_select(engine.handle, &staleSelection, &selected, &task, nullptr) ==
                  AA_STALE_VIEW &&
              !selected,
          "stale selection accepted");
    auto empty = page(engine.handle, browser, 2);
    check(empty->count == 0 && empty->total_rows == 2, "EOF page");
    aa_browser_page_request invalid{
        sizeof(invalid), 512, browser.archive, browser.query, 3, 4096, 0};
    check(aa_browser_request_page(engine.handle, &invalid, &task, nullptr) == AA_OK,
          "past end submit");
    auto failed = wait(engine.handle, task);
    check(failed->state == AA_TASK_FAILED, "past end not failed");
    release(engine.handle, task);
    auto held = page(engine.handle, browser);
    check(aa_browser_close(engine.handle, browser.archive) == AA_OK, "close browser");
    check(aa_browser_request_page(engine.handle, &invalid, &task, nullptr) == AA_STALE_VIEW,
          "closed archive accepted query");
    aa_engine_destroy(std::exchange(engine.handle, nullptr));
    check(text(held->rows[0].name) == "CAFÉ🐈.txt" && text(root->rows[0].name) == "docs",
          "owned page lost lifetime");
}
void boundaryAndShutdown() {
    Engine engine;
    aa_api_info info{};
    info.struct_size = sizeof(info);
    check(aa_get_api_info(&info) == AA_OK && (info.features & AA_FEATURE_BROWSER),
          "feature negotiation");
    aa_browser_query_request request{};
    aa_query_id queryId = 99;
    aa_task_id task = 99;
    check(aa_browser_query(engine.handle, &request, &queryId, &task, nullptr) ==
                  AA_INVALID_ARGUMENT &&
              !queryId && !task,
          "short struct outputs");
    request.struct_size = sizeof(request);
    request.search = {nullptr, 1};
    check(aa_browser_query(engine.handle, &request, &queryId, &task, nullptr) ==
              AA_INVALID_ARGUMENT,
          "null search");
    request.search = input(std::string_view("\xFF", 1));
    check(aa_browser_query(engine.handle, &request, &queryId, &task, nullptr) ==
              AA_INVALID_ARGUMENT,
          "bad unicode search");
    aa_browser_select_request selection{};
    selection.struct_size = sizeof(selection);
    selection.range_count = UINT64_MAX;
    aa_selection_id id = 99;
    check(aa_browser_select(engine.handle, &selection, &id, &task, nullptr) ==
                  AA_INVALID_ARGUMENT &&
              !id,
          "range input overflow");
    aa_browser_page_request pageRequest{};
    pageRequest.struct_size = sizeof(pageRequest);
    pageRequest.max_rows = 513;
    pageRequest.max_bytes = 4096;
    check(aa_browser_request_page(engine.handle, &pageRequest, &task, nullptr) ==
              AA_INVALID_ARGUMENT,
          "unbounded row request");
    pageRequest.max_rows = 1;
    pageRequest.max_bytes = AA_MAX_PAGE_BYTES + 1;
    check(aa_browser_request_page(engine.handle, &pageRequest, &task, nullptr) ==
              AA_INVALID_ARGUMENT,
          "unbounded bytes request");
    TemporaryDirectory temp;
    auto file = temp.path / "large.zip";
    std::vector<FixtureEntry> entries;
    for (int i = 0; i < 20000; ++i)
        entries.push_back({"docs/" + std::to_string(i), ""});
    createFixture(file, FixtureFormat::Zip, entries);
    auto path = pathToUtf8(file);
    aa_open_request openRequest{sizeof(openRequest), 0, input(path), {}};
    aa_archive_id archive{};
    auto large = open(engine.handle, file);
    check(query(engine.handle, large, "docs", "", 1, true) == 20000, "large C browser query");
    auto bounded = page(engine.handle, large, 0, 2048);
    check(bounded->count > 0 && bounded->count < 512 && bounded->transfer_bytes <= 2048,
          "large ABI byte bound");
    aa_browser_select_request overflow{sizeof(overflow), 0, large.archive, large.query, nullptr, 1};
    aa_browser_range badRange{1, UINT64_MAX};
    overflow.ranges = &badRange;
    aa_selection_id failedSelection{};
    check(aa_browser_select(engine.handle, &overflow, &failedSelection, &task, nullptr) == AA_OK,
          "overflow selection submit");
    auto bad = wait(engine.handle, task);
    check(bad->state == AA_TASK_FAILED, "overflow selection was accepted");
    release(engine.handle, task);
    auto hostile = temp.path / "hostile.zip";
    std::string deep;
    for (int i = 0; i < 257; ++i)
        deep += "a/";
    createFixture(hostile, FixtureFormat::Zip, {{deep + "file", ""}});
    auto hostilePath = pathToUtf8(hostile);
    aa_open_request hostileRequest{sizeof(hostileRequest), 0, input(hostilePath), {}};
    check(aa_browser_open(engine.handle, &hostileRequest, &archive, &task, nullptr) == AA_OK,
          "hostile index submit");
    auto rejected = wait(engine.handle, task);
    check(rejected->state == AA_TASK_FAILED && rejected->error &&
              rejected->error->code == AA_ERROR_RESOURCE_LIMIT,
          "default metadata depth budget");
    const aa_browser_result* partial = nullptr;
    check(aa_browser_task_result(engine.handle, task, &partial, nullptr) == AA_ARCHIVE_ERROR &&
              !partial,
          "failed index exported partial view");
    release(engine.handle, task);
    check(aa_browser_open(engine.handle, &openRequest, &archive, &task, nullptr) == AA_OK,
          "cancel open submit");
    check(aa_task_cancel(engine.handle, task) == AA_OK, "cancel native build");
    auto cancelled = wait(engine.handle, task);
    check(cancelled->state == AA_TASK_CANCELLED, "native build cancel");
    release(engine.handle, task);
    check(aa_browser_close(engine.handle, archive) == AA_OK, "close cancelled native browser");
    check(aa_browser_open(engine.handle, &openRequest, &archive, &task, nullptr) == AA_OK,
          "shutdown open submit");
    check(aa_engine_shutdown(engine.handle) == AA_OK, "shutdown with native work");
    uint32_t stopped = 0;
    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!stopped && std::chrono::steady_clock::now() < until) {
        aa_engine_is_stopped(engine.handle, &stopped);
        std::this_thread::sleep_for(1ms);
    }
    check(stopped, "shutdown left browser worker alive");
}
} // namespace
int main() {
    Tests tests;
    tests.run("F2 bounded native browsing, Unicode, duplicate/directory selection, stale "
              "generations and ownership",
              behavior);
    tests.run("F2 checked requests, cancellation and shutdown", boundaryAndShutdown);
    return tests.finish();
}
