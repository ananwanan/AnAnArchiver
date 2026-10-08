#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <cstdint>
#include <memory>

namespace ananarchiver::qt {

class DesktopController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QVariantList recentArchives READ recentArchives NOTIFY recentArchivesChanged)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
  public:
    explicit DesktopController(QObject* parent = nullptr);
    explicit DesktopController(const QString& settingsFile, QObject* parent = nullptr);
    ~DesktopController() override;
    int themeMode() const { return themeMode_; }
    void setThemeMode(int value);
    QVariantList recentArchives() const;
    QString settingsError() const { return settingsError_; }
    bool saving() const { return saving_; }
    void rememberArchive(const QString& path);
    Q_INVOKABLE void clearRecent();
    Q_INVOKABLE void removeRecent(const QString& path);
    Q_INVOKABLE void openRecent(const QString& path);
  signals:
    void themeModeChanged();
    void recentArchivesChanged();
    void settingsErrorChanged();
    void savingChanged();
    void openRequested(const QString& path);
  private:
    void load();
    void startPersistence();
    void save();
    void finishSave(std::uint64_t revision, bool success);
    struct PersistenceWorker;
    QSettings settings_;
    int themeMode_{};
    QStringList recent_;
    QString settingsError_;
    bool saving_{};
    std::uint64_t revision_{};
    std::unique_ptr<PersistenceWorker> persistence_;
};

} // namespace ananarchiver::qt
