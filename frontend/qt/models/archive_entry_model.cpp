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
    case SelectedRole:
        return selected_[static_cast<std::size_t>(index.row())] != 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> ArchiveEntryModel::roleNames() const
{
    return {{PathRole, "entryPath"}, {SizeRole, "entrySize"}, {ModifiedRole, "entryModified"},
            {TypeRole, "entryType"}, {DirectoryRole, "entryDirectory"}, {SelectedRole, "entrySelected"}};
}

void ArchiveEntryModel::clear()
{
    beginResetModel();
    std::vector<ArchiveEntry>{}.swap(entries_);
    std::vector<std::uint8_t>{}.swap(selected_);
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    totalBytes_ = 0;
    totalBytesKnown_ = true;
    endResetModel();
    emit countChanged();
    emit selectionChanged();
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
    if (selected_.capacity() < entries_.size() + entries.size())
        selected_.reserve(entries_.capacity());
    for (const auto& entry : entries) {
        if (entry.type != EntryType::RegularFile)
            continue;
        if (!entry.sizeKnown || totalBytes_ > std::numeric_limits<std::uint64_t>::max() - entry.size)
            totalBytesKnown_ = false;
        else
            totalBytes_ += entry.size;
    }
    beginInsertRows({}, first, last);
    selected_.insert(selected_.end(), entries.size(), 0);
    entries_.insert(entries_.end(), std::make_move_iterator(entries.begin()),
                    std::make_move_iterator(entries.end()));
    endInsertRows();
    emit countChanged();
}

void ArchiveEntryModel::clearSelection()
{
    if (selectionCount_ == 0)
        return;
    std::fill(selected_.begin(), selected_.end(), 0);
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    if (count() > 0)
        emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}

void ArchiveEntryModel::selectAll()
{
    if (count() == 0 || selectionCount_ == count())
        return;
    std::fill(selected_.begin(), selected_.end(), 1);
    selectionRanges_ = {{0, count() - 1}};
    selectionCount_ = count();
    anchor_ = 0;
    emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}

void ArchiveEntryModel::selectRow(int row, bool toggle, bool range)
{
    if (row < 0 || row >= count())
        return;
    if (range && anchor_ >= 0) {
        const auto first = std::min(row, anchor_);
        const auto last = std::max(row, anchor_);
        if (!toggle) {
            std::fill(selected_.begin(), selected_.end(), 0);
            selectionCount_ = 0;
            selectionRanges_.clear();
        }
        for (int i = first; i <= last; ++i) {
            if (!selected_[static_cast<std::size_t>(i)]) {
                selected_[static_cast<std::size_t>(i)] = 1;
                ++selectionCount_;
            }
        }
        selectionRanges_.emplace_back(first, last);
        std::sort(selectionRanges_.begin(), selectionRanges_.end());
        std::vector<std::pair<int, int>> merged;
        merged.reserve(selectionRanges_.size());
        for (const auto& interval : selectionRanges_) {
            if (!merged.empty() && interval.first <= merged.back().second + 1)
                merged.back().second = std::max(merged.back().second, interval.second);
            else
                merged.push_back(interval);
        }
        selectionRanges_ = std::move(merged);
    } else if (toggle) {
        auto& value = selected_[static_cast<std::size_t>(row)];
        value = value ? 0 : 1;
        selectionCount_ += value ? 1 : -1;
        if (value) {
            selectionRanges_.emplace_back(row, row);
            std::sort(selectionRanges_.begin(), selectionRanges_.end());
        } else {
            for (std::size_t i = 0; i < selectionRanges_.size(); ++i) {
                const auto [first, last] = selectionRanges_[i];
                if (row < first || row > last)
                    continue;
                selectionRanges_.erase(selectionRanges_.begin() + static_cast<std::ptrdiff_t>(i));
                if (row > first)
                    selectionRanges_.emplace_back(first, row - 1);
                if (row < last)
                    selectionRanges_.emplace_back(row + 1, last);
                std::sort(selectionRanges_.begin(), selectionRanges_.end());
                break;
            }
        }
        anchor_ = row;
    } else {
        std::fill(selected_.begin(), selected_.end(), 0);
        selected_[static_cast<std::size_t>(row)] = 1;
        selectionCount_ = 1;
        selectionRanges_ = {{row, row}};
        anchor_ = row;
    }
    emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}

std::vector<std::uint64_t> ArchiveEntryModel::selectedIndices() const
{
    std::vector<std::uint64_t> result;
    result.reserve(static_cast<std::size_t>(selectionCount_));
    // Sparse selection reads only its selected row ranges; selecting one entry
    // near the end of a million-row archive does not scan the metadata index.
    for (const auto& [first, last] : selectionRanges_)
        for (int row = first; row <= last; ++row)
            result.push_back(entries_[static_cast<std::size_t>(row)].index);
    return result;
}

void ArchiveEntryModel::setExtractionTotals(ExtractionRequest& request, bool selected) const
{
    // Directory selection includes descendants in the backend. Until that
    // expansion is known, display actual bytes with an indeterminate total.
    if (selected)
        return;
    request.totalEntries = static_cast<std::uint64_t>(entries_.size());
    if (totalBytesKnown_)
        request.totalBytes = totalBytes_;
}

}
