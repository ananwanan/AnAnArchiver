#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "controllers/desktop_controller.h"
#include "controllers/task_controller.h"
#include "platform/path_conversion.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace ananarchiver;
using namespace ananarchiver::qt;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool waitUntil(const std::function<bool()>& condition) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < 5000) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}
class Reader final : public ArchiveReader {
  public:
    Result<ArchiveInfo> open(const std::filesystem::path& file, CancellationToken) override {
        ArchiveInfo info;
        info.path = file;
        info.formatName = "Fixture";
        return info;
    }
    Result<void> enumerate(const EntryCallback& callback, CancellationToken token) override {
        for (int i = 0; i < 5000; ++i) {
            if (token.stop_requested())
                return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
            ArchiveEntry entry;
            entry.path = "fixture/" + std::to_string(i);
            entry.size = 1;
            entry.type = EntryType::RegularFile;
            if (!callback(entry))
                return std::unexpected(ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
        }
        return {};
    }
};
void settings() {
    QTemporaryDir temporary;
    require(temporary.isValid(), "settings fixture failed");
    const auto file = temporary.filePath(QStringLiteral("settings.ini"));
    const auto archive = temporary.filePath(QString::fromUtf8("中文🐈.zip"));
    {
        DesktopController controller(file);
        controller.setThemeMode(2);
        controller.setThemeMode(8);
        require(controller.themeMode() == 2, "invalid theme accepted");
        controller.rememberArchive(archive);
        controller.rememberArchive(archive);
        require(controller.recentArchives().size() == 1, "duplicate recent path");
        controller.rememberArchive(QStringLiteral("relative.zip"));
        require(controller.recentArchives().size() == 1, "relative recent path accepted");
        QString opened;
        QObject::connect(&controller, &DesktopController::openRequested,
            [&opened](const QString& path) { opened = path; });
        controller.openRecent(archive);
        require(opened == archive, "recent reopen lost Unicode path");
        require(waitUntil([&] { return !controller.saving(); }) && controller.settingsError().isEmpty(), "settings save failed");
    }
    DesktopController restored(file);
    require(restored.themeMode() == 2 && restored.recentArchives().size() == 1, "settings not persisted");
    for (int i = 0; i < 30; ++i)
        restored.rememberArchive(temporary.filePath(QStringLiteral("%1.zip").arg(i)));
    require(restored.recentArchives().size() == 20, "recent history unbounded");
    const auto newest = restored.recentArchives().first().toMap().value(QStringLiteral("path")).toString();
    require(newest.endsWith(QStringLiteral("29.zip")), "recent order wrong");
    restored.removeRecent(newest);
    require(restored.recentArchives().size() == 19, "remove recent failed");
    restored.clearRecent();
    require(restored.recentArchives().isEmpty(), "clear recent failed");
    require(waitUntil([&] { return !restored.saving(); }), "settings persistence did not finish");
    require(DesktopController(file).recentArchives().isEmpty(), "clear recent not persisted");
}
void tasks() {
    TaskController center;
    application::TaskSnapshot snapshot;
    snapshot.id = 1;
    snapshot.type = application::TaskType::ExtractArchive;
    snapshot.state = application::TaskState::Running;
    snapshot.totalBytes = 200;
    snapshot.processedBytes = 100;
    application::TaskId cancelled = 0;
    center.observe(QStringLiteral("archive"), snapshot, QStringLiteral("output"),
        [&cancelled](auto id) { cancelled = id; });
    require(center.activeCount() == 1 && center.count() == 1, "task record missing");
    require(center.data(center.index(0), TaskController::ProgressRole).toDouble() == 0.5, "task progress wrong");
    center.cancelTask(QStringLiteral("archive:1"));
    require(cancelled == 1, "cancel not routed to original task");
    snapshot.state = application::TaskState::Failed;
    snapshot.error = ArchiveError{ArchiveErrorCode::CorruptedArchive, "bad <img src=x>", "CRC failed", {}};
    center.observe(QStringLiteral("archive"), snapshot, {}, {});
    require(center.count() == 1 && center.activeCount() == 0, "finished task duplicated or remained active");
    require(center.data(center.index(0), TaskController::DetailsRole).toString().contains(QStringLiteral("CRC failed")), "error detail lost");
    for (std::uint64_t i = 2; i < 90; ++i) {
        snapshot.id = i;
        center.observe(QStringLiteral("archive"), snapshot, {}, {});
    }
    require(center.count() == 64, "task session history unbounded");
    snapshot.id = 1;
    snapshot.state = application::TaskState::Running;
    snapshot.type = application::TaskType::OpenArchive;
    center.observe(QStringLiteral("compress"), snapshot, {}, {});
    require(center.activeCount() == 1, "controller ids collided");
    require(center.data(center.index(0), TaskController::ProgressRole).toDouble() < 0, "metadata fabricated byte progress");
    center.clearFinished();
    require(center.count() == 1 && center.activeCount() == 1, "clear finished removed active operation");
}
void persistenceFailureAndRetry() {
    QTemporaryDir temporary;
    const auto parent = temporary.filePath(QStringLiteral("blocked"));
    QFile blockingFile(parent);
    require(blockingFile.open(QIODevice::WriteOnly), "blocked settings fixture failed");
    blockingFile.write("file");
    blockingFile.close();
    DesktopController controller(parent + QStringLiteral("/preferences.ini"));
    controller.setThemeMode(1);
    require(waitUntil([&] { return !controller.saving(); }) && !controller.settingsError().isEmpty(),
        "settings worker suppressed persistence failure");
    require(QFile::remove(parent) && QDir().mkpath(parent), "cannot repair settings fixture");
    for (int i = 0; i < 50; ++i) controller.setThemeMode(i % 3);
    controller.setThemeMode(2);
    require(waitUntil([&] { return !controller.saving(); }) && controller.settingsError().isEmpty(),
        "latest settings revision did not recover after storage repair");
    require(DesktopController(parent + QStringLiteral("/preferences.ini")).themeMode() == 2,
        "coalesced settings worker lost final revision");
}
void actualControllerHistory() {
    ArchiveController archive([] { return std::make_unique<Reader>(); });
    TaskController center;
    QObject::connect(&archive, &ArchiveController::taskObserved,
        [&center, &archive](const auto& snapshot, const auto& path) {
            center.observe(QStringLiteral("archive"), snapshot, path,
                [&archive](auto id) { archive.cancelTask(id); });
        });
    QString recent;
    QObject::connect(&archive, &ArchiveController::archiveIndexed,
        [&recent](const QString& path) { recent = path; });
    archive.openFile(QDir::tempPath() + QStringLiteral("/recent-fixture.zip"));
    require(waitUntil([&] { return !archive.busy(); }), "archive did not complete");
    require(center.count() == 1 && center.activeCount() == 0 && !recent.isEmpty(), "indexing lifecycle/history lost");
    archive.openFile(QDir::tempPath() + QStringLiteral("/cancel-fixture.zip"));
    require(center.activeCount() == 1, "new task not in center");
    center.cancelTask(center.data(center.index(0), TaskController::IdRole).toString());
    require(waitUntil([&] { return !archive.busy(); }), "center cancellation hung");
    require(center.count() == 2 && center.activeCount() == 0, "cancel history missing");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        settings(); std::cout << "PASS persisted themes and bounded Unicode recent archives\n";
        persistenceFailureAndRetry(); std::cout << "PASS asynchronous settings failure, recovery and latest revision\n";
        tasks(); std::cout << "PASS bounded task center identity, progress, cancellation and errors\n";
        actualControllerHistory(); std::cout << "PASS real controller lifecycle and center cancellation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
