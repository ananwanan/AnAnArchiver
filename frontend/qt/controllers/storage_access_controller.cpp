#include "storage_access_controller.h"

#include <QGuiApplication>
#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QVariant>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace ananarchiver::qt {

StorageAccessController::StorageAccessController(QObject* parent) : QObject(parent)
{
    refreshTimer_.setInterval(300);
    connect(&refreshTimer_, &QTimer::timeout, this, &StorageAccessController::refresh);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) refresh();
        });
    }
#ifdef Q_OS_ANDROID
    granted_ = false;
#endif
}

bool StorageAccessController::available() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

bool StorageAccessController::requiresAllFilesAccess() const
{
#ifdef Q_OS_ANDROID
    return QNativeInterface::QAndroidApplication::sdkVersion() >= 30;
#else
    return false;
#endif
}

QUrl StorageAccessController::sharedStorageFolder() const
{
#ifdef Q_OS_ANDROID
    // Resolve Android's public volume through the platform API. QFileDialog's
    // local-path fallback must not start inside the app's private home folder.
    const auto directory = QJniObject::callStaticObjectMethod("android/os/Environment",
        "getExternalStorageDirectory", "()Ljava/io/File;");
    if (directory.isValid()) {
        const auto path = directory.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
        if (!path.isEmpty()) return QUrl::fromLocalFile(path);
    }
#endif
    return {};
}

void StorageAccessController::initialize()
{
    if (!available()) return;
    refresh();
    if (!granted_ && !QSettings().value(QStringLiteral("android/fileAccessPromptSeen"), false).toBool())
        showPrompt();
}

void StorageAccessController::refresh()
{
#ifdef Q_OS_ANDROID
    if (!QNativeInterface::QAndroidApplication::isActivityContext()) return;
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) return;
    const bool granted = activity.callMethod<jboolean>("hasStorageAccess", "()Z");
    const bool pending = activity.callMethod<jboolean>("isStorageRequestPending", "()Z");
    const bool changedState = granted_ != granted || requestInProgress_ != pending
        || (granted && (promptVisible_ || !error_.isEmpty()));
    granted_ = granted;
    requestInProgress_ = pending;
    if (!pending) refreshTimer_.stop();
    if (granted) {
        promptVisible_ = false;
        error_.clear();
    }
    if (changedState) emit changed();
#endif
}

void StorageAccessController::showPrompt()
{
    if (!available() || requestInProgress_) return;
    refresh();
    promptVisible_ = true;
    emit changed();
}

void StorageAccessController::rememberPrompt()
{
    QSettings settings;
    settings.setValue(QStringLiteral("android/fileAccessPromptSeen"), true);
}

void StorageAccessController::dismissPrompt()
{
    if (!available()) return;
    rememberPrompt();
    if (!promptVisible_) return;
    promptVisible_ = false;
    emit changed();
}

void StorageAccessController::requestAccess()
{
    if (!available() || requestInProgress_) return;
    rememberPrompt();
    promptVisible_ = false;
    error_.clear();
    requestInProgress_ = true;
    emit changed();
#ifdef Q_OS_ANDROID
    // Android's UI thread and Qt's GUI thread are different. Queue the request
    // and receive its result asynchronously, then watch the Activity callback.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        return activity.isValid() && activity.callMethod<jboolean>("requestStorageAccess", "()Z");
    }).then(this, [this](const QVariant& result) {
        if (!result.toBool()) {
            requestInProgress_ = false;
            error_ = tr("Could not request file access. Open the app's permissions in Android settings and try again.");
            emit changed();
            return;
        }
        refresh();
        if (requestInProgress_) refreshTimer_.start();
    });
#endif
}

}
