#pragma once

#include <ananarchiver/application/archive_service.h>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <memory>

class QDrag;

namespace ananarchiver::qt {

class ArchiveController;
class CompressionController;
class TempFileManager;

class DragController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetails READ errorDetails NOTIFY changed)
    Q_PROPERTY(QList<QUrl> exportUrls READ exportUrls NOTIFY changed)

  public:
    DragController(ArchiveController& archive, CompressionController& compression,
                   QObject* parent = nullptr, application::ReaderFactory factory = {},
                   std::function<Qt::DropAction(QDrag&)> nativeDragExecutor = {});
    ~DragController() override;
    bool busy() const { return active_.has_value() || dragging_; }
    bool ready() const { return !exportUrls_.isEmpty() && !busy(); }
    QString status() const { return status_; }
    QString errorMessage() const { return errorMessage_; }
    QString errorDetails() const { return errorDetails_; }
    QList<QUrl> exportUrls() const { return exportUrls_; }

    Q_INVOKABLE void dropUrls(const QList<QUrl>& urls);
    Q_INVOKABLE void prepareExport(QString password = {});
    Q_INVOKABLE void startDrag();
    Q_INVOKABLE void cancel();
    void cancelTask(application::TaskId id);
    Q_INVOKABLE void dismissError();

  signals:
    void changed();
    void openRequested(const QString& path);
    void createRequested();
    void taskObserved(const application::TaskSnapshot& snapshot, const QString& path);

  private:
    enum class Phase { InspectDrop, ProbeDrop, CreateRoot, Extract, Collect };
    struct Operation;
    void poll();
    void addDroppedInputs();
    void discardExport();
    void showError(const ArchiveError& error);
    void startExtract();
    void startCollect();
    void finishExport(const application::TaskSnapshot& snapshot);
    void invalidateExport();
    bool begin(Result<application::TaskId> task, Phase phase, const QString& status);
    void observeTask(application::TaskSnapshot snapshot);

    ArchiveController& archive_;
    CompressionController& compression_;
    // Tasks are destroyed/joined before the temp manager cleans owned roots.
    std::shared_ptr<TempFileManager> temporary_;
    application::TaskManager tasks_;
    application::ArchiveService service_;
    std::function<Qt::DropAction(QDrag&)> nativeDragExecutor_;
    QTimer timer_;
    std::optional<application::TaskId> active_;
    std::shared_ptr<Operation> operation_;
    Phase phase_{Phase::InspectDrop};
    QList<QUrl> droppedUrls_;
    QList<QUrl> exportUrls_;
    std::filesystem::path exportRoot_;
    std::optional<std::vector<std::uint64_t>> exportedIndices_;
    std::vector<std::string> exportedDirectoryPaths_;
    QString exportedArchive_;
    QString status_;
    QString errorMessage_;
    QString errorDetails_;
    bool dragging_{};
    bool retained_{};
    std::uint64_t retentionGeneration_{};
};

} // namespace ananarchiver::qt
