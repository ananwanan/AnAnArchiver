#include "archive_entry_model.h"
#include "platform/path_conversion.h"

#include <QDateTime>
#include <QLocale>
#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace ananarchiver::qt {

struct ArchiveEntryModel::QueryWorker {
    struct Request {
        std::uint64_t generation{};
        std::vector<EntryBatch> batches;
        int totalCount{};
        QString query;
        int sortColumn{-1};
        bool descending{};
        std::stop_token cancellation;
    };
    struct Cancelled {};
    struct CachedNames {
        std::weak_ptr<const std::vector<ArchiveEntry>> entries;
        std::vector<QString> folded;
    };
    struct Row {
        std::size_t position{};
        const ArchiveEntry* entry{};
        const QString* name{};
    };

    explicit QueryWorker(ArchiveEntryModel* owner)
        : thread([this, owner](std::stop_token stop) { run(owner, stop); }) {}

    ~QueryWorker()
    {
        thread.request_stop();
        cancel();
        ready.notify_all();
        // Join before the model/QObject is destroyed; queued callbacks have
        // the model as their context and are removed by QObject destruction.
        thread.join();
    }

    void cancel()
    {
        {
            std::lock_guard lock(mutex);
            currentCancellation.request_stop();
            pending.reset();
            prune = true;
        }
        ready.notify_one();
    }

    void submit(Request request, bool cancelRunning)
    {
        {
            std::lock_guard lock(mutex);
            if (cancelRunning || currentCancellation.stop_requested()) {
                currentCancellation.request_stop();
                currentCancellation = std::stop_source{};
            }
            request.cancellation = currentCancellation.get_token();
            pending = std::move(request);
        }
        ready.notify_one();
    }

    static void check(const Request& request, std::stop_token stop)
    {
        if (stop.stop_requested() || request.cancellation.stop_requested())
            throw Cancelled{};
    }

    std::vector<std::size_t> calculate(const Request& request, std::stop_token stop)
    {
        // Keep Unicode keys across queries. They are computed only once per
        // immutable batch, on this worker, and released after an archive closes.
        std::erase_if(names, [](const auto& item) { return item.second.entries.expired(); });
        const bool needsNames = !request.query.isEmpty() || request.sortColumn == 0;
        const auto needle = request.query.toCaseFolded();
        std::vector<std::size_t> result;
        std::vector<Row> rows;
        const auto reserveCount = request.query.isEmpty() ? static_cast<std::size_t>(request.totalCount)
            : std::min<std::size_t>(static_cast<std::size_t>(request.totalCount), 4096);
        if (request.sortColumn < 0)
            result.reserve(reserveCount);
        else
            rows.reserve(reserveCount);
        for (const auto& batch : request.batches) {
            check(request, stop);
            const std::vector<QString>* folded = nullptr;
            if (needsNames) {
                auto found = names.find(batch.entries.get());
                // An expired batch may have the same allocation address as a
                // newly opened archive; never reuse its Unicode cache.
                if (found == names.end() || found->second.entries.expired()) {
                    CachedNames cached;
                    cached.entries = batch.entries;
                    cached.folded.reserve(batch.entries->size());
                    for (std::size_t i = 0; i < batch.entries->size(); ++i) {
                        if ((i & 255U) == 0)
                            check(request, stop);
                        cached.folded.push_back(fromUtf8((*batch.entries)[i].path).toCaseFolded());
                    }
                    found = names.insert_or_assign(batch.entries.get(), std::move(cached)).first;
                }
                folded = &found->second.folded;
            }
            for (std::size_t i = 0; i < batch.entries->size(); ++i) {
                if ((i & 255U) == 0)
                    check(request, stop);
                const QString* name = folded ? &(*folded)[i] : nullptr;
                if (needle.isEmpty() || name->contains(needle)) {
                    if (request.sortColumn < 0)
                        result.push_back(batch.first + i);
                    else
                        rows.push_back({batch.first + i, &(*batch.entries)[i], name});
                }
            }
        }
        if (request.sortColumn >= 0) {
            std::size_t comparisons = 0;
            std::sort(rows.begin(), rows.end(), [&](const Row& left, const Row& right) {
                if ((comparisons++ & 4095U) == 0)
                    check(request, stop);
                const auto& a = *left.entry;
                const auto& b = *right.entry;
                int order = 0;
                switch (request.sortColumn) {
                case 0:
                    order = QString::compare(*left.name, *right.name);
                    break;
                case 1: {
                    const bool aKnown = a.sizeKnown && a.type != EntryType::Directory;
                    const bool bKnown = b.sizeKnown && b.type != EntryType::Directory;
                    // Empty/unknown values stay last in either direction.
                    if (aKnown != bKnown)
                        return aKnown;
                    if (aKnown)
                        order = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
                    break;
                }
                case 2:
                    order = a.type < b.type ? -1 : (a.type > b.type ? 1 : 0);
                    break;
                case 3:
                    if (a.modified.has_value() != b.modified.has_value())
                        return a.modified.has_value();
                    if (a.modified)
                        order = *a.modified < *b.modified ? -1 : (*a.modified > *b.modified ? 1 : 0);
                    break;
                default:
                    break;
                }
                if (order == 0)
                    return left.position < right.position;
                return request.descending ? order > 0 : order < 0;
            });
        }
        check(request, stop);
        result.reserve(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if ((i & 4095U) == 0)
                check(request, stop);
            result.push_back(rows[i].position);
        }
        return result;
    }

    void run(ArchiveEntryModel* owner, std::stop_token stop)
    {
        while (!stop.stop_requested()) {
            std::optional<Request> request;
            bool pruneNames = false;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, stop, [&] { return pending.has_value() || prune; });
                if (stop.stop_requested())
                    return;
                if (prune) {
                    prune = false;
                    pruneNames = true;
                }
                if (pending) {
                    request = std::move(pending);
                    pending.reset();
                }
            }
            // Releasing cached strings can take time for a large archive.
            // Keep it outside the mailbox lock used by the GUI thread.
            if (pruneNames)
                std::erase_if(names, [](const auto& item) { return item.second.entries.expired(); });
            if (!request)
                continue;
            std::vector<std::size_t> result;
            QString error;
            try {
                result = calculate(*request, stop);
            } catch (const Cancelled&) {
                continue;
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
            }
            if (stop.stop_requested() || request->cancellation.stop_requested())
                continue;
            QMetaObject::invokeMethod(owner,
                [owner, generation = request->generation, sourceCount = request->totalCount,
                 result = std::move(result), error]() mutable {
                    owner->applyView(generation, sourceCount, std::move(result), error);
                }, Qt::QueuedConnection);
        }
    }

    std::mutex mutex;
    std::condition_variable_any ready;
    std::optional<Request> pending;
    bool prune{};
    std::stop_source currentCancellation;
    std::unordered_map<const std::vector<ArchiveEntry>*, CachedNames> names;
    // Last member: every object used by run() exists before the thread starts.
    std::jthread thread;
};

ArchiveEntryModel::ArchiveEntryModel(QObject* parent)
    : QAbstractListModel(parent), worker_(std::make_unique<QueryWorker>(this)) {}

ArchiveEntryModel::~ArchiveEntryModel() = default;

int ArchiveEntryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

int ArchiveEntryModel::count() const
{
    return identityView_ ? totalCount_ : static_cast<int>(visibleRows_.size());
}

const ArchiveEntry& ArchiveEntryModel::entryAt(int row) const
{
    const auto position = identityView_ ? static_cast<std::size_t>(row)
                                       : visibleRows_[static_cast<std::size_t>(row)];
    const auto batch = std::upper_bound(batches_.begin(), batches_.end(), position,
        [](std::size_t value, const EntryBatch& candidate) { return value < candidate.first; });
    const auto& found = *std::prev(batch);
    return (*found.entries)[position - found.first];
}

void ArchiveEntryModel::setQuery(const QString& query)
{
    if (query_ == query)
        return;
    query_ = query;
    clearSelection();
    emit queryChanged();
    requestView();
}

void ArchiveEntryModel::setSortColumn(int column)
{
    if (column < -1 || column > 3 || column == sortColumn_)
        return;
    sortColumn_ = column;
    clearSelection();
    emit sortChanged();
    requestView();
}

void ArchiveEntryModel::setSortDescending(bool descending)
{
    if (descending == sortDescending_)
        return;
    sortDescending_ = descending;
    clearSelection();
    emit sortChanged();
    requestView();
}

void ArchiveEntryModel::setQuerying(bool querying)
{
    if (querying_ == querying)
        return;
    querying_ = querying;
    emit queryingChanged();
}

void ArchiveEntryModel::requestView(bool configurationChanged)
{
    if (configurationChanged) {
        ++generation_;
        appliedSourceCount_ = 0;
    }
    clearSelection();
    if (totalCount_ == 0 || (query_.isEmpty() && sortColumn_ == -1)) {
        worker_->cancel();
        if (!identityView_) {
            beginResetModel();
            identityView_ = true;
            visibleRows_.clear();
            selected_.assign(static_cast<std::size_t>(totalCount_), 0);
            selectionRanges_.clear();
            selectionCount_ = 0;
            anchor_ = -1;
            endResetModel();
            emit countChanged();
            emit selectionChanged();
        }
        setQuerying(false);
        return;
    }
    setQuerying(true);
    try {
        // Appends coalesce the next snapshot without cancelling a same-query
        // sweep already in progress. Otherwise a 30 ms enumeration cadence
        // would starve every sort of a large index until enumeration ended.
        worker_->submit({generation_, batches_, totalCount_, query_, sortColumn_, sortDescending_, {}},
                        configurationChanged);
    } catch (const std::exception& exception) {
        setQuerying(false);
        emit queryFailed(QString::fromUtf8(exception.what()));
    }
}

void ArchiveEntryModel::applyView(std::uint64_t generation, int sourceCount,
                                 std::vector<std::size_t> rows, QString error)
{
    if (generation != generation_ || sourceCount < appliedSourceCount_)
        return;
    if (!error.isEmpty()) {
        setQuerying(sourceCount < totalCount_);
        emit queryFailed(error);
        return;
    }
    std::vector<std::uint8_t> selection;
    try {
        selection.resize(rows.size(), 0);
    } catch (const std::exception& exception) {
        setQuerying(sourceCount < totalCount_);
        emit queryFailed(QString::fromUtf8(exception.what()));
        return;
    }
    beginResetModel();
    appliedSourceCount_ = sourceCount;
    visibleRows_ = std::move(rows);
    identityView_ = false;
    selected_ = std::move(selection);
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    endResetModel();
    emit countChanged();
    emit selectionChanged();
    setQuerying(sourceCount < totalCount_);
}

QVariant ArchiveEntryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= count())
        return {};
    const auto& entry = entryAt(index.row());
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
    ++generation_;
    beginResetModel();
    std::vector<EntryBatch>{}.swap(batches_);
    std::vector<std::size_t>{}.swap(visibleRows_);
    totalCount_ = 0;
    appliedSourceCount_ = 0;
    identityView_ = true;
    std::vector<std::uint8_t>{}.swap(selected_);
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    totalBytes_ = 0;
    totalBytesKnown_ = true;
    endResetModel();
    worker_->cancel();
    setQuerying(false);
    emit countChanged();
    emit totalCountChanged();
    emit selectionChanged();
}

void ArchiveEntryModel::append(std::vector<ArchiveEntry> entries)
{
    if (entries.empty())
        return;
    Q_ASSERT(static_cast<std::size_t>(totalCount_) + entries.size()
             <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const auto first = totalCount_;
    const auto last = first + static_cast<int>(entries.size()) - 1;
    // Reserve before Qt announces insertion, preserving model consistency on allocation failure.
    auto batch = std::make_shared<const std::vector<ArchiveEntry>>(std::move(entries));
    if (batches_.size() == batches_.capacity())
        batches_.reserve(std::max<std::size_t>(batches_.size() + 1, batches_.capacity() * 2));
    if (identityView_ && selected_.capacity() < static_cast<std::size_t>(last + 1))
        selected_.reserve(std::max<std::size_t>(static_cast<std::size_t>(last + 1), selected_.capacity() * 2));
    for (const auto& entry : *batch) {
        if (entry.type != EntryType::RegularFile)
            continue;
        if (!entry.sizeKnown || totalBytes_ > std::numeric_limits<std::uint64_t>::max() - entry.size)
            totalBytesKnown_ = false;
        else
            totalBytes_ += entry.size;
    }
    if (identityView_)
        beginInsertRows({}, first, last);
    batches_.push_back({static_cast<std::size_t>(first), std::move(batch)});
    totalCount_ = last + 1;
    if (identityView_) {
        selected_.resize(static_cast<std::size_t>(totalCount_), 0);
        endInsertRows();
        emit countChanged();
    }
    emit totalCountChanged();
    if (!query_.isEmpty() || sortColumn_ != -1)
        requestView(false);
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
    if (querying_ || count() == 0 || selectionCount_ == count())
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
    if (querying_ || row < 0 || row >= count())
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
            result.push_back(entryAt(row).index);
    return result;
}

void ArchiveEntryModel::setExtractionTotals(ExtractionRequest& request, bool selected) const
{
    // Directory selection includes descendants in the backend. Until that
    // expansion is known, display actual bytes with an indeterminate total.
    if (selected)
        return;
    request.totalEntries = static_cast<std::uint64_t>(totalCount_);
    if (totalBytesKnown_)
        request.totalBytes = totalBytes_;
}

}
