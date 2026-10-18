#include "archive_folder_model.h"

#include <QStringList>
#include <utility>

namespace ananarchiver::qt {

QString normalizedArchivePath(QString path)
{
    path.replace(QChar(u'\\'), QChar(u'/'));
    auto parts = path.split(QChar(u'/'), Qt::SkipEmptyParts);
    parts.removeAll(QStringLiteral("."));
    return parts.join(QChar(u'/'));
}

QModelIndex ArchiveFolderModel::index(int row, int column, const QModelIndex& parentIndex) const
{
    if (!snapshot_ || row < 0 || column != 0 ||
        (parentIndex.isValid() && (parentIndex.model() != this || parentIndex.column() != 0)))
        return {};
    const auto parentId = parentIndex.isValid() ? parentIndex.internalId() : 0;
    if (parentId >= snapshot_->folders.size())
        return {};
    const auto& children = snapshot_->folders[parentId].children;
    if (static_cast<std::size_t>(row) >= children.size())
        return {};
    return createIndex(row, column, static_cast<quintptr>(children[static_cast<std::size_t>(row)]));
}

QModelIndex ArchiveFolderModel::parent(const QModelIndex& child) const
{
    if (!snapshot_ || !child.isValid() || child.model() != this ||
        child.internalId() >= snapshot_->folders.size())
        return {};
    const auto parentId = snapshot_->folders[child.internalId()].parent;
    if (parentId == 0)
        return {};
    return createIndex(snapshot_->folders[parentId].row, 0, static_cast<quintptr>(parentId));
}

int ArchiveFolderModel::rowCount(const QModelIndex& parentIndex) const
{
    if (!snapshot_ || (parentIndex.isValid() &&
        (parentIndex.model() != this || parentIndex.column() != 0)))
        return 0;
    const auto parentId = parentIndex.isValid() ? parentIndex.internalId() : 0;
    return parentId < snapshot_->folders.size()
        ? static_cast<int>(snapshot_->folders[parentId].children.size()) : 0;
}

QVariant ArchiveFolderModel::data(const QModelIndex& item, int role) const
{
    if (!snapshot_ || !item.isValid() || item.model() != this || item.column() != 0 ||
        item.internalId() == 0 || item.internalId() >= snapshot_->folders.size())
        return {};
    const auto& folder = snapshot_->folders[item.internalId()];
    if (role == Qt::DisplayRole || role == NameRole)
        return folder.name;
    if (role == PathRole)
        return folder.path;
    return {};
}

QHash<int, QByteArray> ArchiveFolderModel::roleNames() const
{
    return {{NameRole, "folderName"}, {PathRole, "folderPath"}};
}

bool ArchiveFolderModel::contains(const QString& path) const
{
    return path.isEmpty() || (snapshot_ && snapshot_->byPath.contains(path));
}

std::shared_ptr<const ArchiveFolderSnapshot> ArchiveFolderModel::replace(
    std::shared_ptr<const ArchiveFolderSnapshot> snapshot)
{
    if (snapshot_ == snapshot)
        return {};
    beginResetModel();
    auto previous = std::exchange(snapshot_, std::move(snapshot));
    endResetModel();
    return previous;
}

}
