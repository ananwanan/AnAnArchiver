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
    // Match Core's pathname/depth limits, and bound the extra implicit-folder
    // index independently of the source entry count. QString contents are
    // shared between snapshots; the text budget counts stored path and name
    // buffers, while the node limit bounds vector/hash/object overhead.
    static constexpr std::size_t maxHierarchyPathBytes = 64U * 1024U;
    static constexpr qsizetype maxHierarchyDepth = 256;
    static constexpr std::size_t maxFolderNodes = 1'000'000;
    static constexpr std::size_t maxFolderTextBytes = 128U * 1024U * 1024U;
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
    struct Cancelled {};
    struct CachedNames {
        std::weak_ptr<const std::vector<ArchiveEntry>> entries;
        std::vector<QString> folded;
        std::vector<std::size_t> parents;
        std::vector<QString> foldedNames;
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

    void retire(std::shared_ptr<const ArchiveFolderSnapshot> snapshot)
    {
        if (!snapshot)
            return;
        {
            std::lock_guard lock(mutex);
            retiredFolders.push_back(std::move(snapshot));
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
        const bool needsNames = request.folderNavigation || !request.query.isEmpty() || request.sortColumn == 0;
        const auto needle = request.query.toCaseFolded();
        std::vector<std::size_t> result;
        std::vector<Row> rows;
        const auto reserveCount = request.query.isEmpty() && !request.folderNavigation
            ? static_cast<std::size_t>(request.totalCount)
            : std::min<std::size_t>(static_cast<std::size_t>(request.totalCount), 4096);
        const auto folderId = request.folderNavigation
            ? folderSnapshot->byPath.value(request.folder, std::numeric_limits<std::size_t>::max()) : 0;
        if (request.sortColumn < 0)
            result.reserve(reserveCount);
        else
            rows.reserve(reserveCount);
        if (request.folderNavigation && folderId < folderSnapshot->folders.size()) {
            const auto& children = folderSnapshot->folders[folderId].children;
            // Synthetic direct-folder rows share the hierarchy snapshot. They
            // have no backend index, and must stay ahead of files in any sort.
            const bool reverse = request.sortColumn == 0 && request.descending;
            for (std::size_t row = 0; row < children.size(); ++row) {
                if ((row & 255U) == 0)
                    check(request, stop);
                const auto id = children[reverse ? children.size() - row - 1 : row];
                if (needle.isEmpty() || folderSnapshot->folders[id].name.toCaseFolded().contains(needle))
                    result.push_back(folderRowMask | id);
            }
        }
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
                    found = names.insert_or_assign(batch.entries.get(), std::move(cached)).first;
                }
                auto& cached = found->second;
                if (request.folderNavigation && cached.parents.size() != batch.entries->size()) {
                    std::vector<std::size_t> parents;
                    std::vector<QString> foldedNames;
                    parents.reserve(batch.entries->size());
                    foldedNames.reserve(batch.entries->size());
                    for (std::size_t i = 0; i < batch.entries->size(); ++i) {
                        if ((i & 255U) == 0)
                            check(request, stop);
                        const auto path = normalizedArchivePath(fromUtf8((*batch.entries)[i].path));
                        const auto separator = path.lastIndexOf(QChar(u'/'));
                        parents.push_back(folderSnapshot->byPath.value(
                            separator < 0 ? QString() : path.left(separator)));
                        foldedNames.push_back(path.mid(separator + 1).toCaseFolded());
                    }
                    cached.parents = std::move(parents);
                    cached.foldedNames = std::move(foldedNames);
                } else if (!request.folderNavigation && cached.folded.size() != batch.entries->size()) {
                    std::vector<QString> foldedPaths;
                    foldedPaths.reserve(batch.entries->size());
                    for (std::size_t i = 0; i < batch.entries->size(); ++i) {
                        if ((i & 255U) == 0)
                            check(request, stop);
                        foldedPaths.push_back(fromUtf8((*batch.entries)[i].path).toCaseFolded());
                    }
                    cached.folded = std::move(foldedPaths);
                }
                folded = request.folderNavigation ? &found->second.foldedNames : &found->second.folded;
            }
            for (std::size_t i = 0; i < batch.entries->size(); ++i) {
                if ((i & 255U) == 0)
                    check(request, stop);
                if (request.folderNavigation &&
                    ((*batch.entries)[i].type == EntryType::Directory ||
                     names.at(batch.entries.get()).parents[i] != folderId))
                    continue;
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
        result.reserve(result.size() + rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if ((i & 4095U) == 0)
                check(request, stop);
            result.push_back(rows[i].position);
        }
        return result;
    }

    std::shared_ptr<const ArchiveFolderSnapshot> calculateFolders(
        const Request& request, std::stop_token stop)
    {
        if (!request.folderNavigation || request.batches.empty())
            return {};
        const auto first = folderSource.lock();
        if (!first || first != request.batches.front().entries) {
            folderSource = request.batches.front().entries;
            folderBuilder = ArchiveFolderSnapshot{};
            folderBatchCount = 0;
            folderSnapshotBatchCount = 0;
            folderTextBytes = 0;
            folderSnapshot.reset();
        }
        if (folderSnapshotBatchCount == request.batches.size())
            return folderSnapshot;
        // Keep the previously indexed hierarchy while appends coalesce. All
        // path parsing, deduplication and ordering stay on this worker.
        for (; folderBatchCount < request.batches.size(); ++folderBatchCount) {
            const auto& batch = request.batches[folderBatchCount];
            for (std::size_t i = 0; i < batch.entries->size(); ++i) {
                if ((i & 255U) == 0)
                    check(request, stop);
                const auto& entry = (*batch.entries)[i];
                if (entry.path.size() > maxHierarchyPathBytes)
                    throw std::length_error("Archive folder pathname exceeds the 64 KiB metadata limit.");
                auto path = normalizedArchivePath(fromUtf8(entry.path));
                // Count before producing full prefixes: a single deeply
                // nested entry must not amplify 64 KiB into gigabytes of
                // separately allocated implicit-folder paths.
                if (path.count(QChar(u'/')) >= maxHierarchyDepth)
                    throw std::length_error("Archive folder hierarchy exceeds the 256-level depth limit.");
                if (entry.type != EntryType::Directory)
                    path = path.left(std::max<qsizetype>(0, path.lastIndexOf(QChar(u'/'))));
                if (path.isEmpty())
                    continue;
                if (folderBuilder.byPath.contains(path))
                    continue;
                std::size_t parent = 0;
                qsizetype start = 0;
                while (start < path.size()) {
                    check(request, stop);
                    auto end = path.indexOf(QChar(u'/'), start);
                    if (end < 0)
                        end = path.size();
                    const auto prefix = path.left(end);
                    auto found = folderBuilder.byPath.constFind(prefix);
                    if (found == folderBuilder.byPath.cend()) {
                        if (folderBuilder.folders.size() >= maxFolderNodes)
                            throw std::length_error("Archive folder hierarchy exceeds the 1,000,000-node limit.");
                        // Each temporary lookup prefix is bounded by the
                        // individual pathname limit above. Check cumulative
                        // storage before allocating/inserting a new node.
                        const auto prefixUnits = static_cast<std::size_t>(end);
                        const auto nameUnits = static_cast<std::size_t>(end - start);
                        const auto remainingUnits = (maxFolderTextBytes - folderTextBytes) / sizeof(QChar);
                        if (prefixUnits > remainingUnits || nameUnits > remainingUnits - prefixUnits)
                            throw std::length_error("Archive folder hierarchy exceeds the 128 MiB text-memory limit.");
                        const auto id = folderBuilder.folders.size();
                        folderBuilder.folders.push_back({prefix, path.mid(start, end - start), parent, 0, {}});
                        folderBuilder.folders[parent].children.push_back(id);
                        folderBuilder.byPath.insert(prefix, id);
                        folderTextBytes += (prefixUnits + nameUnits) * sizeof(QChar);
                        parent = id;
                    } else {
                        parent = *found;
                    }
                    start = end + 1;
                }
            }
        }
        if (folderSnapshot && folderSnapshot->folders.size() == folderBuilder.folders.size()) {
            folderSnapshotBatchCount = request.batches.size();
            return folderSnapshot;
        }
        auto snapshot = std::make_shared<ArchiveFolderSnapshot>(folderBuilder);
        for (std::size_t id = 0; id < snapshot->folders.size(); ++id) {
            if ((id & 255U) == 0)
                check(request, stop);
            auto& children = snapshot->folders[id].children;
            std::size_t comparisons = 0;
            std::sort(children.begin(), children.end(), [&](std::size_t left, std::size_t right) {
                if ((comparisons++ & 4095U) == 0)
                    check(request, stop);
                const auto& a = snapshot->folders[left];
                const auto& b = snapshot->folders[right];
                const auto folded = QString::compare(a.name, b.name, Qt::CaseInsensitive);
                return folded != 0 ? folded < 0 : a.path < b.path;
            });
            for (std::size_t row = 0; row < children.size(); ++row)
                snapshot->folders[children[row]].row = static_cast<int>(row);
        }
        folderSnapshot = std::move(snapshot);
        folderSnapshotBatchCount = request.batches.size();
        return folderSnapshot;
    }

    void run(ArchiveEntryModel* owner, std::stop_token stop)
    {
        while (!stop.stop_requested()) {
            std::optional<Request> request;
            bool pruneNames = false;
            std::vector<std::shared_ptr<const ArchiveFolderSnapshot>> retired;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, stop, [&] { return pending.has_value() || prune || !retiredFolders.empty(); });
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
                retired.swap(retiredFolders);
            }
            // Releasing cached strings can take time for a large archive.
            // Keep it outside the mailbox lock used by the GUI thread.
            if (pruneNames) {
                std::erase_if(names, [](const auto& item) { return item.second.entries.expired(); });
                if (folderSource.expired()) {
                    folderBuilder = ArchiveFolderSnapshot{};
                    folderSnapshot.reset();
                    folderBatchCount = 0;
                    folderSnapshotBatchCount = 0;
                    folderTextBytes = 0;
                }
            }
            retired.clear();
            if (!request)
                continue;
            std::vector<std::size_t> result;
            QString error;
            std::shared_ptr<const ArchiveFolderSnapshot> folders;
            try {
                folders = calculateFolders(*request, stop);
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
                 result = std::move(result), folders = std::move(folders), error]() mutable {
                    owner->applyView(generation, sourceCount, std::move(result), std::move(folders), error);
                }, Qt::QueuedConnection);
        }
    }

    std::mutex mutex;
    std::condition_variable_any ready;
    std::optional<Request> pending;
    bool prune{};
    std::stop_source currentCancellation;
    std::unordered_map<const std::vector<ArchiveEntry>*, CachedNames> names;
    std::vector<std::shared_ptr<const ArchiveFolderSnapshot>> retiredFolders;
    std::weak_ptr<const std::vector<ArchiveEntry>> folderSource;
    ArchiveFolderSnapshot folderBuilder;
    std::shared_ptr<const ArchiveFolderSnapshot> folderSnapshot;
    std::size_t folderBatchCount{};
    std::size_t folderSnapshotBatchCount{};
    std::size_t folderTextBytes{};
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
    Q_ASSERT(!isFolderRow(row));
    const auto position = identityView_ ? static_cast<std::size_t>(row)
                                       : visibleRows_[static_cast<std::size_t>(row)];
    const auto batch = std::upper_bound(batches_.begin(), batches_.end(), position,
        [](std::size_t value, const EntryBatch& candidate) { return value < candidate.first; });
    const auto& found = *std::prev(batch);
    return (*found.entries)[position - found.first];
}

bool ArchiveEntryModel::isFolderRow(int row) const
{
    return !identityView_ && (visibleRows_[static_cast<std::size_t>(row)] & folderRowMask) != 0;
}

QString ArchiveEntryModel::directoryPathAt(int row) const
{
    if (isFolderRow(row))
        return viewFolders_->folders[visibleRows_[static_cast<std::size_t>(row)] & ~folderRowMask].path;
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
    visibleRows_.clear();
    worker_->retire(std::exchange(viewFolders_, {}));
    selected_.assign(enabled ? 0 : static_cast<std::size_t>(totalCount_), 0);
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
    visibleRows_.clear();
    worker_->retire(std::exchange(viewFolders_, {}));
    selected_.clear();
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
        appliedSourceCount_ = 0;
    }
    clearSelection();
    if (totalCount_ == 0 || (!folderNavigation_ && query_.isEmpty() && sortColumn_ == -1)) {
        worker_->cancel();
        if (!identityView_) {
            beginResetModel();
            identityView_ = !folderNavigation_;
            visibleRows_.clear();
            worker_->retire(std::exchange(viewFolders_, {}));
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
                                 std::shared_ptr<const ArchiveFolderSnapshot> folders, QString error)
{
    if (generation != generation_ || sourceCount < appliedSourceCount_) {
        worker_->retire(std::move(folders));
        return;
    }
    if (!error.isEmpty()) {
        worker_->retire(std::move(folders));
        viewFailed_ = true;
        setQuerying(sourceCount < totalCount_);
        emit queryFailed(error);
        return;
    }
    std::vector<std::uint8_t> selection;
    try {
        selection.resize(rows.size(), 0);
    } catch (const std::exception& exception) {
        worker_->retire(std::move(folders));
        viewFailed_ = true;
        setQuerying(sourceCount < totalCount_);
        emit queryFailed(QString::fromUtf8(exception.what()));
        return;
    }
    beginResetModel();
    viewFailed_ = false;
    appliedSourceCount_ = sourceCount;
    visibleRows_ = std::move(rows);
    worker_->retire(std::exchange(viewFolders_, folders));
    identityView_ = false;
    selected_ = std::move(selection);
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
        const auto& folder = viewFolders_->folders[
            visibleRows_[static_cast<std::size_t>(index.row())] & ~folderRowMask];
        switch (role) {
        case Qt::DisplayRole:
        case PathRole: return folder.path;
        case NameRole: return folder.name;
        case SizeRole:
        case ModifiedRole: return QString();
        case TypeRole: return tr("Folder");
        case DirectoryRole: return true;
        case SelectedRole: return selected_[static_cast<std::size_t>(index.row())] != 0;
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
        return selected_[static_cast<std::size_t>(index.row())] != 0;
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
    beginResetModel();
    std::vector<EntryBatch>{}.swap(batches_);
    std::vector<std::size_t>{}.swap(visibleRows_);
    worker_->retire(std::exchange(viewFolders_, {}));
    totalCount_ = 0;
    appliedSourceCount_ = 0;
    identityView_ = !folderNavigation_;
    std::vector<std::uint8_t>{}.swap(selected_);
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
    if (folderNavigation_ || !query_.isEmpty() || sortColumn_ != -1)
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
    if (querying_ || viewFailed_ || count() == 0 || selectionCount_ == count())
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
    if (querying_ || viewFailed_ || row < 0 || row >= count())
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
        for (int row = first; row <= last; ++row) {
            if (isFolderRow(row))
                continue;
            const auto& entry = entryAt(row);
            if (entry.type != EntryType::Directory)
                result.push_back(entry.index);
        }
    return result;
}

std::vector<std::string> ArchiveEntryModel::selectedDirectoryPaths() const
{
    std::vector<std::string> result;
    for (const auto& [first, last] : selectionRanges_)
        for (int row = first; row <= last; ++row) {
            const auto path = directoryPathAt(row);
            if (path.isEmpty())
                continue;
            const auto bytes = path.toUtf8();
            result.emplace_back(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        }
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
