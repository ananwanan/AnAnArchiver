#include "build_info.h"
#include "ananarchiver_build_timestamp.h"
#include "ananarchiver_dependency_info.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace ananarchiver::qt {

namespace {
const QJsonObject& metadata()
{
    static const auto value = QJsonDocument::fromJson(ANANARCHIVER_DEPENDENCY_INFO_JSON).object();
    return value;
}
}

QString BuildInfo::buildTime() const
{
    const auto timestamp = QStringLiteral(ANANARCHIVER_BUILD_TIMESTAMP_UTC);
    const auto builtAt = QDateTime::fromString(timestamp, Qt::ISODate);
    if (!builtAt.isValid())
        return timestamp;

    const auto local = builtAt.toLocalTime();
    const auto offsetMinutes = local.offsetFromUtc() / 60;
    const auto absoluteOffset = qAbs(offsetMinutes);
    const QLocale locale;
    return locale.toString(local.date(), QLocale::ShortFormat) + QChar(u' ')
        + locale.toString(local.time(), QStringLiteral("HH:mm:ss"))
        + QStringLiteral(" (UTC%1%2:%3)")
              .arg(offsetMinutes < 0 ? QChar(u'-') : QChar(u'+'))
              .arg(absoluteOffset / 60, 2, 10, QChar(u'0'))
              .arg(absoluteOffset % 60, 2, 10, QChar(u'0'));
}

QUrl BuildInfo::repositoryUrl() const
{
    return QUrl(QStringLiteral("https://github.com/ananwanan/AnAnArchiver"));
}

QVariantMap BuildInfo::dependencyInfo() const
{
    return metadata().value(QStringLiteral("components")).toObject().toVariantMap();
}

QString BuildInfo::applicationRevision() const
{
    return metadata().value(QStringLiteral("applicationRevision")).toString();
}

bool BuildInfo::applicationDirty() const
{
    return metadata().value(QStringLiteral("applicationDirty")).toBool();
}

QString BuildInfo::qtRuntimeVersion() const
{
    return QString::fromLatin1(qVersion());
}

}
