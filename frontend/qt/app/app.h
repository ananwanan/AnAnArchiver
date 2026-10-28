#pragma once

#include "build_info.h"
#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "controllers/desktop_controller.h"
#include "controllers/task_controller.h"
#include "controllers/storage_access_controller.h"
#include "models/file_tree_model.h"
#include <QObject>
#include <memory>

class QQmlApplicationEngine;

namespace ananarchiver::qt {
class DragController;

class App final : public QObject
{
    Q_OBJECT

public:
    explicit App(QObject* parent = nullptr);
    ~App() override;
    bool initialize(const QString& file, const QUrl& mainQml = {});
    ArchiveController& archiveController() { return archives_; }
    bool handleShellAction(const QString& action, const QStringList& paths);
    void openArchive(const QString& file);

signals:
    void shellActionFinished(bool succeeded);
    void shellCompressionReady();

private:
    void tryShellExtraction();
    BuildInfo buildInfo_;
    ArchiveController archives_;
    CompressionController compression_;
    FileTreeModel directories_;
    DesktopController desktop_;
    TaskController taskCenter_;
    StorageAccessController storageAccess_;
    QString shellArchive_;
    QString shellDestination_;
    QString shellRetryDestination_;
    bool shellExtracting_{};
    bool shellIndexReady_{};
    bool shellStartQueued_{};
    std::unique_ptr<DragController> drag_;
    // Destroy QML before its controller/model dependencies.
    std::unique_ptr<QQmlApplicationEngine> engine_;
};

}
