#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

namespace ananarchiver::qt {

// Android storage consent belongs to the frontend; archive operations continue
// to use the platform-neutral filesystem APIs and report ordinary I/O errors.
class StorageAccessController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool requiresAllFilesAccess READ requiresAllFilesAccess CONSTANT)
    Q_PROPERTY(QUrl sharedStorageFolder READ sharedStorageFolder CONSTANT)
    Q_PROPERTY(bool granted READ granted NOTIFY changed)
    Q_PROPERTY(bool requestInProgress READ requestInProgress NOTIFY changed)
    Q_PROPERTY(bool promptVisible READ promptVisible NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit StorageAccessController(QObject* parent = nullptr);
    bool available() const;
    bool requiresAllFilesAccess() const;
    QUrl sharedStorageFolder() const;
    bool granted() const { return granted_; }
    bool requestInProgress() const { return requestInProgress_; }
    bool promptVisible() const { return promptVisible_; }
    QString error() const { return error_; }

    void initialize();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void showPrompt();
    Q_INVOKABLE void dismissPrompt();
    Q_INVOKABLE void requestAccess();

signals:
    void changed();

private:
    void rememberPrompt();
    bool granted_{true};
    bool requestInProgress_{};
    bool promptVisible_{};
    QString error_;
    QTimer refreshTimer_;
};

}
