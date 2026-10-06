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
    int entryCount() const { return entries_.count(); }
    qint64 elapsedMilliseconds() const { return elapsedMilliseconds_; }
    Q_INVOKABLE void openFile(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void close();
    Q_INVOKABLE void dismissError();

signals:
    void changed();
    void operationFinished(bool succeeded);

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
};

}
