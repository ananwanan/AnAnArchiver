#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QString>
#include <ananarchiver/application/metadata_index.h>
#include <memory>
#include <vector>

namespace ananarchiver::qt {

using ArchiveFolderSnapshot = application::MetadataFolders;

class ArchiveFolderModel final : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole };
    explicit ArchiveFolderModel(QObject* parent = nullptr) : QAbstractItemModel(parent) {}
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return 1; }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool contains(const QString& path) const;
    std::shared_ptr<const ArchiveFolderSnapshot> replace(
        std::shared_ptr<const ArchiveFolderSnapshot> snapshot);

private:
    std::shared_ptr<const ArchiveFolderSnapshot> snapshot_;
};

// This is archive metadata normalization, never filesystem canonicalization.
// Original entry paths remain untouched for extraction and security validation.
QString normalizedArchivePath(QString path);

}
