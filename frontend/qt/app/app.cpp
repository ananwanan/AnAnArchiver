#include "app.h"
#include "controllers/drag_controller.h"
#include <QQmlApplicationEngine>
#include <QTimer>

namespace ananarchiver::qt {

App::App(QObject* parent) : QObject(parent), drag_(std::make_unique<DragController>(archives_, compression_))
{
    connect(&archives_, &ArchiveController::archiveIndexed, &desktop_, &DesktopController::rememberArchive);
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

bool App::initialize(const QString& file)
{
    engine_ = std::make_unique<QQmlApplicationEngine>();
    engine_->setInitialProperties({
        {QStringLiteral("buildInfo"), QVariant::fromValue(&buildInfo_)},
        {QStringLiteral("archiveController"), QVariant::fromValue(&archives_)},
        {QStringLiteral("compressionController"), QVariant::fromValue(&compression_)},
        {QStringLiteral("directoryModel"), QVariant::fromValue(&directories_)},
        {QStringLiteral("desktopController"), QVariant::fromValue(&desktop_)},
        {QStringLiteral("taskController"), QVariant::fromValue(&taskCenter_)},
        {QStringLiteral("dragController"), QVariant::fromValue(drag_.get())}});
    engine_->loadFromModule("AnAnArchiver", "Main");
    if (engine_->rootObjects().isEmpty())
        return false;
    if (!file.isEmpty())
        QTimer::singleShot(0, &archives_, [this, file] { archives_.openFile(file); });
    return true;
}

}
