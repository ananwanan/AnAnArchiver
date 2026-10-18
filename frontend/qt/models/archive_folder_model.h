#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QString>
#include <memory>
#include <vector>

namespace ananarchiver::qt {

// Constructed on the metadata worker and published as an immutable snapshot.
// Node zero is the archive root; only its actual folders appear in the view.
struct ArchiveFolderSnapshot {
    struct Folder {
        QString path;
        QString name;
        std::size_t parent{};
        int row{};
        std::vector<std::size_t> children;
    };
    std::vector<Folder> folders{Folder{}};
    QHash<QString, std::size_t> byPath{{QString(), 0}};
};

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
