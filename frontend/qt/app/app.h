#pragma once

#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "controllers/desktop_controller.h"
#include "controllers/task_controller.h"
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
    bool initialize(const QString& file);
    ArchiveController& archiveController() { return archives_; }

private:
    ArchiveController archives_;
    CompressionController compression_;
    FileTreeModel directories_;
    DesktopController desktop_;
    TaskController taskCenter_;
    std::unique_ptr<DragController> drag_;
    // Destroy QML before its controller/model dependencies.
    std::unique_ptr<QQmlApplicationEngine> engine_;
};

}
