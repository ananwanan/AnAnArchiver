#include "archive_entry_model.h"
#include "platform/path_conversion.h"

#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>

namespace ananarchiver::qt {

ArchiveEntryModel::ArchiveEntryModel(QObject* parent) : QAbstractListModel(parent) {}

int ArchiveEntryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

int ArchiveEntryModel::count() const
{
    return static_cast<int>(entries_.size());
}

QVariant ArchiveEntryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= count())
        return {};
    const auto& entry = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case PathRole:
        return fromUtf8(entry.path);
    case SizeRole:
        if (entry.type == EntryType::Directory)
            return QString();
        if (!entry.sizeKnown)
            return tr("Unknown");
        return QLocale().formattedDataSize(static_cast<qint64>(std::min<std::uint64_t>(
            entry.size, static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()))));
    case ModifiedRole:
        if (entry.modified) {
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.modified->time_since_epoch()).count();
            return QLocale().toString(QDateTime::fromMSecsSinceEpoch(millis), QLocale::ShortFormat);
        }
        return QString();
    case TypeRole:
        switch (entry.type) {
        case EntryType::RegularFile: return tr("File");
        case EntryType::Directory: return tr("Folder");
        case EntryType::Symlink: return tr("Symbolic link");
        case EntryType::Hardlink: return tr("Hard link");
        case EntryType::Other: return tr("Other");
        }
        return QString();
    case DirectoryRole:
        return entry.type == EntryType::Directory;
    default:
        return {};
    }
}

QHash<int, QByteArray> ArchiveEntryModel::roleNames() const
{
    return {{PathRole, "entryPath"}, {SizeRole, "entrySize"}, {ModifiedRole, "entryModified"},
            {TypeRole, "entryType"}, {DirectoryRole, "entryDirectory"}};
}

void ArchiveEntryModel::clear()
{
    beginResetModel();
    std::vector<ArchiveEntry>{}.swap(entries_);
    endResetModel();
    emit countChanged();
}

void ArchiveEntryModel::append(std::vector<ArchiveEntry> entries)
{
    if (entries.empty())
        return;
    Q_ASSERT(entries_.size() + entries.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const auto first = count();
    const auto last = first + static_cast<int>(entries.size()) - 1;
    // Reserve before Qt announces insertion, preserving model consistency on allocation failure.
    if (entries_.capacity() < entries_.size() + entries.size())
        entries_.reserve(std::max(entries_.size() + entries.size(), entries_.capacity() * 2));
    beginInsertRows({}, first, last);
    entries_.insert(entries_.end(), std::make_move_iterator(entries.begin()),
                    std::make_move_iterator(entries.end()));
    endInsertRows();
    emit countChanged();
}

}
