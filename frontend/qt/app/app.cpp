#include "app.h"
#include <QQmlApplicationEngine>
#include <QTimer>

namespace ananarchiver::qt {

App::App(QObject* parent) : QObject(parent) {}
App::~App() = default;

bool App::initialize(const QString& file)
{
    engine_ = std::make_unique<QQmlApplicationEngine>();
    engine_->setInitialProperties({
        {QStringLiteral("archiveController"), QVariant::fromValue(&archives_)},
        {QStringLiteral("compressionController"), QVariant::fromValue(&compression_)},
        {QStringLiteral("directoryModel"), QVariant::fromValue(&directories_)}});
    engine_->loadFromModule("AnAnArchiver", "Main");
    if (engine_->rootObjects().isEmpty())
        return false;
    if (!file.isEmpty())
        QTimer::singleShot(0, &archives_, [this, file] { archives_.openFile(file); });
    return true;
}

}
