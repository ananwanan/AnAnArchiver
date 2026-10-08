#include "desktop_controller.h"
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace ananarchiver::qt {

struct DesktopController::PersistenceWorker {
    struct Snapshot {
        std::uint64_t revision{};
        int theme{};
        QStringList recent;
    };

    PersistenceWorker(DesktopController* owner, QString filename, QSettings::Format format)
        : thread([this, owner, filename = std::move(filename), format] {
            run(owner, filename, format);
        }) {}

    ~PersistenceWorker() {
        {
            std::lock_guard lock(mutex);
            finishing = true;
        }
        ready.notify_one();
        // Shutdown flushes the last coalesced snapshot, with no dependency on
        // the GUI event loop. All normal save/sync IO happens on this worker.
        thread.join();
    }

    void submit(Snapshot snapshot) {
        {
            std::lock_guard lock(mutex);
            pending = std::move(snapshot);
        }
        ready.notify_one();
    }

    void run(DesktopController* owner, const QString& filename, QSettings::Format format) {
        for (;;) {
            Snapshot snapshot;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] { return finishing || pending.has_value(); });
                if (!pending)
                    return;
                snapshot = std::move(*pending);
                pending.reset();
            }
            bool success = false;
            try {
                // A distinct worker-owned instance prevents cross-thread
                // QSettings access and resets sticky error status on retry.
                QSettings settings(filename, format);
                settings.setValue(QStringLiteral("appearance/theme"), snapshot.theme);
                settings.setValue(QStringLiteral("archives/recent"), snapshot.recent);
                settings.sync();
                success = settings.status() == QSettings::NoError;
            } catch (...) {
                success = false;
            }
            QMetaObject::invokeMethod(owner, [owner, revision = snapshot.revision, success] {
                owner->finishSave(revision, success);
            }, Qt::QueuedConnection);
        }
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Snapshot> pending;
    bool finishing{};
    // Start only after all synchronization members have been constructed.
    std::thread thread;
};

DesktopController::DesktopController(QObject* parent) : QObject(parent) {
    load();
    startPersistence();
}
DesktopController::DesktopController(const QString& file, QObject* parent)
    : QObject(parent), settings_(file, QSettings::IniFormat) {
    load();
    startPersistence();
}
DesktopController::~DesktopController() {
    // Join while this QObject is alive. Its destruction then removes any
    // pending save callbacks addressed to this controller.
    persistence_.reset();
}

void DesktopController::startPersistence() {
    try {
        persistence_ = std::make_unique<PersistenceWorker>(this, settings_.fileName(), settings_.format());
    } catch (const std::exception&) {
        settingsError_ = tr("Preferences could not be saved. Check your settings folder permissions.");
    }
}

void DesktopController::load() {
    const auto mode = settings_.value(QStringLiteral("appearance/theme"), 0).toInt();
    themeMode_ = mode >= 0 && mode <= 2 ? mode : 0;
    for (const auto& path : settings_.value(QStringLiteral("archives/recent")).toStringList()) {
        // Lexical validation only: startup never stats files on slow/network drives.
        if (recent_.size() == 20)
            break;
        if (path.size() <= 32768 && !path.contains(QChar(u'\0')) && QDir::isAbsolutePath(path)) {
            const auto normalized = QDir::cleanPath(path);
            if (!recent_.contains(normalized))
                recent_.append(normalized);
        }
    }
}

void DesktopController::save() {
    ++revision_;
    if (!saving_) {
        saving_ = true;
        emit savingChanged();
    }
    // recent_ is capped at twenty paths. Repeated theme/history edits replace
    // the pending snapshot rather than creating unbounded events or IO work.
    if (!persistence_)
        startPersistence();
    if (!persistence_) {
        finishSave(revision_, false);
        return;
    }
    try {
        persistence_->submit({revision_, themeMode_, recent_});
    } catch (const std::exception&) {
        finishSave(revision_, false);
    }
}

void DesktopController::finishSave(std::uint64_t revision, bool success) {
    if (revision != revision_)
        return;
    const auto error = success ? QString()
        : tr("Preferences could not be saved. Check your settings folder permissions.");
    if (error != settingsError_) {
        settingsError_ = error;
        emit settingsErrorChanged();
    }
    saving_ = false;
    emit savingChanged();
}

void DesktopController::setThemeMode(int value) {
    if (value < 0 || value > 2 || value == themeMode_)
        return;
    themeMode_ = value;
    save();
    emit themeModeChanged();
}

QVariantList DesktopController::recentArchives() const {
    QVariantList result;
    for (const auto& path : recent_)
        result.append(QVariantMap{{QStringLiteral("path"), path},
            {QStringLiteral("name"), QFileInfo(path).fileName()}});
    return result;
}

void DesktopController::rememberArchive(const QString& path) {
    if (path.size() > 32768 || path.contains(QChar(u'\0')) || !QDir::isAbsolutePath(path))
        return;
    const auto normalized = QDir::cleanPath(path);
    recent_.removeAll(normalized);
    recent_.prepend(normalized);
    while (recent_.size() > 20)
        recent_.removeLast();
    save();
    emit recentArchivesChanged();
}

void DesktopController::clearRecent() {
    if (recent_.isEmpty())
        return;
    recent_.clear();
    save();
    emit recentArchivesChanged();
}

void DesktopController::removeRecent(const QString& path) {
    if (recent_.removeAll(path)) {
        save();
        emit recentArchivesChanged();
    }
}

void DesktopController::openRecent(const QString& path) {
    if (recent_.contains(path))
        emit openRequested(path);
}

} // namespace ananarchiver::qt
