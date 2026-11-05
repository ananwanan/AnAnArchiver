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
#include <stdexcept>
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
        bool folderNavigation{};
        QString folder;
        std::stop_token cancellation;
    };
    struct Output {
        std::uint64_t generation{};
        int sourceCount{};
        std::vector<std::size_t> rows;
        std::shared_ptr<const ArchiveFolderSnapshot> folders;
        std::shared_ptr<const application::MetadataIndex> metadata;
        QString error;
    };
    void deliver(ArchiveEntryModel* owner) {
        std::optional<Output> result;
        {
            std::lock_guard lock(mutex);
            result = std::move(output);
            output.reset();
        }
        if (result)
            owner->applyView(result->generation, result->sourceCount, std::move(result->rows),
                             std::move(result->folders), std::move(result->metadata),
                             std::move(result->error));
    }
    explicit QueryWorker(ArchiveEntryModel* owner)
        : thread([this, owner](std::stop_token stop) { run(owner, stop); }) {}
    ~QueryWorker() {
        thread.request_stop();
        cancel();
        ready.notify_all();
        thread.join();
    }
    void cancel() {
        {
            std::lock_guard lock(mutex);
            currentCancellation.request_stop();
            pending.reset();
            prune = true;
        }
        ready.notify_one();
    }
    void submit(Request request, bool cancelRunning) {
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
    void retire(std::shared_ptr<const void> snapshot) {
        if (!snapshot)
            return;
        {
            std::lock_guard lock(mutex);
            retired.push_back(std::move(snapshot));
        }
        ready.notify_one();
    }
    void run(ArchiveEntryModel* owner, std::stop_token stop) {
        std::shared_ptr<const application::MetadataIndex> cached;
        std::vector<application::MetadataBatch> cachedBatches;
        std::shared_ptr<const ArchiveFolderSnapshot> tree;
        while (!stop.stop_requested()) {
            std::optional<Request> request;
            std::vector<std::shared_ptr<const void>> garbage;
            std::optional<Output> abandoned;
            bool clear = false;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, stop,
                           [&] { return pending.has_value() || prune || !retired.empty(); });
                if (stop.stop_requested())
                    break;
                clear = std::exchange(prune, false);
                if (clear) {
                    abandoned = std::move(output);
                    output.reset();
                }
                request = std::move(pending);
                pending.reset();
                garbage.swap(retired);
            }
            if (clear) {
                cached.reset();
                cachedBatches.clear();
                tree.reset();
            }
            garbage.clear();
            if (!request)
                continue;
            std::stop_source combined;
            std::stop_callback onExit(stop, [&] { combined.request_stop(); });
            std::stop_callback onChange(request->cancellation, [&] { combined.request_stop(); });
            auto cancellation = combined.get_token();
            std::vector<application::MetadataBatch> batches;
            QString error;
            std::vector<std::size_t> rows;
            try {
                batches.reserve(request->batches.size());
                for (const auto& batch : request->batches)
                    batches.push_back(batch.entries);
                if (!cached || batches != cachedBatches) {
                    auto built = application::MetadataIndex::build(batches, {}, cancellation);
                    if (built) {
                        auto next = (*built)->hierarchySnapshot();
                        bool same = tree && tree->folders.size() == next->folders.size();
                        for (std::size_t id = 0; same && id < tree->folders.size(); ++id) {
                            const auto& a = tree->folders[id];
                            const auto& b = next->folders[id];
                            same = a.path == b.path && a.parent == b.parent &&
                                   a.children == b.children;
                            if ((id & 255U) == 0 && cancellation.stop_requested()) {
                                same = false;
                                break;
                            }
                        }
                        if (!same)
                            tree = std::move(next);
                        cached = std::move(*built);
                        cachedBatches = std::move(batches);
                    } else
                        error = fromUtf8(built.error().message);
                }
                if (error.isEmpty()) {
                    auto queried =
                        cached->query({request->folder.toUtf8().toStdString(),
                                       request->query.toUtf8().toStdString(), request->sortColumn,
                                       request->descending, request->folderNavigation, false},
                                      cancellation);
                    if (queried)
                        rows.assign(queried->begin(), queried->end());
                    else
                        error = fromUtf8(queried.error().message);
                }
            } catch (const std::exception& failure) {
                error = QString::fromUtf8(failure.what());
            } catch (...) {
                error = QStringLiteral("Metadata adapter failed");
            }
            if (cancellation.stop_requested())
                continue;
            std::shared_ptr<const ArchiveFolderSnapshot> folders;
            if (error.isEmpty() && request->folderNavigation)
                folders = tree;
            auto index = error.isEmpty() ? cached : nullptr;
            if (!error.isEmpty()) {
                cached.reset();
                cachedBatches.clear();
                tree.reset();
            }
            std::optional<Output> previous;
            {
                std::lock_guard lock(mutex);
                previous = std::exchange(output, Output{request->generation, request->totalCount,
                                                        std::move(rows), std::move(folders),
                                                        std::move(index), error});
            }
            // The Qt queue carries a wakeup only. Native results coalesce in one
            // owned mailbox and are reclaimed on this worker if never delivered.
            QMetaObject::invokeMethod(
                owner, [owner] { owner->worker_->deliver(owner); }, Qt::QueuedConnection);
        }
        // Final destruction also drains native allocations on the worker.
        std::optional<Output> finalOutput;
        std::vector<std::shared_ptr<const void>> finalRetired;
        {
            std::lock_guard lock(mutex);
            finalOutput = std::move(output);
            output.reset();
            finalRetired.swap(retired);
            pending.reset();
        }
    }
    std::mutex mutex;
    std::condition_variable_any ready;
    std::optional<Request> pending;
    std::optional<Output> output;
    bool prune{};
    std::stop_source currentCancellation;
    std::vector<std::shared_ptr<const void>> retired;
    std::jthread thread;
};

ArchiveEntryModel::ArchiveEntryModel(QObject* parent)
    : QAbstractListModel(parent), worker_(std::make_unique<QueryWorker>(this)) {}

ArchiveEntryModel::~ArchiveEntryModel() { ++*lifetimeGeneration_; }

int ArchiveEntryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

int ArchiveEntryModel::count() const
{
    return identityView_ ? totalCount_ : static_cast<int>(visibleRows_->size());
}

const ArchiveEntry& ArchiveEntryModel::entryAt(int row) const
{
    Q_ASSERT(!isFolderRow(row));
    const auto position = identityView_ ? static_cast<std::size_t>(row)
                                        : (*visibleRows_)[static_cast<std::size_t>(row)];
    const auto batch = std::upper_bound(batches_.begin(), batches_.end(), position,
        [](std::size_t value, const EntryBatch& candidate) { return value < candidate.first; });
    const auto& found = *std::prev(batch);
    return (*found.entries)[position - found.first];
}

bool ArchiveEntryModel::isFolderRow(int row) const
{
    return !identityView_ && ((*visibleRows_)[static_cast<std::size_t>(row)] & folderRowMask) != 0;
}

QString ArchiveEntryModel::directoryPathAt(int row) const
{
    if (isFolderRow(row))
        return fromUtf8(
            viewFolders_->folders[(*visibleRows_)[static_cast<std::size_t>(row)] & ~folderRowMask]
                .path);
    const auto& entry = entryAt(row);
    return entry.type == EntryType::Directory ? normalizedArchivePath(fromUtf8(entry.path)) : QString();
}

void ArchiveEntryModel::setFolderNavigationEnabled(bool enabled)
{
    if (folderNavigation_ == enabled)
        return;
    folderNavigation_ = enabled;
    beginResetModel();
    identityView_ = !enabled;
    worker_->retire(
        std::exchange(visibleRows_, std::make_shared<const std::vector<std::size_t>>()));
    worker_->retire(std::exchange(viewFolders_, {}));
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    endResetModel();
    emit countChanged();
    emit selectionChanged();
    requestView();
}

bool ArchiveEntryModel::navigateToFolder(const QString& path)
{
    const auto normalized = normalizedArchivePath(path);
    if (!folderNavigation_ || !folders_.contains(normalized))
        return false;
    if (normalized == currentFolder_)
        return true;
    currentFolder_ = normalized;
    clearSelection();
    beginResetModel();
    identityView_ = false;
    worker_->retire(
        std::exchange(visibleRows_, std::make_shared<const std::vector<std::size_t>>()));
    worker_->retire(std::exchange(viewFolders_, {}));
    endResetModel();
    emit countChanged();
    emit folderChanged();
    requestView();
    return true;
}

bool ArchiveEntryModel::navigateToRow(int row)
{
    if (querying_ || viewFailed_ || row < 0 || row >= count())
        return false;
    const auto path = directoryPathAt(row);
    return !path.isEmpty() && navigateToFolder(path);
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
    viewFailed_ = false;
    if (configurationChanged) {
        ++generation_;
        ++*lifetimeGeneration_;
        appliedSourceCount_ = 0;
    }
    clearSelection();
    if (totalCount_ == 0 || (!folderNavigation_ && query_.isEmpty() && sortColumn_ == -1)) {
        worker_->cancel();
        worker_->retire(std::exchange(viewIndex_, {}));
        if (!identityView_) {
            beginResetModel();
            identityView_ = !folderNavigation_;
            worker_->retire(
                std::exchange(visibleRows_, std::make_shared<const std::vector<std::size_t>>()));
            worker_->retire(std::exchange(viewFolders_, {}));
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
        worker_->submit({generation_, batches_, totalCount_, query_, sortColumn_, sortDescending_,
                         folderNavigation_, currentFolder_, {}},
                        configurationChanged);
    } catch (const std::exception& exception) {
        viewFailed_ = true;
        setQuerying(false);
        emit queryFailed(QString::fromUtf8(exception.what()));
    }
}

void ArchiveEntryModel::applyView(std::uint64_t generation, int sourceCount,
                                  std::vector<std::size_t> rows,
                                  std::shared_ptr<const ArchiveFolderSnapshot> folders,
                                  std::shared_ptr<const application::MetadataIndex> metadata,
                                  QString error) {
    if (generation != generation_ || sourceCount < appliedSourceCount_) {
        worker_->retire(std::move(folders));
        worker_->retire(std::move(metadata));
        worker_->retire(std::make_shared<const std::vector<std::size_t>>(std::move(rows)));
        return;
    }
    if (!error.isEmpty()) {
        worker_->retire(std::move(folders));
        viewFailed_ = true;
        beginResetModel();
        identityView_ = false;
        worker_->retire(
            std::exchange(visibleRows_, std::make_shared<const std::vector<std::size_t>>()));
        worker_->retire(std::exchange(viewIndex_, {}));
        worker_->retire(std::exchange(viewFolders_, {}));
        endResetModel();
        worker_->retire(folders_.replace({}));
        emit countChanged();
        setQuerying(sourceCount < totalCount_);
        emit queryFailed(error);
        return;
    }
    beginResetModel();
    viewFailed_ = false;
    appliedSourceCount_ = sourceCount;
    worker_->retire(std::exchange(
        visibleRows_, std::make_shared<const std::vector<std::size_t>>(std::move(rows))));
    worker_->retire(std::exchange(viewFolders_, folders));
    identityView_ = false;
    worker_->retire(std::exchange(viewIndex_, std::move(metadata)));
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    endResetModel();
    if (folderNavigation_)
        worker_->retire(folders_.replace(std::move(folders)));
    emit countChanged();
    emit selectionChanged();
    setQuerying(sourceCount < totalCount_);
}

QVariant ArchiveEntryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= count())
        return {};
    if (isFolderRow(index.row())) {
        const auto& folder =
            viewFolders_
                ->folders[(*visibleRows_)[static_cast<std::size_t>(index.row())] & ~folderRowMask];
        switch (role) {
        case Qt::DisplayRole:
        case PathRole:
            return fromUtf8(folder.path);
        case NameRole:
            return fromUtf8(folder.name);
        case SizeRole:
        case ModifiedRole: return QString();
        case TypeRole: return tr("Folder");
        case DirectoryRole: return true;
        case SelectedRole:
            return rowSelected(index.row());
        default: return {};
        }
    }
    const auto& entry = entryAt(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case PathRole:
        return fromUtf8(entry.path);
    case NameRole: {
        const auto path = normalizedArchivePath(fromUtf8(entry.path));
        return path.mid(path.lastIndexOf(QChar(u'/')) + 1);
    }
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
        return rowSelected(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> ArchiveEntryModel::roleNames() const
{
    return {{PathRole, "entryPath"}, {SizeRole, "entrySize"}, {ModifiedRole, "entryModified"},
            {TypeRole, "entryType"}, {DirectoryRole, "entryDirectory"}, {SelectedRole, "entrySelected"},
            {NameRole, "entryName"}};
}

void ArchiveEntryModel::clear()
{
    ++generation_;
    ++*lifetimeGeneration_;
    beginResetModel();
    worker_->retire(std::make_shared<const std::vector<EntryBatch>>(std::move(batches_)));
    batches_ = {};
    worker_->retire(std::exchange(viewIndex_, {}));
    worker_->retire(
        std::exchange(visibleRows_, std::make_shared<const std::vector<std::size_t>>()));
    worker_->retire(std::exchange(viewFolders_, {}));
    totalCount_ = 0;
    appliedSourceCount_ = 0;
    identityView_ = !folderNavigation_;
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    totalBytes_ = 0;
    totalBytesKnown_ = true;
    viewFailed_ = false;
    endResetModel();
    worker_->retire(folders_.replace({}));
    const bool wasInFolder = !currentFolder_.isEmpty();
    currentFolder_.clear();
    worker_->cancel();
    setQuerying(false);
    emit countChanged();
    emit totalCountChanged();
    emit selectionChanged();
    if (wasInFolder)
        emit folderChanged();
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
        endInsertRows();
        emit countChanged();
    }
    emit totalCountChanged();
    if (folderNavigation_ || !query_.isEmpty() || sortColumn_ != -1)
        requestView(false);
}

bool ArchiveEntryModel::rowSelected(int row) const {
    return std::ranges::any_of(
        selectionRanges_, [row](auto range) { return row >= range.first && row <= range.second; });
}
void ArchiveEntryModel::clearSelection() {
    ++selectionGeneration_;
    ++*lifetimeGeneration_;
    if (!selectionCount_)
        return;
    selectionRanges_.clear();
    selectionCount_ = 0;
    anchor_ = -1;
    if (count())
        emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}
void ArchiveEntryModel::selectAll() {
    if (querying_ || viewFailed_ || !count() || selectionCount_ == count())
        return;
    selectionRanges_ = {{0, count() - 1}};
    selectionCount_ = count();
    anchor_ = 0;
    ++selectionGeneration_;
    ++*lifetimeGeneration_;
    emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}
void ArchiveEntryModel::selectRow(int row, bool toggle, bool range) {
    if (querying_ || viewFailed_ || row < 0 || row >= count())
        return;
    const bool remove = toggle && !range && rowSelected(row);
    if ((!toggle || range) && !range)
        selectionRanges_.clear();
    if (range && !toggle)
        selectionRanges_.clear();
    if (remove) {
        std::vector<std::pair<int, int>> next;
        for (auto [first, last] : selectionRanges_) {
            if (row < first || row > last)
                next.emplace_back(first, last);
            else {
                if (row > first)
                    next.emplace_back(first, row - 1);
                if (row < last)
                    next.emplace_back(row + 1, last);
            }
        }
        selectionRanges_ = std::move(next);
    } else
        selectionRanges_.emplace_back(range && anchor_ >= 0 ? std::min(row, anchor_) : row,
                                      range && anchor_ >= 0 ? std::max(row, anchor_) : row);
    std::sort(selectionRanges_.begin(), selectionRanges_.end());
    std::vector<std::pair<int, int>> merged;
    for (auto part : selectionRanges_) {
        if (!merged.empty() && part.first <= merged.back().second + 1)
            merged.back().second = std::max(merged.back().second, part.second);
        else
            merged.push_back(part);
    }
    selectionRanges_ = std::move(merged);
    selectionCount_ = 0;
    for (auto [first, last] : selectionRanges_)
        selectionCount_ += last - first + 1;
    if (!range)
        anchor_ = row;
    ++selectionGeneration_;
    ++*lifetimeGeneration_;
    emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}
application::SelectionResolver ArchiveEntryModel::selectionResolver() const {
    std::vector<std::weak_ptr<const std::vector<ArchiveEntry>>> batches;
    batches.reserve(batches_.size());
    for (const auto& batch : batches_)
        batches.emplace_back(batch.entries);
    std::weak_ptr<const application::MetadataIndex> metadata = viewIndex_;
    std::weak_ptr<const std::vector<std::size_t>> rows = visibleRows_;
    auto ranges = selectionRanges_;
    const auto expected = lifetimeGeneration_->load();
    auto lifetime = lifetimeGeneration_;
    const bool identity = identityView_;
    const bool valid = !querying_ && !viewFailed_;
    return [batches = std::move(batches), metadata = std::move(metadata), rows,
            ranges = std::move(ranges), expected, lifetime, identity,
            valid](ExtractionRequest& target, CancellationToken stop) -> Result<void> {
        if (!valid || lifetime->load() != expected)
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::UnsupportedOperation, "Stale archive view", {}, {}});
        auto source = metadata.lock();
        if (!source) {
            std::vector<application::MetadataBatch> input;
            for (const auto& batch : batches) {
                auto locked = batch.lock();
                if (!locked)
                    return std::unexpected(ArchiveError{
                        ArchiveErrorCode::UnsupportedOperation, "Expired archive view", {}, {}});
                input.push_back(std::move(locked));
            }
            auto built = application::MetadataIndex::build(std::move(input), {}, stop);
            if (!built)
                return std::unexpected(built.error());
            source = std::move(*built);
        }
        std::vector<std::uint64_t> nativeRows;
        if (identity) {
            nativeRows.resize(source->count());
            for (std::size_t i = 0; i < nativeRows.size(); ++i)
                nativeRows[i] = i;
        } else {
            auto locked = rows.lock();
            if (!locked)
                return std::unexpected(ArchiveError{
                    ArchiveErrorCode::UnsupportedOperation, "Expired query rows", {}, {}});
            nativeRows.assign(locked->begin(), locked->end());
        }
        std::vector<application::MetadataRange> nativeRanges;
        for (auto [first, last] : ranges)
            nativeRanges.push_back(
                {static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(last - first + 1)});
        auto result = source->resolve(nativeRows, nativeRanges, stop);
        if (!result)
            return std::unexpected(result.error());
        if (lifetime->load() != expected)
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::UnsupportedOperation, "Stale archive view", {}, {}});
        target.selectedIndices = std::move(result->indices);
        target.selectedDirectoryPaths = std::move(result->directories);
        return {};
    };
}
std::vector<std::uint64_t> ArchiveEntryModel::selectedIndices() const {
    ExtractionRequest request;
    auto resolved = selectionResolver()(request, {});
    return resolved && request.selectedIndices ? std::move(*request.selectedIndices)
                                               : std::vector<std::uint64_t>{};
}
std::vector<std::string> ArchiveEntryModel::selectedDirectoryPaths() const {
    ExtractionRequest request;
    auto resolved = selectionResolver()(request, {});
    return resolved ? std::move(request.selectedDirectoryPaths) : std::vector<std::string>{};
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
