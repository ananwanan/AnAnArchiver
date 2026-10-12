#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

namespace ananarchiver::qt {

class BuildInfo final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString buildTime READ buildTime CONSTANT)
    Q_PROPERTY(QUrl repositoryUrl READ repositoryUrl CONSTANT)

public:
    explicit BuildInfo(QObject* parent = nullptr) : QObject(parent) {}

    QString buildTime() const;
    QUrl repositoryUrl() const;
};

}
