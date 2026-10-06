#pragma once

#include <ananarchiver/archive_entry.h>
#include <QAbstractListModel>
#include <vector>

namespace ananarchiver::qt {

class ArchiveEntryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { PathRole = Qt::UserRole + 1, SizeRole, ModifiedRole, TypeRole, DirectoryRole };

    explicit ArchiveEntryModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    void clear();
    void append(std::vector<ArchiveEntry> entries);

signals:
    void countChanged();

private:
    std::vector<ArchiveEntry> entries_;
};

}
