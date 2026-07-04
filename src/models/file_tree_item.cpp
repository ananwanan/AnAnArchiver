#include "file_tree_item.h"

FileTreeItem::FileTreeItem(const QString &name, bool directory, FileTreeItem *parent)
    : name(name)
    , isDirectory(directory)
    , m_parent(parent)
{}

FileTreeItem::~FileTreeItem() {}

void FileTreeItem::appendChild(FileTreeItem *child)
{
    child->m_parent = this;
    m_children.emplace_back(child);
}

FileTreeItem *FileTreeItem::child(int row) const
{
    if (row < 0 || row >= m_children.size()) {
        return nullptr;
    }

    return m_children.at(row).get();
}

int FileTreeItem::childCount() const
{
    return m_children.size();
}

FileTreeItem *FileTreeItem::parent() const
{
    return m_parent;
}

int FileTreeItem::row() const
{
    if (!m_parent)
        return 0;

    auto it = std::find_if(m_parent->m_children.begin(),
                           m_parent->m_children.end(),
                           [this](const auto &item) { return item.get() == this; });

    return static_cast<int>(std::distance(m_parent->m_children.begin(), it));
}