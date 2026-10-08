#include "models/archive_entry_model.h"
#include "platform/path_conversion.h"
#include "../../../tests/security/extraction_fixtures.h"
#include <ananarchiver/libarchive_reader.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using namespace ananarchiver;
using qt::ArchiveEntryModel;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()>& ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 15000) {
        QEventLoop loop;
        QTimer::singleShot(1, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}

void finishQuery(ArchiveEntryModel& model)
{
    require(waitUntil([&] { return !model.querying(); }), "Metadata query did not complete");
}

ArchiveEntry entry(std::uint64_t index, std::string path, std::uint64_t size,
                   std::optional<std::chrono::system_clock::time_point> modified = {})
{
    ArchiveEntry result;
    result.index = index;
    result.path = std::move(path);
    result.size = size;
    result.type = EntryType::RegularFile;
    result.modified = modified;
    return result;
}

std::vector<std::uint64_t> visibleIndices(ArchiveEntryModel& model)
{
    model.selectAll();
    const auto result = model.selectedIndices();
    model.clearSelection();
    return result;
}

void unicodeSearchAndSelection()
{
    ArchiveEntryModel model;
    model.append({entry(18, "中文/こんにちは/한국어/CAFÉ🐈.txt", 10),
                  entry(77, "中文/café🐈.txt", 20),
                  entry(94, "unrelated.txt", 30),
                  entry(101, "中文/café🐈.txt", 40)});
    model.selectRow(2, false, false);
    model.setQuery(QString::fromUtf8("café🐈"));
    require(model.querying() && model.selectionCount() == 0,
            "Query did not clear potentially hidden selection");
    model.selectAll();
    require(model.selectionCount() == 0, "Pending query accepted selection of stale rows");
    finishQuery(model);
    require(model.count() == 3 && model.totalCount() == 4,
            "Unicode case-insensitive search or visible/total counts failed");
    require(visibleIndices(model) == std::vector<std::uint64_t>{18, 77, 101},
            "Filtered select-all included hidden rows or collapsed duplicate paths");
    model.setQuery(QString::fromUtf8("こんにちは"));
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{18}, "Search ignored the entry path");
    ExtractionRequest totals;
    model.setExtractionTotals(totals, false);
    require(totals.totalEntries == 4 && totals.totalBytes == 100,
            "Filtered view changed extract-all estimates");
    ExtractionRequest selectedTotals;
    model.setExtractionTotals(selectedTotals, true);
    require(!selectedTotals.totalEntries && !selectedTotals.totalBytes,
            "Selected extraction claimed an unexpanded directory total");
    model.setQuery({});
    require(!model.querying() && model.count() == 4,
            "Restoring archive order unnecessarily waited for a worker");
    std::cout << "PASS Unicode path search, visible selection and full archive totals\n";
}

void numericSortAndStableIdentity()
{
    ArchiveEntryModel model;
    const auto early = std::chrono::system_clock::time_point{std::chrono::seconds{100}};
    const auto late = std::chrono::system_clock::time_point{std::chrono::seconds{200}};
    auto unknown = entry(600, "unknown", 0);
    unknown.sizeKnown = false;
    auto directory = entry(700, "folder", 0);
    directory.type = EntryType::Directory;
    model.append({entry(100, "duplicate", 10, late), entry(200, "duplicate", 2, early),
                  entry(300, "alpha", 100, late), entry(400, "duplicate", 10, late),
                  unknown, directory});
    model.setSortColumn(1);
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{200, 100, 400, 300, 600, 700},
            "Size sorting used formatted text or unstable ties");
    model.selectRow(1, false, false);
    model.selectRow(2, true, false);
    require(model.selectedIndices() == std::vector<std::uint64_t>{100, 400},
            "Sorted selection used displayed row ids instead of archive identities");
    model.setSortDescending(true);
    require(model.selectionCount() == 0, "Sort direction retained selection of reordered rows");
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{300, 100, 400, 200, 600, 700},
            "Descending sort inverted unknown values or stable duplicate order");
    model.setSortColumn(3);
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{100, 300, 400, 200, 600, 700},
            "Timestamp sorting ignored exact time values or unknown timestamps");
    model.setSortColumn(0);
    model.setSortDescending(false);
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{300, 100, 200, 400, 700, 600},
            "Name sorting did not preserve archive order for duplicate paths");
    model.selectRow(1, false, false);
    model.selectRow(3, false, true);
    require(model.selectedIndices() == std::vector<std::uint64_t>{100, 200, 400},
            "Sorted Shift selection did not map its visible range to archive identities");
    model.setSortColumn(2);
    finishQuery(model);
    require(visibleIndices(model).back() == 700, "Type sorting did not use actual entry kinds");
    model.setSortColumn(-1);
    require(visibleIndices(model) == std::vector<std::uint64_t>{100, 200, 300, 400, 600, 700},
            "Archive order restoration lost source positions");
    std::cout << "PASS numeric/date/type sort, stable ties and sorted multiselection\n";
}

void incrementalAndStaleResults()
{
    ArchiveEntryModel model;
    int insertions = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] { ++insertions; });
    model.append({entry(10, "match-z", 3), entry(11, "other", 1)});
    require(insertions == 1, "Default view did not insert metadata in a batch");
    model.setQuery(QStringLiteral("match"));
    model.setSortColumn(0);
    finishQuery(model);
    model.append({entry(12, "match-a", 2), entry(13, "irrelevant", 4)});
    finishQuery(model);
    require(model.totalCount() == 4 && visibleIndices(model) == std::vector<std::uint64_t>{12, 10},
            "Appending metadata to a filtered sorted view lost the new batch");
    model.setQuery(QStringLiteral("old archive"));
    model.clear();
    model.append({entry(55, "new archive", 5)});
    model.setQuery(QStringLiteral("new archive"));
    finishQuery(model);
    require(model.totalCount() == 1 && visibleIndices(model) == std::vector<std::uint64_t>{55},
            "Stale query results replaced a cleared/reopened archive");
    std::cout << "PASS batched append under filters and archive-generation isolation\n";
}

void largeIndexResponsivenessAndCancellation()
{
    ArchiveEntryModel model;
    constexpr int entries = 300000;
    for (int first = 0; first < entries; first += 1024) {
        std::vector<ArchiveEntry> batch;
        batch.reserve(1024);
        for (int row = first; row < std::min(first + 1024, entries); ++row)
            batch.push_back(entry(static_cast<std::uint64_t>(row),
                "large/" + std::to_string(row) + "/CAFÉ.txt", static_cast<std::uint64_t>(entries - row)));
        model.append(std::move(batch));
    }
    QElapsedTimer setter;
    setter.start();
    model.setSortColumn(0);
    require(setter.elapsed() < 200 && model.querying(),
            "Large-index sorting blocked the caller instead of scheduling work");
    bool heartbeat = false;
    QTimer::singleShot(0, [&] { heartbeat = true; });
    require(waitUntil([&] { return heartbeat; }), "UI event loop stopped during metadata processing");
    int completedViews = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++completedViews; });
    int nextId = entries;
    QTimer producer;
    producer.setInterval(1);
    QObject::connect(&producer, &QTimer::timeout, [&] {
        model.append({entry(static_cast<std::uint64_t>(nextId),
            "zz/live/" + std::to_string(nextId), 1)});
        ++nextId;
    });
    producer.start();
    require(waitUntil([&] { return completedViews > 0; }),
            "Continuous metadata append starved the running query sweep");
    producer.stop();
    finishQuery(model);
    const auto streamedCount = model.totalCount();
    for (int i = 0; i < 40; ++i) {
        model.setQuery(QString::number(i));
        model.setSortDescending((i & 1) != 0);
    }
    model.setQuery(QStringLiteral("/299999/"));
    model.setSortColumn(1);
    finishQuery(model);
    require(model.totalCount() == streamedCount && visibleIndices(model) == std::vector<std::uint64_t>{299999},
            "Rapidly cancelled query generations overwrote the latest result");
    model.setQuery(QStringLiteral("absent"));
    finishQuery(model);
    require(model.count() == 0, "No-match search retained stale metadata rows");
    model.clear();
    require(!model.querying() && model.count() == 0 && model.totalCount() == 0,
            "Clear retained worker state or metadata");
    {
        ArchiveEntryModel shuttingDown;
        shuttingDown.append({entry(1, "shutdown", 1)});
        shuttingDown.setQuery(QStringLiteral("shutdown"));
    }
    std::cout << "PASS 300k-entry asynchronous queries, responsive event loop, cancellation and shutdown\n";
}

void exactDuplicateExtractionAfterSort()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create sorted-extraction fixture directory");
    const auto root = qt::toPath(temporary.path());
    const auto archive = root / "duplicate-sort.zip";
    extraction_tests::createFixture(archive, extraction_tests::FixtureFormat::Zip,
        {{"same.txt", "FIRST-LARGER"}, {"same.txt", "tiny"}, {"a.txt", "aa"}});
    auto reader = makeLibArchiveReader();
    extraction_tests::succeeded(reader->open(archive), "Open duplicate fixture");
    std::vector<ArchiveEntry> entries;
    extraction_tests::succeeded(reader->enumerate([&](const ArchiveEntry& item) {
        entries.push_back(item);
        return true;
    }), "Enumerate duplicate fixture");
    ArchiveEntryModel model;
    model.append(std::move(entries));
    model.setQuery(QStringLiteral("same"));
    model.setSortColumn(1);
    finishQuery(model);
    model.selectRow(0, false, false);
    ExtractionRequest request;
    request.destination = root / "output";
    request.selectedIndices = model.selectedIndices();
    require(request.selectedIndices == std::vector<std::uint64_t>{1},
            "Sorted duplicate row selected the wrong backend identity");
    extraction_tests::succeeded(reader->extract(request), "Extract exact selected duplicate");
    std::ifstream output(request.destination / "same.txt", std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>{output}, std::istreambuf_iterator<char>{}};
    require(contents == "tiny" && !std::filesystem::exists(request.destination / "a.txt"),
            "Sorted filtered extraction wrote an unselected duplicate or hidden file");
    std::cout << "PASS exact duplicate entry extraction through sorted filtered metadata\n";
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    try {
        unicodeSearchAndSelection();
        numericSortAndStableIdentity();
        incrementalAndStaleResults();
        largeIndexResponsivenessAndCancellation();
        exactDuplicateExtractionAfterSort();
    } catch (const std::exception& exception) {
        std::cerr << "FAIL " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
