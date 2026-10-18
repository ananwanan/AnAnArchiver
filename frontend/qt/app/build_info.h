#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace ananarchiver::qt {

class BuildInfo final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString buildTime READ buildTime CONSTANT)
    Q_PROPERTY(QUrl repositoryUrl READ repositoryUrl CONSTANT)
    Q_PROPERTY(QVariantMap dependencyInfo READ dependencyInfo CONSTANT)
    Q_PROPERTY(QString applicationRevision READ applicationRevision CONSTANT)
    Q_PROPERTY(bool applicationDirty READ applicationDirty CONSTANT)
    Q_PROPERTY(QString qtRuntimeVersion READ qtRuntimeVersion CONSTANT)

public:
    explicit BuildInfo(QObject* parent = nullptr) : QObject(parent) {}

    QString buildTime() const;
    QUrl repositoryUrl() const;
    QVariantMap dependencyInfo() const;
    QString applicationRevision() const;
    bool applicationDirty() const;
    QString qtRuntimeVersion() const;
};

}
