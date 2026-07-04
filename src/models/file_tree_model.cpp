#include "file_tree_model.h"
#include "file_tree_item.h"

#include <QDir>
#include <QFileInfoList>

FileTreeModel::FileTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_root = new FileTreeItem("/", true);
}

FileTreeModel::~FileTreeModel()
{
    delete m_root;
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    FileTreeItem *parentItem;

    if (!parent.isValid()) {
        parentItem = m_root;
    } else {
        parentItem = static_cast<FileTreeItem *>(parent.internalPointer());
    }

    FileTreeItem *childItem = parentItem->child(row);

    if (childItem) {
        /**
         * 将childItem存进QModelIndex
         * 可通过
         * FileTreeItem *item = static_cast<FileTreeItem*>(index.internalPointer());
         * 取出
         *
         **/
        return createIndex(row, column, childItem);
    }

    return QModelIndex();
}

QModelIndex FileTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }

    auto childItem = static_cast<FileTreeItem *>(child.internalPointer());

    auto parentItem = childItem->parent();

    // 已经到第一层目录
    if (parentItem == m_root) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem);
}

int FileTreeModel::rowCount(const QModelIndex &parent) const
{
    FileTreeItem *parentItem;

    if (!parent.isValid()) {
        parentItem = m_root;
    } else {
        parentItem = static_cast<FileTreeItem *>(parent.internalPointer());
    }

    return parentItem->childCount();
}

int FileTreeModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

QVariant FileTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    auto item = static_cast<FileTreeItem *>(index.internalPointer());

    if (role == Qt::DisplayRole) {
        return item->name;
    }
    /**
     * if(role == NameRole)
     * 
     * if(role == SizeRole)
     * 
     * if(role == IconRole)
     * 
     * if(role == TypeRole)
     **/

    return QVariant();
}

bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    return rowCount(parent) > 0;
}

QHash<int, QByteArray> FileTreeModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}};
}

Qt::ItemFlags FileTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    // Qt::ItemIsEditable
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void FileTreeModel::loadDirectory(const QString &path)
{
    beginResetModel();

    clear();

    m_root = new FileTreeItem("/", true);

    scanDirectory(path, m_root);

    endResetModel();
}

void FileTreeModel::clear()
{
    delete m_root;
    m_root = nullptr;
}

void FileTreeModel::scanDirectory(const QString &path, FileTreeItem *parent)
{
    QDir dir(path);

    const QFileInfoList infos = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &info : infos) {
        auto item = new FileTreeItem(info.fileName(), info.isDir(), parent);

        item->fullPath = info.absoluteFilePath();
        item->size = info.size();
        item->modified = info.lastModified();

        parent->appendChild(item);

        if (info.isDir()) {
            scanDirectory(info.absoluteFilePath(), item);
        }
    }
}