#pragma once

#include "models/archive_entry_model.h"
#include <ananarchiver/application/archive_service.h>
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace ananarchiver::qt {

class ArchiveController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* entryModel READ entryModel CONSTANT)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY changed)
    Q_PROPERTY(QString formatName READ formatName NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString currentEntry READ currentEntry NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetails READ errorDetails NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY changed)
    Q_PROPERTY(qint64 elapsedMilliseconds READ elapsedMilliseconds NOTIFY changed)
    Q_PROPERTY(bool canExtract READ canExtract NOTIFY changed)
    Q_PROPERTY(bool extracting READ extracting NOTIFY changed)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY changed)
    Q_PROPERTY(double processedBytes READ processedBytes NOTIFY changed)
    Q_PROPERTY(double totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(double bytesPerSecond READ bytesPerSecond NOTIFY changed)
    Q_PROPERTY(qint64 remainingMilliseconds READ remainingMilliseconds NOTIFY changed)
    Q_PROPERTY(bool awaitingConflict READ awaitingConflict NOTIFY changed)
    Q_PROPERTY(QString conflictPath READ conflictPath NOTIFY changed)
    Q_PROPERTY(bool passwordRequested READ passwordRequested NOTIFY changed)

public:
    explicit ArchiveController(application::ReaderFactory factory = {}, QObject* parent = nullptr);
    ~ArchiveController() override;
    QAbstractItemModel* entryModel() { return &entries_; }
    QString currentFile() const { return currentFile_; }
    QString formatName() const { return formatName_; }
    QString status() const { return status_; }
    QString currentEntry() const { return currentEntry_; }
    QString errorMessage() const { return errorMessage_; }
    QString errorDetails() const { return errorDetails_; }
    bool busy() const { return active_.has_value() || !pendingFile_.isEmpty(); }
    int entryCount() const { return entries_.totalCount(); }
    qint64 elapsedMilliseconds() const { return elapsedMilliseconds_; }
    bool canExtract() const { return indexed_ && !busy() && !entries_.querying(); }
    bool extracting() const { return extracting_; }
    int selectionCount() const { return entries_.selectionCount(); }
    double processedBytes() const { return processedBytes_; }
    double totalBytes() const { return totalBytes_; }
    double bytesPerSecond() const { return bytesPerSecond_; }
    qint64 remainingMilliseconds() const { return remainingMilliseconds_; }
    bool awaitingConflict() const { return conflictId_ != 0; }
    QString conflictPath() const { return conflictPath_; }
    bool passwordRequested() const { return passwordRequested_; }
    Q_INVOKABLE void openFile(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void close();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void selectRow(int row, bool toggle = false, bool range = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void extract(const QString& destination, bool onlySelected, QString password,
                             int overwrite = 0, int links = 0);
    Q_INVOKABLE void resolveConflict(int overwrite, bool applyToAll);
    Q_INVOKABLE void retryOpenPassword(QString password);
    Q_INVOKABLE void dismissPassword();
    Q_INVOKABLE QString localPath(const QUrl& url) const { return url.toLocalFile(); }
    Result<ExtractionRequest> selectedExtractionRequest(const std::filesystem::path& destination) const;
    void cancelTask(application::TaskId id);

signals:
    void changed();
    void operationFinished(bool succeeded);
    void archiveIndexed(const QString& path);
    void taskObserved(const application::TaskSnapshot& snapshot, const QString& path);

private:
    void poll();
    void pollTasks();
    void requestOpen(const QString& path);
    void allocationFailed();
    void retireActive();
    void startPending();
    void showError(const ArchiveError& error);

    application::TaskManager tasks_;
    application::ArchiveService service_;
    ArchiveEntryModel entries_;
    QTimer timer_;
    std::optional<application::TaskId> active_;
    std::optional<application::TaskId> retired_;
    QString currentFile_;
    QString pendingFile_;
    QString formatName_;
    QString status_;
    QString currentEntry_;
    QString errorMessage_;
    QString errorDetails_;
    qint64 elapsedMilliseconds_{};
    bool indexed_{};
    bool extracting_{};
    bool passwordRequested_{};
    double processedBytes_{};
    double totalBytes_{-1};
    double bytesPerSecond_{};
    qint64 remainingMilliseconds_{-1};
    std::uint64_t conflictId_{};
    QString conflictPath_;
};

}
