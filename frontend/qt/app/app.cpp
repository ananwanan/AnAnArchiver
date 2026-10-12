#include "app.h"
#include "controllers/drag_controller.h"
#include <QQmlApplicationEngine>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <utility>

namespace ananarchiver::qt {

App::App(QObject* parent) : QObject(parent), drag_(std::make_unique<DragController>(archives_, compression_))
{
    connect(&archives_, &ArchiveController::archiveIndexed, &desktop_, &DesktopController::rememberArchive);
    connect(&archives_, &ArchiveController::archiveOpening, this, [this](const QString& path) {
        if (shellExtracting_ || (!shellArchive_.isEmpty() && path != shellArchive_)) {
            shellArchive_.clear();
            shellDestination_.clear();
            shellIndexReady_ = false;
            shellExtracting_ = false;
            shellRetryDestination_.clear();
        }
    });
    const auto abandonShell = [this] {
        shellArchive_.clear();
        shellDestination_.clear();
        shellExtracting_ = false;
        shellRetryDestination_.clear();
        shellIndexReady_ = false;
    };
    connect(&archives_, &ArchiveController::archiveClosed, this, abandonShell);
    connect(&archives_, &ArchiveController::passwordDismissed, this, abandonShell);
    connect(&archives_, &ArchiveController::changed, this, &App::tryShellExtraction);
    connect(&archives_, &ArchiveController::archiveIndexed, this, [this](const QString& path) {
        if (shellArchive_ != path || shellDestination_.isEmpty()) return;
        shellIndexReady_ = true;
        tryShellExtraction();
    });
    connect(&archives_, &ArchiveController::operationFinished, this, [this](bool succeeded) {
        if (shellExtracting_) {
            shellExtracting_ = false;
            const auto destination = std::exchange(shellRetryDestination_, {});
            if (!succeeded && archives_.extractionNeedsPassword() && !destination.isEmpty()) {
                QMetaObject::invokeMethod(engine_->rootObjects().first(), "showShellExtractionPassword",
                                          Q_ARG(QVariant, destination));
            } else emit shellActionFinished(succeeded);
        } else if (!succeeded && !shellArchive_.isEmpty() && !archives_.passwordRequested()) {
            shellArchive_.clear();
            shellDestination_.clear();
            shellIndexReady_ = false;
            emit shellActionFinished(false);
        }
    });
    connect(&desktop_, &DesktopController::openRequested, &archives_, &ArchiveController::openFile);
    connect(drag_.get(), &DragController::openRequested, &archives_, &ArchiveController::openFile);
    connect(&compression_, &CompressionController::operationFinished, this, [this](bool succeeded) {
        if (succeeded) desktop_.rememberArchive(compression_.destination());
    });
    connect(&archives_, &ArchiveController::taskObserved, &taskCenter_,
        [this](const application::TaskSnapshot& snapshot, const QString& path) {
            taskCenter_.observe(QStringLiteral("archive"), snapshot, path,
                [this](application::TaskId id) { archives_.cancelTask(id); });
        });
    connect(&compression_, &CompressionController::taskObserved, &taskCenter_,
        [this](const application::TaskSnapshot& snapshot, const QString& path) {
            taskCenter_.observe(QStringLiteral("compress"), snapshot, path,
                [this](application::TaskId id) { compression_.cancelTask(id); });
        });
    connect(drag_.get(), &DragController::taskObserved, &taskCenter_,
        [this](const application::TaskSnapshot& snapshot, const QString& path) {
            taskCenter_.observe(QStringLiteral("drag"), snapshot, path,
                [this](application::TaskId id) { drag_->cancelTask(id); });
        });
}
App::~App() = default;

bool App::initialize(const QString& file, const QUrl& mainQml)
{
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    engine_->setInitialProperties({
        {QStringLiteral("buildInfo"), QVariant::fromValue(&buildInfo_)},
        {QStringLiteral("archiveController"), QVariant::fromValue(&archives_)},
        {QStringLiteral("compressionController"), QVariant::fromValue(&compression_)},
        {QStringLiteral("directoryModel"), QVariant::fromValue(&directories_)},
        {QStringLiteral("desktopController"), QVariant::fromValue(&desktop_)},
        {QStringLiteral("taskController"), QVariant::fromValue(&taskCenter_)},
        {QStringLiteral("dragController"), QVariant::fromValue(drag_.get())}});
    if (mainQml.isEmpty()) engine_->loadFromModule("AnAnArchiver", "Main");
    else engine_->load(mainQml);
    if (engine_->rootObjects().isEmpty())
        return false;
    if (!file.isEmpty())
        QTimer::singleShot(0, this, [this, file] { openArchive(file); });
    return true;
}

void App::openArchive(const QString& file)
{
    shellArchive_.clear();
    shellDestination_.clear();
    shellExtracting_ = false;
    shellRetryDestination_.clear();
    shellIndexReady_ = false;
    archives_.openFile(QFileInfo(file).absoluteFilePath());
}

void App::tryShellExtraction()
{
    if (!shellIndexReady_ || shellStartQueued_ || shellArchive_.isEmpty() ||
        shellDestination_.isEmpty() || !archives_.canExtract()) return;
    shellStartQueued_ = true;
    const auto path = shellArchive_;
    // Index/query completions can emit nested changed() signals. Defer until
    // that stack unwinds, and retry on changed() if an async query is pending.
    QTimer::singleShot(0, this, [this, path] {
        shellStartQueued_ = false;
        if (!shellIndexReady_ || shellArchive_ != path || archives_.currentFile() != path || !archives_.canExtract()) return;
        shellIndexReady_ = false;
        const auto destination = std::exchange(shellDestination_, {});
        shellArchive_.clear();
        shellRetryDestination_ = destination;
        shellExtracting_ = true;
        archives_.extract(destination, false, {}, 0, 0);
        if (!archives_.busy()) {
            shellExtracting_ = false;
            emit shellActionFinished(false);
        }
    });
}

bool App::handleShellAction(const QString& action, const QStringList& paths)
{
    if (paths.isEmpty() || !engine_ || engine_->rootObjects().isEmpty()) return false;
    if (action == QStringLiteral("compress")) {
        if (compression_.busy()) return false;
        for (const auto& path : paths) compression_.addInput(QFileInfo(path).absoluteFilePath());
        const QFileInfo first(QDir::cleanPath(QFileInfo(paths.first()).absoluteFilePath()));
        const auto name = paths.size() == 1 ? first.fileName() : QStringLiteral("Archive");
        const auto destination = first.absolutePath() + QChar(u'/') + name + QStringLiteral(".zip");
        if (!QMetaObject::invokeMethod(engine_->rootObjects().first(), "showShellCompression",
                                      Q_ARG(QVariant, destination))) return false;
        emit shellCompressionReady();
        return true;
    }
    if (paths.size() != 1 || (action != QStringLiteral("extract-here") &&
                            action != QStringLiteral("extract-to-folder"))) return false;
    const QFileInfo archive(paths.first());
    auto destination = archive.absolutePath();
    if (action == QStringLiteral("extract-to-folder")) {
        auto name = archive.fileName();
        // Match compound suffixes before their final codec suffix. Unknown
        // extensions still use content detection when opening the archive.
        const QStringList suffixes{QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"),
            QStringLiteral(".tar.xz"), QStringLiteral(".tar.zst"), QStringLiteral(".tar.lz4")};
        bool compound = false;
        for (const auto& suffix : suffixes) {
            if (name.endsWith(suffix, Qt::CaseInsensitive)) {
                name.chop(suffix.size()); compound = true; break;
            }
        }
        if (!compound) {
            const auto dot = name.lastIndexOf(QChar(u'.'));
            if (dot > 0) name.truncate(dot);
        }
        if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
            name = QStringLiteral("Archive");
        // Distinct names such as '.zip' and 'a.zip.zip' must not target the
        // source archive. Appending a folder suffix also handles hidden names.
        if (name == archive.fileName()) name += QStringLiteral(" extracted");
        destination += QChar(u'/') + name;
    }
    shellArchive_ = archive.absoluteFilePath();
    shellDestination_ = destination;
    shellExtracting_ = false;
    shellIndexReady_ = false;
    archives_.openFile(shellArchive_);
    return true;
}

}
