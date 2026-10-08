#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "controllers/drag_controller.h"
#include "platform/path_conversion.h"
#include "platform/temp_file_manager.h"
#include "../../../tests/security/extraction_fixtures.h"

#include <QElapsedTimer>
#include <QDir>
#include <QDrag>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>
#include <atomic>
#include <fstream>
#include <thread>

namespace {

using namespace ananarchiver;
using namespace extraction_tests;

bool waitUntil(const std::function<bool()>& predicate) {
    QElapsedTimer timeout;
    timeout.start();
    while (!predicate() && timeout.elapsed() < 10000) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

QString droppedPath(const std::filesystem::path& path) {
    // QUrl's local-file wire representation uses forward separators on
    // Windows. Retain exact path assertions after that defined conversion.
    return QDir::fromNativeSeparators(qt::fromPath(path));
}

struct Controllers {
    qt::ArchiveController archive;
    qt::CompressionController compression;
    qt::DragController drag{archive, compression};
    QString opened;
    int creates{};

    Controllers() {
        QObject::connect(&drag, &qt::DragController::openRequested, [&](const QString& path) { opened = path; });
        QObject::connect(&drag, &qt::DragController::createRequested, [&] { ++creates; });
    }

    void open(const std::filesystem::path& path) {
        archive.openFile(qt::fromPath(path));
        check(waitUntil([&] { return !archive.busy(); }), "Archive indexing timed out");
        check(archive.canExtract(), "Archive did not index: " + archive.errorDetails().toStdString());
    }

    void exportSelection(const QString& password = {}) {
        drag.prepareExport(password);
        check(drag.busy(), "Export did not start on a worker");
        check(waitUntil([&] { return !drag.busy(); }), "Export timed out");
        check(drag.ready(), "Export did not become ready: " + drag.errorDetails().toStdString());
    }
};

void contentDetection() {
    TemporaryDirectory temp;
    const auto archive = temp.path / "misleading.dat";
    createFixture(archive, FixtureFormat::Zip, {{"entry.txt", "archive"}});
    Controllers view;
    int failedTasks{};
    QObject::connect(&view.drag, &qt::DragController::taskObserved,
        [&](const application::TaskSnapshot& snapshot, const QString&) {
            if (snapshot.state == application::TaskState::Failed)
                ++failedTasks;
        });
    view.drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(archive))});
    check(view.drag.busy(), "Single-file content detection blocked the GUI");
    check(waitUntil([&] { return !view.drag.busy(); }), "Drop probe timed out");
    check(view.opened == droppedPath(archive), "Content-detected ZIP with a .dat extension did not open; actual=" +
          view.opened.toStdString() + "; expected=" + droppedPath(archive).toStdString() + "; error=" + view.drag.errorDetails().toStdString());
    check(view.creates == 0 && view.compression.inputCount() == 0, "Archive drop entered compression");

    const auto ordinary = temp.path / "ordinary.zip";
    writeFile(ordinary, "ordinary text pretending to be a ZIP by extension");
    view.opened.clear();
    view.drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(ordinary))});
    check(waitUntil([&] { return !view.drag.busy(); }), "Ordinary-file drop probe timed out");
    check(view.opened.isEmpty() && view.creates == 1, "Ordinary .zip file was classified by extension");
    check(failedTasks == 0, "Successful ordinary-file classification appeared as an error in the task center");
    check(view.compression.inputs() == QStringList{droppedPath(ordinary)}, "Ordinary input lost its path");

    const auto stream = temp.path / "stream.blob";
    createFixture(stream, FixtureFormat::TarGzip, {{"streamed.txt", "data"}});
    view.drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(stream))});
    check(waitUntil([&] { return !view.drag.busy(); }), "TAR.GZ detection timed out");
    check(view.opened == droppedPath(stream), "Streaming TAR.GZ content was not detected");
}

void localInputsAndRemoteRejection() {
    TemporaryDirectory temp;
    const auto folder = temp.path / pathFromUtf8("安安🐈");
    std::filesystem::create_directory(folder);
    const auto file = temp.path / "plain.txt";
    writeFile(file, "file");
    Controllers view;
    view.drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(folder))});
    check(view.drag.busy(), "Directory inspection ran synchronously");
    check(waitUntil([&] { return !view.drag.busy(); }), "Directory drop timed out");
    check(view.creates == 1 && view.compression.inputs().contains(droppedPath(folder)), "Directory was not added for compression; actual=" +
          view.compression.inputs().join(';').toStdString() + "; expected=" + droppedPath(folder).toStdString());
    view.drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(file)), QUrl::fromLocalFile(qt::fromPath(folder))});
    check(view.creates == 2 && view.compression.inputCount() == 2, "Multiple local URLs were not added/deduplicated");
    const auto count = view.compression.inputCount();
    view.drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(temp.path / "unaccepted.txt")), QUrl("https://example.com/file.zip")});
    check(view.compression.inputCount() == count && view.creates == 2 && !view.drag.errorMessage().isEmpty(),
          "Remote mixed drop was partially accepted");
    view.drag.dismissError();
    check(view.drag.errorMessage().isEmpty() && view.drag.errorDetails().isEmpty(), "Dismiss retained drop error details");
    view.drag.dropUrls({QUrl("file://remote-server/share/file.zip")});
    check(!view.drag.errorMessage().isEmpty() && view.compression.inputCount() == count, "Remote file URL was accepted");
}

class PasswordProbe final : public ArchiveReader {
  public:
    Result<ArchiveInfo> open(const std::filesystem::path& path, CancellationToken) override {
        return std::unexpected(ArchiveError{ArchiveErrorCode::PasswordRequired,
                                            "Encrypted headers require a password", {}, path});
    }
    Result<void> enumerate(const EntryCallback&, CancellationToken) override {
        throw std::runtime_error("Drop probe must not enumerate the archive");
    }
};

void passwordRequiredDrop() {
    TemporaryDirectory temp;
    const auto input = temp.path / "encrypted.payload";
    writeFile(input, "fixture probe input");
    qt::ArchiveController archive;
    qt::CompressionController compression;
    qt::DragController drag(archive, compression, nullptr, [] { return std::make_unique<PasswordProbe>(); });
    QString opened;
    QObject::connect(&drag, &qt::DragController::openRequested, [&](const QString& path) { opened = path; });
    drag.dropUrls({QUrl::fromLocalFile(qt::fromPath(input))});
    check(waitUntil([&] { return !drag.busy(); }), "Encrypted-header drop timed out");
    check(opened == droppedPath(input) && compression.inputCount() == 0,
          "Password-required archive was sent to compression; actual=" + opened.toStdString() + "; expected=" + droppedPath(input).toStdString());
}

void exportSortedSelectionAndLinks() {
    TemporaryDirectory temp;
    const auto input = temp.path / "selection.tar";
    createFixture(input, FixtureFormat::Tar, {
        {"z.txt", "z"}, {"a.txt", "a"}, {"folder/中文🐈.txt", "unicode"},
        {"link.txt", {}, EntryType::Symlink, "a.txt"}});
    Controllers view;
    view.open(input);
    auto* model = qobject_cast<qt::ArchiveEntryModel*>(view.archive.entryModel());
    model->setSortColumn(0);
    check(waitUntil([&] { return !model->querying(); }), "Sorting timed out");
    check(model->data(model->index(0), qt::ArchiveEntryModel::PathRole).toString() == QStringLiteral("a.txt"),
          "Fixture did not reorder its original enumeration identities");
    view.archive.selectRow(0);
    const auto selected = view.archive.selectedExtractionRequest({})->selectedIndices;
    check(selected && *selected == std::vector<std::uint64_t>{1},
          "Sorted selection did not retain the original index");
    view.exportSelection();
    check(view.drag.exportUrls().size() == 1, "Single selection exported unrelated top-level entries");
    const auto exported = qt::toPath(view.drag.exportUrls().first().toLocalFile());
    check(exported.filename() == "a.txt" && readFile(exported) == "a", "Export used a sorted row number instead of the original entry");
    const auto firstRoot = exported.parent_path();
    view.archive.clearSelection();
    check(!view.drag.ready(), "Selection changes left stale file URLs ready");
    check(waitUntil([&] { return !std::filesystem::exists(firstRoot); }), "Invalidated export was not cleaned on a worker");

    view.archive.selectAll();
    view.exportSelection();
    const auto urls = view.drag.exportUrls();
    check(urls.size() == 3, "Link-skipping export did not produce the expected files/folder");
    std::filesystem::path root;
    for (const auto& url : urls) {
        check(url.isLocalFile(), "Export generated a remote URL");
        const auto path = qt::toPath(url.toLocalFile());
        root = path.parent_path();
        check(!std::filesystem::is_symlink(path), "Export retained a symlink");
    }
    check(readFile(root / pathFromUtf8("folder/中文🐈.txt")) == "unicode", "Nested Unicode export lost contents");
    check(!std::filesystem::exists(root / "link.txt"), "Export created a selected symbolic link");
    view.drag.cancel();
    check(!view.drag.ready() && waitUntil([&] { return !std::filesystem::exists(root); }), "Cancelled ready export was not cleaned");
}

void longPathExport() {
    TemporaryDirectory temp;
    const auto archive = temp.path / "long-path.zip";
    const auto name = std::string(235, 'a') + ".txt";
    createFixture(archive, FixtureFormat::Zip, {{name, "long native path"}});
    Controllers view;
    view.open(archive);
    view.archive.selectAll();
    view.exportSelection();
    check(view.drag.exportUrls().size() == 1, "Long filename export lost its URL");
    const auto path = view.drag.exportUrls().first().toLocalFile();
    check(path.size() > 260, "Long-path fixture did not exceed MAX_PATH");
    QFile file(path);
    check(file.open(QIODevice::ReadOnly) && file.readAll() == QByteArray("long native path"),
          "Exported long path could not be read through the Qt native adapter");
    file.close();
    const auto root = QFileInfo(path).path();
    view.drag.cancel();
    check(waitUntil([&] { return !QFileInfo::exists(root); }), "Long export root was not cleaned through the native adapter");
}

void hiddenFileExport() {
    TemporaryDirectory temp;
    const auto archive = temp.path / "dotfile.zip";
    createFixture(archive, FixtureFormat::Zip, {{".config", "selected hidden file"}});
    Controllers view;
    view.open(archive);
    view.archive.selectAll();
    view.exportSelection();
    check(view.drag.exportUrls().size() == 1 &&
          readFile(qt::toPath(view.drag.exportUrls().first().toLocalFile())) == "selected hidden file",
          "Selected hidden file was omitted from native file URLs");
}

void nativeDragBoundaryAndNestedEvents() {
    TemporaryDirectory temp;
    const auto input = temp.path / "native-drag.zip";
    createFixture(input, FixtureFormat::Zip, {{"copy.txt", "copy payload"}});
    qt::ArchiveController archive;
    qt::CompressionController compression;
    archive.openFile(qt::fromPath(input));
    check(waitUntil([&] { return !archive.busy(); }), "Native-boundary fixture indexing timed out");
    archive.selectAll();
    int executed{};
    std::filesystem::path exported;
    qt::DragController* controller{};
    {
        qt::DragController drag(archive, compression, nullptr, {}, [&](QDrag& native) {
            ++executed;
            check(native.source() == controller, "QDrag lost its live QObject source");
            check(native.mimeData() && native.mimeData()->hasFormat(QStringLiteral("text/uri-list")) &&
                  native.mimeData()->urls() == controller->exportUrls(), "Native MIME lost the ready local-file URL list");
            exported = qt::toPath(native.mimeData()->urls().first().toLocalFile());
            QEventLoop nested;
            QTimer::singleShot(0, [&] {
                check(controller->busy() && !controller->ready(), "Native drag did not guard reentry");
                controller->startDrag();
                controller->cancel();
                if (executed == 2)
                    archive.clearSelection();
                check(readFile(exported) == "copy payload", "Native cancellation deleted its still-used source");
            });
            QTimer::singleShot(30, &nested, &QEventLoop::quit);
            nested.exec();
            return Qt::IgnoreAction;
        });
        controller = &drag;
        drag.prepareExport();
        check(waitUntil([&] { return !drag.busy(); }), "Native-boundary export timed out");
        check(drag.ready() && executed == 0, "Preparing files triggered a delayed native drag");
        drag.startDrag();
        check(executed == 1 && drag.ready() && !drag.busy(), "Cancelled native-boundary drag did not return to ready");
        drag.startDrag();
        check(executed == 2, "A repeated explicit drag did not reuse ready files");
        check(!drag.ready() && std::filesystem::exists(exported), "Selection invalidation removed OS-retained output");
    }
    check(!std::filesystem::exists(exported.parent_path()), "Native-boundary shutdown did not clean retained output");

    archive.selectAll();
    qt::DragController production(archive, compression);
    production.prepareExport();
    check(waitUntil([&] { return !production.busy(); }), "Gesture-guard fixture export timed out");
    production.startDrag(); // No physical button is held in offscreen mode.
    check(production.ready() && !production.busy(), "Ready menu/keyboard action started a native drag without a gesture");
}

void unsafeExportAndEncryptedFiles() {
    TemporaryDirectory temp;
    const auto unsafe = temp.path / "unsafe.tar";
    createFixture(unsafe, FixtureFormat::Tar, {{"../escape.txt", "unsafe"}});
    Controllers view;
    view.open(unsafe);
    view.archive.selectAll();
    view.drag.prepareExport();
    check(waitUntil([&] { return !view.drag.busy(); }), "Unsafe export timed out");
    check(!view.drag.ready() && !view.drag.errorMessage().isEmpty(), "Unsafe export became draggable");
    check(!std::filesystem::exists(temp.path / "escape.txt"), "Export path traversal escaped its root");

    const auto encrypted = temp.path / "encrypted.zip";
    createFixture(encrypted, FixtureFormat::Zip, {{"secret.txt", "encrypted fixture"}}, true);
    view.open(encrypted);
    view.archive.selectAll();
    view.drag.prepareExport();
    check(waitUntil([&] { return !view.drag.busy(); }), "Password-free encrypted export timed out");
    check(!view.drag.ready() && !view.drag.errorMessage().isEmpty(), "Encrypted export silently succeeded without a password");
    view.exportSelection(QStringLiteral("test-only-passphrase"));
    check(readFile(qt::toPath(view.drag.exportUrls().first().toLocalFile())) == "encrypted fixture",
          "Password export did not preserve plaintext contents");
}

struct SlowState {
    std::atomic<bool> began{};
    std::filesystem::path output;
};

class SlowExportReader final : public ArchiveReader {
  public:
    explicit SlowExportReader(std::shared_ptr<SlowState> state) : state_(std::move(state)) {}
    Result<ArchiveInfo> open(const std::filesystem::path& path, CancellationToken) override {
        ArchiveInfo info;
        info.path = path;
        info.format = ArchiveFormat::Zip;
        return info;
    }
    Result<void> enumerate(const EntryCallback&, CancellationToken) override { return {}; }
    Result<void> extract(const ExtractionRequest& request, const ProgressCallback&, CancellationToken stop) override {
        state_->output = request.destination;
        writeFile(request.destination / "partial.txt", "partial output");
        state_->began = true;
        while (!stop.stop_requested())
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled, "Slow fixture cancelled", {}, {}});
    }
  private:
    std::shared_ptr<SlowState> state_;
};

void cancellableExportAndShutdown() {
    TemporaryDirectory temp;
    const auto input = temp.path / "cancel.zip";
    createFixture(input, FixtureFormat::Zip, {{"partial.txt", "payload"}});
    qt::ArchiveController archive;
    qt::CompressionController compression;
    archive.openFile(qt::fromPath(input));
    check(waitUntil([&] { return !archive.busy(); }), "Cancel fixture indexing timed out");
    archive.selectAll();
    auto state = std::make_shared<SlowState>();
    int heartbeat = 0;
    int terminalTasks = 0;
    QTimer alive;
    alive.setInterval(5);
    QObject::connect(&alive, &QTimer::timeout, [&] { ++heartbeat; });
    alive.start();
    {
        qt::DragController drag(archive, compression, nullptr,
                               [state] { return std::make_unique<SlowExportReader>(state); });
        QObject::connect(&drag, &qt::DragController::taskObserved,
            [&](const application::TaskSnapshot& snapshot, const QString&) {
                if (snapshot.state == application::TaskState::Cancelled)
                    ++terminalTasks;
            });
        drag.prepareExport();
        check(waitUntil([&] { return state->began.load(); }), "Cancellable extraction did not start");
        drag.cancel();
        check(waitUntil([&] { return !drag.busy(); }), "Drag cancellation did not finish");
        check(!drag.ready() && terminalTasks > 0 && heartbeat > 0, "Cancellation lost task history or blocked the UI");
        check(waitUntil([&] { return !std::filesystem::exists(state->output); }), "Cancelled partial output was not cleaned");

        state->began = false;
        drag.prepareExport();
        check(waitUntil([&] { return state->began.load(); }), "Shutdown extraction did not start");
        // Destroying the controller cancels and joins the worker; cleanup does
        // not race a producer and no detached thread survives this scope.
    }
    check(!std::filesystem::exists(state->output), "Shutdown left a private partial export directory");
}

void managedTemporaryRetention() {
    TemporaryDirectory temp;
    std::filesystem::path retained;
    {
        qt::TempFileManager manager(temp.path);
        Result<std::filesystem::path> created;
        std::jthread creator([&] { created = manager.create(); });
        creator.join();
        succeeded(created, "Managed temporary creation");
        retained = *created;
        writeFile(retained / "desktop-copy.txt", "ready");
        manager.retain(retained, std::chrono::seconds{1});
        check(std::filesystem::exists(retained / "desktop-copy.txt"), "Retained output disappeared before desktop copy");
        check(waitUntil([&] { return !std::filesystem::exists(retained); }), "Expired drag output was not cleaned");
        std::jthread next([&] { created = manager.create(); });
        next.join();
        succeeded(created, "Second managed temporary creation");
        retained = *created;
    }
    check(!std::filesystem::exists(retained), "Temp manager shutdown left its owned directory");
}

void boundedTemporaryBacklog() {
    TemporaryDirectory temp;
    qt::TempFileManager manager(temp.path);
    std::vector<std::filesystem::path> roots;
    std::vector<Result<std::filesystem::path>> results(8);
    std::vector<std::jthread> creators;
    for (std::size_t index = 0; index < results.size(); ++index)
        creators.emplace_back([&, index] { results[index] = manager.create(); });
    for (auto& creator : creators)
        creator.join();
    for (const auto& result : results) {
        if (result)
            roots.push_back(*result);
        else
            check(result.error().code == ArchiveErrorCode::ResourceLimitExceeded, "Concurrent export creation failed for an unexpected reason");
    }
    check(roots.size() == 4, "Concurrent creations bypassed the four-root reservation limit");
    manager.discard(roots.front());
    check(waitUntil([&] { return !std::filesystem::exists(roots.front()); }), "Bounded backlog did not free discarded output");
    Result<std::filesystem::path> overflow;
    std::jthread replacement([&] { overflow = manager.create(); });
    replacement.join();
    succeeded(overflow, "Cleaned backlog did not allow another export");
}

void cleanupFailureDiagnostics() {
#ifdef _WIN32
    TemporaryDirectory temp;
    qt::TempFileManager manager(temp.path);
    Result<std::filesystem::path> result;
    std::jthread creator([&] { result = manager.create(); });
    creator.join();
    succeeded(result, "Locked cleanup fixture creation");
    QFile locked(qt::fromPath(*result / "locked.txt"));
    check(locked.open(QIODevice::WriteOnly), "Could not lock the cleanup fixture file");
    manager.discard(*result);
    std::vector<ArchiveError> errors;
    check(waitUntil([&] {
        if (errors.empty())
            errors = manager.takeCleanupErrors();
        return !errors.empty();
    }), "Three failed cleanup attempts did not report a diagnostic");
    check(errors.front().code == ArchiveErrorCode::CleanupFailed && errors.front().path == *result,
          "Cleanup diagnostic omitted the precise remaining private root");
    locked.close();
    manager.discard(*result);
    check(waitUntil([&] { return !QFileInfo::exists(qt::fromPath(*result)); }), "Cleanup did not recover after the desktop released its file");
#endif
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    Tests tests;
    tests.run("Drop content detection independent of file extension", contentDetection);
    tests.run("Local directory/multi-file input and atomic remote URL rejection", localInputsAndRemoteRejection);
    tests.run("Encrypted header drop routes to archive opening", passwordRequiredDrop);
    tests.run("Sorted original selection, Unicode export and link skipping", exportSortedSelectionAndLinks);
    tests.run("Native long-path file URLs and worker cleanup", longPathExport);
    tests.run("Selected hidden files remain in exported URLs", hiddenFileExport);
    tests.run("Native MIME/source boundary, nested reentry and gesture guard", nativeDragBoundaryAndNestedEvents);
    tests.run("Unsafe path rejection and encrypted drag extraction", unsafeExportAndEncryptedFiles);
    tests.run("Asynchronous cancellation, task history and shutdown cleanup", cancellableExportAndShutdown);
    tests.run("Desktop-copy retention and managed temporary cleanup", managedTemporaryRetention);
    tests.run("Retained export backlog is bounded", boundedTemporaryBacklog);
    tests.run("Locked cleanup retries preserve remaining-root diagnostics", cleanupFailureDiagnostics);
    return tests.finish();
}
