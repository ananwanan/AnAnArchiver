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

int rowForPath(ArchiveEntryModel& model, const QString& path, bool directory)
{
    for (int row = 0; row < model.count(); ++row)
        if (model.data(model.index(row), ArchiveEntryModel::PathRole).toString() == path &&
            model.data(model.index(row), ArchiveEntryModel::DirectoryRole).toBool() == directory)
            return row;
    return -1;
}

std::vector<std::string> visibleDirectories(ArchiveEntryModel& model)
{
    model.selectAll();
    const auto result = model.selectedDirectoryPaths();
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
    require(visibleIndices(model) == std::vector<std::uint64_t>{200, 100, 400, 300, 600},
            "Size sorting used formatted text or unstable ties");
    model.selectRow(1, false, false);
    model.selectRow(2, true, false);
    require(model.selectedIndices() == std::vector<std::uint64_t>{100, 400},
            "Sorted selection used displayed row ids instead of archive identities");
    model.setSortDescending(true);
    require(model.selectionCount() == 0, "Sort direction retained selection of reordered rows");
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{300, 100, 400, 200, 600},
            "Descending sort inverted unknown values or stable duplicate order");
    model.setSortColumn(3);
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{100, 300, 400, 200, 600},
            "Timestamp sorting ignored exact time values or unknown timestamps");
    model.setSortColumn(0);
    model.setSortDescending(false);
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{300, 100, 200, 400, 600},
            "Name sorting did not preserve archive order for duplicate paths");
    model.selectRow(1, false, false);
    model.selectRow(3, false, true);
    require(model.selectedIndices() == std::vector<std::uint64_t>{100, 200, 400},
            "Sorted Shift selection did not map its visible range to archive identities");
    model.setSortColumn(2);
    finishQuery(model);
    require(model.data(model.index(model.count() - 1), ArchiveEntryModel::DirectoryRole).toBool(),
            "Type sorting did not use actual entry kinds");
    model.setSortColumn(-1);
    require(visibleIndices(model) == std::vector<std::uint64_t>{100, 200, 300, 400, 600},
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
        {{"folder/same.txt", "FIRST-LARGER"}, {"folder/same.txt", "tiny"}, {"a.txt", "aa"}});
    auto reader = makeLibArchiveReader();
    extraction_tests::succeeded(reader->open(archive), "Open duplicate fixture");
    std::vector<ArchiveEntry> entries;
    extraction_tests::succeeded(reader->enumerate([&](const ArchiveEntry& item) {
        entries.push_back(item);
        return true;
    }), "Enumerate duplicate fixture");
    ArchiveEntryModel model;
    model.setFolderNavigationEnabled(true);
    model.append(std::move(entries));
    finishQuery(model);
    require(model.navigateToFolder(QStringLiteral("folder")), "Cannot navigate duplicate fixture folder");
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
    std::ifstream output(request.destination / "folder/same.txt", std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>{output}, std::istreambuf_iterator<char>{}};
    require(contents == "tiny" && !std::filesystem::exists(request.destination / "a.txt"),
            "Sorted filtered extraction wrote an unselected duplicate or hidden file");
    std::cout << "PASS exact duplicate entry extraction through sorted filtered folder metadata\n";
}

QModelIndex folderIndex(qt::ArchiveFolderModel& model, const QString& path,
                       const QModelIndex& parent = {})
{
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const auto item = model.index(row, 0, parent);
        if (model.data(item, qt::ArchiveFolderModel::PathRole).toString() == path)
            return item;
        const auto nested = folderIndex(model, path, item);
        if (nested.isValid())
            return nested;
    }
    return {};
}

void folderHierarchyAndScopedSelection()
{
    ArchiveEntryModel model;
    model.setFolderNavigationEnabled(true);
    auto empty = entry(80, "empty/", 0);
    empty.type = EntryType::Directory;
    auto explicitParent = entry(81, "中文/", 0);
    explicitParent.type = EntryType::Directory;
    model.append({entry(10, "root.txt", 1), entry(41, "中文/CAFÉ🐈.txt", 20),
                  entry(42, "中文/CAFÉ🐈.txt", 2), entry(43, "中文/嵌套/deep.txt", 4),
                  empty, explicitParent, entry(44, "synthetic\\child.txt", 3)});
    finishQuery(model);
    auto& folders = *model.folderModel();
    require(folders.rowCount() == 3 && folders.columnCount() == 1,
            "Folder tree included files, lost empty folders or duplicated explicit parents");
    const auto parent = folderIndex(folders, QString::fromUtf8("中文"));
    const auto child = folderIndex(folders, QString::fromUtf8("中文/嵌套"));
    require(parent.isValid() && child.isValid() && folders.parent(child) == parent &&
            !folders.parent(parent).isValid() && folders.rowCount(parent) == 1,
            "Implicit folder tree hierarchy or parent indices are incorrect");
    require(visibleIndices(model) == std::vector<std::uint64_t>{10},
            "Archive root included nested files in its extraction identities");
    require(model.count() == 4 && visibleDirectories(model) ==
            std::vector<std::string>{"empty", "synthetic", "中文"},
            "Root list did not include each direct folder exactly once ahead of files");
    require(model.data(model.index(0), ArchiveEntryModel::DirectoryRole).toBool() &&
            model.data(model.index(0), ArchiveEntryModel::SizeRole).toString().isEmpty() &&
            model.data(model.index(0), ArchiveEntryModel::NameRole).toString() == QStringLiteral("empty"),
            "Virtual folder row does not expose directory/name/blank-size roles");
    for (int column = -1; column <= 3; ++column) {
        for (const bool descending : {false, true}) {
            model.setSortColumn(column);
            model.setSortDescending(descending);
            finishQuery(model);
            for (int row = 0; row < 3; ++row)
                require(model.data(model.index(row), ArchiveEntryModel::DirectoryRole).toBool(),
                        "A file sorted ahead of a folder in the scoped browser");
            require(!model.data(model.index(3), ArchiveEntryModel::DirectoryRole).toBool() &&
                    visibleIndices(model) == std::vector<std::uint64_t>{10},
                    "Folder-first sorting changed the file identity or folder grouping");
        }
    }
    model.setSortColumn(-1);
    model.setSortDescending(false);
    finishQuery(model);
    require(model.navigateToFolder(QString::fromUtf8("中文/")), "Cannot enter Unicode folder");
    require(model.currentFolder() == QString::fromUtf8("中文") && model.count() == 0 && model.querying(),
            "Folder change did not normalize path or clear stale visible files immediately");
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{41, 42},
            "Folder listing included descendants or collapsed duplicate files");
    require(model.count() == 3 && visibleDirectories(model) == std::vector<std::string>{"中文/嵌套"},
            "Current folder list lost its direct implicit child folder");
    require(model.data(model.index(1), ArchiveEntryModel::NameRole).toString() == QString::fromUtf8("CAFÉ🐈.txt") &&
            model.data(model.index(1), ArchiveEntryModel::PathRole).toString() == QString::fromUtf8("中文/CAFÉ🐈.txt"),
            "File display name lost basename or full extraction path");
    require(!model.navigateToRow(1), "Activating a file incorrectly navigated to a folder");
    model.setQuery(QString::fromUtf8("嵌套"));
    finishQuery(model);
    require(model.count() == 1 && visibleIndices(model).empty() &&
            visibleDirectories(model) == std::vector<std::string>{"中文/嵌套"},
            "Scoped search did not match direct folder names independently of files");
    model.setQuery({});
    finishQuery(model);
    model.selectRow(0, false, false);
    require(model.selectedIndices().empty() &&
            model.selectedDirectoryPaths() == std::vector<std::string>{"中文/嵌套"},
            "Virtual folder selection invented a backend index or expanded descendants on the UI thread");
    require(model.navigateToRow(0), "Activating an implicit folder row did not navigate");
    finishQuery(model);
    require(model.currentFolder() == QString::fromUtf8("中文/嵌套") && model.selectionCount() == 0,
            "Folder row activation did not clear selection or set the new scope");
    model.navigateToFolder(QString::fromUtf8("中文"));
    finishQuery(model);
    int folderResets = 0;
    QObject::connect(&folders, &QAbstractItemModel::modelReset, [&] { ++folderResets; });
    model.setQuery(QString::fromUtf8("café🐈"));
    model.setSortColumn(1);
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{42, 41},
            "Scoped Unicode filtering/sorting did not retain original entry identities");
    model.selectRow(0, false, false);
    require(model.selectedIndices() == std::vector<std::uint64_t>{42},
            "Scoped duplicate selection targeted a different backend index");
    require(model.navigateToFolder(QString::fromUtf8("中文/嵌套")) && model.selectionCount() == 0,
            "Switching folders retained selected files from another folder");
    finishQuery(model);
    require(model.count() == 0, "Search leaked a matching file from another folder");
    model.setQuery(QStringLiteral("deep"));
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{43}, "Nested direct file was not visible");
    require(folderResets == 0, "Folder/search/sort changes reset the unchanged tree snapshot");
    require(!model.navigateToFolder(QStringLiteral("missing")) &&
            model.currentFolder() == QString::fromUtf8("中文/嵌套"),
            "Navigation accepted a nonexistent metadata folder");
    model.setQuery({});
    require(model.navigateToFolder(QStringLiteral("empty")), "Explicit empty folder was not navigable");
    finishQuery(model);
    require(model.count() == 0, "Empty folder displayed unrelated files");
    require(model.navigateToFolder(QStringLiteral("synthetic")), "Backslash implicit parent was lost");
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{44} &&
            model.data(model.index(0), ArchiveEntryModel::PathRole).toString() == QStringLiteral("synthetic\\child.txt"),
            "Metadata normalization changed the original backend pathname");
    ExtractionRequest totals;
    model.setExtractionTotals(totals, false);
    require(totals.totalEntries == 7 && totals.totalBytes == 30,
            "Folder navigation changed archive-wide extraction estimates");
    model.append({entry(45, "synthetic/second.txt", 1)});
    finishQuery(model);
    require(folderResets == 0 && visibleIndices(model) == std::vector<std::uint64_t>{45, 44},
            "Appending files to an existing folder reset the unchanged tree or lost direct children");
    model.clear();
    require(folders.rowCount() == 0 && model.currentFolder().isEmpty() && model.count() == 0,
            "Closing archive retained folder hierarchy or scope");
    model.append({entry(100, "fresh.txt", 1)});
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{100} && folders.rowCount() == 0,
            "Reopened archive inherited an old folder snapshot");
    std::cout << "PASS archive folder hierarchy, scoped Unicode search and exact file selection\n";
}

void largeFolderNavigationAndCancellation()
{
    ArchiveEntryModel model;
    model.setFolderNavigationEnabled(true);
    constexpr int entries = 300000;
    for (int first = 0; first < entries; first += 1024) {
        std::vector<ArchiveEntry> batch;
        batch.reserve(1024);
        for (int row = first; row < std::min(first + 1024, entries); ++row)
            batch.push_back(entry(static_cast<std::uint64_t>(row),
                "group" + std::to_string(row / 300) + "/file" + std::to_string(row) + ".txt", 1));
        model.append(std::move(batch));
    }
    finishQuery(model);
    require(model.count() == 1000 && model.folderModel()->rowCount() == 1000 && visibleIndices(model).empty(),
            "Large archive did not build its implicit folder hierarchy");
    QElapsedTimer setter;
    setter.start();
    require(model.navigateToFolder(QStringLiteral("group999")), "Cannot navigate large metadata index");
    require(setter.elapsed() < 200 && model.querying(), "Folder navigation blocked the UI caller");
    bool heartbeat = false;
    QTimer::singleShot(0, [&] { heartbeat = true; });
    for (int folder = 0; folder < 30; ++folder)
        require(model.navigateToFolder(QStringLiteral("group%1").arg(folder)), "Rapid folder navigation failed");
    require(model.navigateToFolder(QStringLiteral("group999")), "Final folder navigation failed");
    model.setQuery(QStringLiteral("299999"));
    model.setSortColumn(1);
    finishQuery(model);
    require(heartbeat && visibleIndices(model) == std::vector<std::uint64_t>{299999},
            "Cancelled folder generations replaced the latest scope or blocked the event loop");
    model.navigateToFolder(QStringLiteral("group0"));
    model.clear();
    model.setQuery({});
    model.append({entry(900000, "replacement.txt", 1)});
    finishQuery(model);
    require(model.currentFolder().isEmpty() && model.folderModel()->rowCount() == 0 &&
            visibleIndices(model) == std::vector<std::uint64_t>{900000},
            "Stale folder query replaced a closed/reopened archive");
    std::cout << "PASS 300k-entry folder navigation responsiveness and cancellation isolation\n";
}

void hostileFolderDepthBudgetAndRecovery()
{
    ArchiveEntryModel model;
    model.setFolderNavigationEnabled(true);
    model.append({entry(1, "safe.txt", 1)});
    finishQuery(model);
    model.selectAll();
    require(model.selectionCount() == 1 && model.viewValid(), "Safe initial folder view is invalid");
    QString failure;
    QObject::connect(&model, &ArchiveEntryModel::queryFailed,
                     [&](const QString& message) { failure = message; });
    // Fits the Core's 64 KiB pathname limit but would create roughly 32k
    // cumulative prefixes, amplifying one metadata entry into gigabytes.
    std::string deepPath;
    deepPath.reserve(64U * 1024U);
    while (deepPath.size() + 2U + 8U <= 64U * 1024U)
        deepPath += "a/";
    deepPath += "file.txt";
    QElapsedTimer elapsed;
    elapsed.start();
    model.append({entry(2, std::move(deepPath), 1)});
    finishQuery(model);
    require(elapsed.elapsed() < 2000 && failure.contains(QStringLiteral("depth limit")) &&
            !model.viewValid() && model.folderModel()->rowCount() == 0,
            "Deep metadata did not fail quickly within the folder hierarchy resource budget");
    model.selectAll();
    model.selectRow(0, false, false);
    require(model.selectionCount() == 0 && model.selectedIndices().empty(),
            "Failed hierarchy allowed selection/extraction of stale visible files");
    model.clear();
    require(model.viewValid() && model.currentFolder().isEmpty(), "Closing failed hierarchy did not reset validity");
    model.append({entry(20, "recovered/安安.txt", 1)});
    finishQuery(model);
    require(model.viewValid() && model.navigateToFolder(QStringLiteral("recovered")),
            "New archive did not recover from a rejected hierarchy");
    finishQuery(model);
    require(visibleIndices(model) == std::vector<std::uint64_t>{20},
            "Recovered hierarchy inherited failed metadata or lost extraction identity");
    std::cout << "PASS 64 KiB hostile folder-depth budget, invalid-view selection guard and reopen recovery\n";
}

void implicitFolderAndMixedSelectionExtraction()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create directory-selection fixture directory");
    const auto root = qt::toPath(temporary.path());
    const auto archive = root / "folder-selection.zip";
    extraction_tests::createFixture(archive, extraction_tests::FixtureFormat::Zip,
        {{"docs/first.txt", "first"}, {"docs/nested/child.txt", "nested"},
         {"docs-last.txt", "unselected sibling"}, {"empty/", {}, EntryType::Directory},
         {"keep.txt", "selected file"}, {"unselected.txt", "unselected root file"}});
    auto reader = makeLibArchiveReader();
    extraction_tests::succeeded(reader->open(archive), "Open folder-selection fixture");
    std::vector<ArchiveEntry> entries;
    extraction_tests::succeeded(reader->enumerate([&](const ArchiveEntry& item) {
        entries.push_back(item);
        return true;
    }), "Enumerate folder-selection fixture");
    ArchiveEntryModel model;
    model.setFolderNavigationEnabled(true);
    model.append(std::move(entries));
    finishQuery(model);
    const auto docs = rowForPath(model, QStringLiteral("docs"), true);
    const auto empty = rowForPath(model, QStringLiteral("empty"), true);
    const auto keep = rowForPath(model, QStringLiteral("keep.txt"), false);
    require(docs >= 0 && empty >= 0 && keep >= 0, "Missing direct folder/file rows in selection fixture");
    model.selectRow(docs, false, false);
    model.selectRow(empty, true, false);
    require(model.selectedIndices().empty() &&
            model.selectedDirectoryPaths() == std::vector<std::string>{"docs", "empty"},
            "Directory selection included synthetic IDs or expanded implicit descendants");
    model.selectRow(keep, true, false);
    ExtractionRequest request;
    request.destination = root / "output";
    request.selectedIndices = model.selectedIndices();
    request.selectedDirectoryPaths = model.selectedDirectoryPaths();
    require(request.selectedIndices == std::vector<std::uint64_t>{4} && model.selectionCount() == 3,
            "Mixed file/folder selection lost the original real-file identity");
    model.setExtractionTotals(request, true);
    require(!request.totalEntries && !request.totalBytes,
            "Selected directory extraction claimed totals without backend expansion");
    extraction_tests::succeeded(reader->extract(request), "Extract selected implicit folder and root file");
    require(std::filesystem::is_regular_file(request.destination / "docs/first.txt") &&
            std::filesystem::is_regular_file(request.destination / "docs/nested/child.txt") &&
            std::filesystem::is_directory(request.destination / "empty") &&
            std::filesystem::is_regular_file(request.destination / "keep.txt") &&
            !std::filesystem::exists(request.destination / "docs-last.txt") &&
            !std::filesystem::exists(request.destination / "unselected.txt"),
            "Folder selection lost descendants/empty directories or extracted unselected siblings");
    std::cout << "PASS direct-folder selection, implicit descendants, empty folders and mixed extraction\n";
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
        folderHierarchyAndScopedSelection();
        largeFolderNavigationAndCancellation();
        hostileFolderDepthBudgetAndRecovery();
        implicitFolderAndMixedSelectionExtraction();
    } catch (const std::exception& exception) {
        std::cerr << "FAIL " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
