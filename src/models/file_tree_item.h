#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <memory>

class FileTreeItem
{
public:
    explicit FileTreeItem(const QString &name = QString(),
                          bool directory = false,
                          FileTreeItem *parent = nullptr);

    ~FileTreeItem();

    void appendChild(FileTreeItem *child);

    FileTreeItem *child(int row) const;

    int childCount() const;

    int row() const;

    FileTreeItem *parent() const;

public:
    QString name;

    QString fullPath;

    bool isDirectory = false;

    quint64 size = 0;

    quint64 compressedSize = 0;

    QDateTime modified;

private:
    std::vector<std::unique_ptr<FileTreeItem>> m_children;

    FileTreeItem *m_parent = nullptr;
};