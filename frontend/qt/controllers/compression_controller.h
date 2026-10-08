#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <ananarchiver/application/archive_service.h>

namespace ananarchiver::qt {

class CompressionController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList formats READ formats CONSTANT)
    Q_PROPERTY(QStringList inputs READ inputs NOTIFY inputsChanged)
    Q_PROPERTY(int inputCount READ inputCount NOTIFY inputsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool hasTask READ hasTask NOTIFY changed)
    Q_PROPERTY(bool canOpenCreated READ canOpenCreated NOTIFY changed)
    Q_PROPERTY(QString destination READ destination NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString currentEntry READ currentEntry NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetails READ errorDetails NOTIFY changed)
    Q_PROPERTY(double processedBytes READ processedBytes NOTIFY changed)
    Q_PROPERTY(double totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(double processedEntries READ processedEntries NOTIFY changed)
    Q_PROPERTY(double bytesPerSecond READ bytesPerSecond NOTIFY changed)
    Q_PROPERTY(qint64 elapsedMilliseconds READ elapsedMilliseconds NOTIFY changed)
    Q_PROPERTY(qint64 remainingMilliseconds READ remainingMilliseconds NOTIFY changed)
    Q_PROPERTY(bool finalizing READ finalizing NOTIFY changed)

  public:
    explicit CompressionController(application::WriterFactory factory = {},
                                   QObject* parent = nullptr);
    ~CompressionController() override;
    QVariantList formats() const { return formats_; }
    QStringList inputs() const { return inputs_; }
    int inputCount() const { return static_cast<int>(inputs_.size()); }
    bool busy() const { return active_.has_value(); }
    bool hasTask() const { return hasTask_; }
    bool canOpenCreated() const { return succeeded_ && !busy(); }
    QString destination() const { return destination_; }
    QString status() const { return status_; }
    QString currentEntry() const { return currentEntry_; }
    QString errorMessage() const { return errorMessage_; }
    QString errorDetails() const { return errorDetails_; }
    double processedBytes() const { return processedBytes_; }
    double totalBytes() const { return totalBytes_; }
    double processedEntries() const { return processedEntries_; }
    double bytesPerSecond() const { return bytesPerSecond_; }
    qint64 elapsedMilliseconds() const { return elapsedMilliseconds_; }
    qint64 remainingMilliseconds() const { return remainingMilliseconds_; }
    bool finalizing() const { return finalizing_; }

    Q_INVOKABLE void addUrls(const QList<QUrl>& urls);
    Q_INVOKABLE void addInput(const QString& path);
    Q_INVOKABLE void removeInput(int index);
    Q_INVOKABLE bool create(const QString& destination, int format, int method, int level,
                            bool overwrite);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void dismissProgress();
    Q_INVOKABLE QString localPath(const QUrl& url) const { return url.toLocalFile(); }
    void cancelTask(application::TaskId id);

  signals:
    void inputsChanged();
    void changed();
    void operationFinished(bool succeeded);
    void taskObserved(const application::TaskSnapshot& snapshot, const QString& path);

  private:
    void poll();
    void showError(const ArchiveError& error);

    application::TaskManager tasks_;
    application::ArchiveService service_;
    QTimer timer_;
    std::vector<ArchiveCapabilities> capabilities_;
    QVariantList formats_;
    QStringList inputs_;
    std::optional<application::TaskId> active_;
    QString destination_;
    QString status_;
    QString currentEntry_;
    QString errorMessage_;
    QString errorDetails_;
    bool hasTask_{};
    bool succeeded_{};
    bool finalizing_{};
    double processedBytes_{};
    double totalBytes_{-1};
    double processedEntries_{};
    double bytesPerSecond_{};
    qint64 elapsedMilliseconds_{};
    qint64 remainingMilliseconds_{-1};
};

} // namespace ananarchiver::qt
