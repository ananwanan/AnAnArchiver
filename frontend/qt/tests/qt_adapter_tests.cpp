#include "controllers/archive_controller.h"
#include "models/file_tree_model.h"
#include "platform/path_conversion.h"
#include "platform/frontend_translator.h"
#include <ananarchiver/unicode.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()>& ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}

class FixtureReader final : public ananarchiver::ArchiveReader
{
public:
    ananarchiver::Result<ananarchiver::ArchiveInfo> open(
        const std::filesystem::path& path, ananarchiver::CancellationToken) override
    {
        prefix_ = ananarchiver::pathToUtf8(path.filename());
        if (prefix_ == "invalid")
            return std::unexpected(ananarchiver::ArchiveError{
                ananarchiver::ArchiveErrorCode::InvalidArchive, "Invalid fixture", {}, path});
        ananarchiver::ArchiveInfo info;
        info.path = path;
        info.formatName = "Fixture";
        return info;
    }

    ananarchiver::Result<void> enumerate(const ananarchiver::EntryCallback& callback,
                                        ananarchiver::CancellationToken token) override
    {
        const auto count = prefix_ == "cancelled" ? 100000 : 3000;
        for (int i = 0; i < count; ++i) {
            if (token.stop_requested())
                return std::unexpected(ananarchiver::ArchiveError{
                    ananarchiver::ArchiveErrorCode::Cancelled, "Cancelled fixture", {}, {}});
            ananarchiver::ArchiveEntry entry;
            entry.path = prefix_ + "/" + std::to_string(i);
            entry.size = 17;
            entry.type = ananarchiver::EntryType::RegularFile;
            if (!callback(entry))
                return std::unexpected(ananarchiver::ArchiveError{
                    ananarchiver::ArchiveErrorCode::Cancelled, "Cancelled callback", {}, {}});
        }
        return {};
    }

private:
    std::string prefix_;
};

void entryModelTest()
{
    ananarchiver::qt::FrontendTranslator translator;
    require(translator.translate("test", "Open archive") == QString::fromUtf8("打开压缩包"),
            "Chinese UI catalog missing");
    ananarchiver::qt::ArchiveEntryModel model;
    int insertions = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] { ++insertions; });
    std::vector<ananarchiver::ArchiveEntry> entries(512);
    entries.front().path = "中文/こんにちは/한국어/café/🐈.txt";
    entries.front().size = 1024;
    entries.front().type = ananarchiver::EntryType::RegularFile;
    entries[1].sizeKnown = false;
    entries[1].path = entries.front().path;
    for (std::size_t row = 0; row < entries.size(); ++row)
        entries[row].index = row;
    model.append(std::move(entries));
    require(model.rowCount() == 512 && insertions == 1, "Archive entries must insert in one batch");
    require(model.data(model.index(0), ananarchiver::qt::ArchiveEntryModel::PathRole).toString()
                == QString::fromUtf8("中文/こんにちは/한국어/café/🐈.txt"), "Unicode metadata changed");
    require(model.rowCount(model.index(0)) == 0, "Archive list unexpectedly has child rows");
    require(model.data(model.index(1), ananarchiver::qt::ArchiveEntryModel::SizeRole).toString()
                == QStringLiteral("Unknown"), "Unknown entry size was presented as zero");
    require(!model.data({}, ananarchiver::qt::ArchiveEntryModel::PathRole).isValid(),
            "Invalid QModelIndex has data");
    model.selectRow(0, false, false);
    model.selectRow(1, true, false);
    require(model.selectedIndices() == std::vector<std::uint64_t>{0, 1},
            "Duplicate names lost exact entry selection identity");
    model.selectRow(4, false, true);
    require(model.selectedIndices() == std::vector<std::uint64_t>{1, 2, 3, 4},
            "Shift selection did not preserve its anchor");
    model.selectAll();
    require(model.selectionCount() == 512 && model.selectedIndices().back() == 511,
            "Select-all omitted rows");
    model.clearSelection();
    require(model.selectionCount() == 0 && model.selectedIndices().empty(), "Selection clear retained ids");
    model.clear();
    require(model.rowCount() == 0, "Archive reset retained rows");
    std::cout << "PASS metadata batching and Unicode\n";
}

void directoryModelTest()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create temporary test directory");
    const auto root = ananarchiver::qt::toPath(temporary.path());
    const auto folder = root / ananarchiver::pathFromUtf8("中文🐈");
    std::filesystem::create_directory(folder);
    std::ofstream(root / "top.txt") << "top";
    std::ofstream(folder / "nested.txt") << "nested";
    std::filesystem::create_directory(folder / "deeper");
    std::ofstream(folder / "deeper" / "unvisited.txt") << "unvisited";

    ananarchiver::qt::FileTreeModel model;
    model.loadDirectory(temporary.path());
    require(model.rowCount() == 0, "Directory scan blocked the calling thread");
    require(waitUntil([&] { return model.rowCount() == 2; }), "Root children did not arrive");
    QModelIndex directory;
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row, 0);
        if (model.data(index, Qt::DisplayRole).toString() == QString::fromUtf8("中文🐈"))
            directory = index;
    }
    require(directory.isValid(), "Unicode directory missing");
    require(model.rowCount(directory) == 0 && model.canFetchMore(directory),
            "Directory children were recursively scanned before expansion");
    model.fetchMore(directory);
    require(waitUntil([&] { return model.rowCount(directory) == 2; }), "Expanded children did not arrive");
    for (int row = 0; row < model.rowCount(directory); ++row)
        require(model.parent(model.index(row, 0, directory)) == directory, "Tree parent contract violated");

    model.loadDirectory(temporary.path());
    model.loadDirectory(ananarchiver::qt::fromPath(folder / "deeper"));
    require(waitUntil([&] { return model.rowCount() == 1; }), "Latest directory scan did not arrive");
    require(model.data(model.index(0, 0), Qt::DisplayRole).toString() == QStringLiteral("unvisited.txt"),
            "Stale directory results replaced current scan");
    model.clear();
    require(model.rowCount() == 0 && !model.canFetchMore({}), "Clear left an active directory model");
    std::cout << "PASS asynchronous lazy directory loading and stale-result suppression\n";
}

void controllerTest()
{
    const auto factory = [] { return std::make_unique<FixtureReader>(); };
    ananarchiver::qt::ArchiveController controller(factory);
    int completions = 0;
    bool success = false;
    QObject::connect(&controller, &ananarchiver::qt::ArchiveController::operationFinished,
                     [&](bool result) { ++completions; success = result; });
    controller.openFile(QStringLiteral("first"));
    controller.openFile(QStringLiteral("second"));
    controller.openFile(QStringLiteral("latest"));
    require(waitUntil([&] { return !controller.busy() && completions == 1; }),
            "Rapid reopen did not finish the latest task");
    require(success && controller.entryCount() == 3000, "Latest task metadata incomplete");
    require(waitUntil([&] { return controller.canExtract(); }), "A completely indexed archive cannot be extracted");
    require(controller.entryModel()->rowCount() == 1 && controller.entryModel()->data(
        controller.entryModel()->index(0, 0), ananarchiver::qt::ArchiveEntryModel::DirectoryRole).toBool(),
        "Archive root did not expose its direct folder without descendant files");
    controller.navigateToFolder(QStringLiteral("latest"));
    require(waitUntil([&] { return controller.canExtract() && controller.entryModel()->rowCount() == 3000; }),
            "Reopened archive folder did not expose its direct files");
    controller.selectRow(1);
    controller.selectRow(3, true);
    require(controller.selectionCount() == 2, "Controller selection did not reach metadata model");
    for (int row = 0; row < controller.entryCount(); ++row)
        require(controller.entryModel()->data(controller.entryModel()->index(row, 0),
            ananarchiver::qt::ArchiveEntryModel::PathRole).toString().startsWith(QStringLiteral("latest/")),
            "Stale metadata mixed into reopened archive");

    controller.openFile(QStringLiteral("invalid"));
    require(waitUntil([&] { return !controller.busy(); }), "Invalid archive task remained running");
    require(!controller.errorMessage().isEmpty() && controller.entryCount() == 0,
            "Invalid archive did not show an error");
    require(!controller.canExtract(), "An invalid archive enabled extraction");
    controller.openFile(QStringLiteral("cancelled"));
    controller.cancel();
    require(waitUntil([&] { return !controller.busy(); }), "Cancellation did not finish");
    require(!success, "Cancelled task reported success");
    controller.openFile(QStringLiteral("closed"));
    controller.close();
    require(controller.entryCount() == 0 && !controller.busy() && controller.currentFile().isEmpty(),
            "Closing archive did not return Home");
    {
        ananarchiver::qt::ArchiveController shuttingDown(factory);
        shuttingDown.openFile(QStringLiteral("shutdown"));
    }
    std::cout << "PASS controller reopen, failure, cancellation, Home and managed shutdown\n";
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    try {
        entryModelTest();
        directoryModelTest();
        controllerTest();
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
