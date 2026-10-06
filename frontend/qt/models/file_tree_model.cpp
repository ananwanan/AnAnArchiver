#include "file_tree_model.h"
#include "platform/path_conversion.h"

#include <algorithm>
#include <exception>
#include <new>

namespace ananarchiver::qt {

FileTreeModel::FileTreeModel(QObject* parent)
    : QAbstractItemModel(parent), root_(std::make_unique<Node>()),
      worker_([this](std::stop_token stop) { run(stop); })
{
    root_->entry.directory = true;
    root_->loaded = true;
    nodes_[0] = root_.get();
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &FileTreeModel::consume);
    timer_.start();
}

FileTreeModel::~FileTreeModel()
{
    worker_.request_stop();
    {
        std::lock_guard lock(mutex_);
        if (activeCancellation_)
            activeCancellation_->request_stop();
    }
    wake_.notify_all();
}

FileTreeModel::Node* FileTreeModel::node(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex FileTreeModel::nodeIndex(Node* item) const
{
    return item == root_.get() ? QModelIndex() : createIndex(item->row, 0, item);
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0 || (parent.isValid() && parent.column() != 0))
        return {};
    const auto* item = node(parent);
    if (static_cast<std::size_t>(row) >= item->children.size())
        return {};
    return createIndex(row, column, item->children[static_cast<std::size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    return nodeIndex(node(child)->parent);
}

int FileTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;
    return static_cast<int>(node(parent)->children.size());
}

int FileTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant FileTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto& entry = node(index)->entry;
    switch (role) {
    case Qt::DisplayRole: return fromUtf8(entry.name);
    case Qt::UserRole + 1: return fromPath(entry.path);
    case Qt::UserRole + 2: return entry.directory;
    default: return {};
    }
}

QHash<int, QByteArray> FileTreeModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "fullPath"},
            {Qt::UserRole + 2, "isDirectory"}};
}

bool FileTreeModel::hasChildren(const QModelIndex& parent) const
{
    const auto* item = node(parent);
    return item->entry.directory && (!item->loaded || !item->children.empty());
}

bool FileTreeModel::canFetchMore(const QModelIndex& parent) const
{
    const auto* item = node(parent);
    return item->entry.directory && !item->loaded && !item->loading;
}

void FileTreeModel::fetchMore(const QModelIndex& parent)
{
    try {
        if (!canFetchMore(parent))
            return;
        auto* item = node(parent);
        {
            std::lock_guard lock(mutex_);
            if (requests_.size() >= 64) {
                QTimer::singleShot(0, this, [this] {
                    emit scanFailed(tr("Too many folders are being loaded. Please try expanding this folder again shortly."));
                });
                return;
            }
            requests_.push_back({generation_, item->id, item->entry.path, {}});
            item->loading = true;
        }
        wake_.notify_one();
    } catch (const std::bad_alloc&) {
        scanFailedAndReset(tr("The operation exceeded a safety limit."));
    } catch (const std::exception& error) {
        scanFailedAndReset(fromUtf8(error.what()));
    }
}

QString FileTreeModel::rootPath() const { return fromPath(root_->entry.path); }

void FileTreeModel::reset(const QString& path)
{
    // Complete allocation and Unicode conversion before announcing a Qt reset.
    auto replacement = std::make_unique<Node>();
    replacement->entry.path = toPath(path);
    replacement->entry.directory = true;
    replacement->loaded = path.isEmpty();
    std::unordered_map<std::uint64_t, Node*> replacementNodes;
    replacementNodes.emplace(0, replacement.get());
    {
        std::lock_guard lock(mutex_);
        if (activeCancellation_)
            activeCancellation_->request_stop();
        requests_.clear();
        completed_.reset();
    }
    ++generation_;
    pending_.reset();
    beginResetModel();
    root_.swap(replacement);
    nodes_.swap(replacementNodes);
    endResetModel();
    emit rootPathChanged();
    wake_.notify_all();
}

void FileTreeModel::loadDirectory(const QString& path)
{
    try {
        reset(path);
        if (!path.isEmpty())
            fetchMore({});
    } catch (const std::bad_alloc&) {
        scanFailedAndReset(tr("The operation exceeded a safety limit."));
    } catch (const std::exception& error) {
        scanFailedAndReset(fromUtf8(error.what()));
    }
}

void FileTreeModel::loadDirectoryUrl(const QUrl& url)
{
    if (url.isLocalFile())
        loadDirectory(url.toLocalFile());
}

void FileTreeModel::fetchChildren(const QModelIndex& parent) { fetchMore(parent); }
void FileTreeModel::clear() { loadDirectory({}); }

void FileTreeModel::scanFailedAndReset(const QString& message)
{
    {
        std::lock_guard lock(mutex_);
        if (activeCancellation_)
            activeCancellation_->request_stop();
        requests_.clear();
        completed_.reset();
    }
    ++generation_;
    pending_.reset();
    beginResetModel();
    // Reuse the existing root so failure cleanup requires no new allocation.
    std::vector<std::unique_ptr<Node>>{}.swap(root_->children);
    std::unordered_map<std::uint64_t, Node*>{}.swap(nodes_);
    root_->entry = {};
    root_->entry.directory = true;
    root_->loaded = true;
    root_->loading = false;
    endResetModel();
    wake_.notify_all();
    emit rootPathChanged();
    emit scanFailed(message);
}

void FileTreeModel::run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        std::optional<Request> request;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [this] { return !requests_.empty(); }))
                return;
            request.emplace(std::move(requests_.front()));
            requests_.pop_front();
            activeCancellation_ = request->cancellation;
        }
        std::stop_callback cancel(stop, [&request] { request->cancellation.request_stop(); });
        Result<std::vector<application::DirectoryEntry>> result;
        try {
            result = application::DirectoryService::scanChildren(
                request->path, request->cancellation.get_token());
        } catch (const std::bad_alloc&) {
            // Keep this error small and avoid copying a potentially long path.
            result = std::unexpected(ArchiveError{
                ArchiveErrorCode::ResourceLimitExceeded, "Out of memory", {}, {}});
        } catch (const std::exception& error) {
            result = std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                error.what(), {}, request->path});
        } catch (...) {
            result = std::unexpected(ArchiveError{ArchiveErrorCode::Unknown,
                "Unexpected directory scan failure", {}, request->path});
        }
        {
            std::unique_lock lock(mutex_);
            // Single slots bound queued scan results and do not allocate on publication.
            wake_.wait(lock, stop, [this, &request] {
                return !completed_ || request->cancellation.stop_requested();
            });
            activeCancellation_.reset();
            if (!stop.stop_requested() && !request->cancellation.stop_requested())
                completed_.emplace(Completed{request->generation, request->parentId, std::move(result), 0});
        }
    }
}

void FileTreeModel::consume()
{
    try {
        consumePending();
    } catch (const std::bad_alloc&) {
        scanFailedAndReset(tr("The operation exceeded a safety limit."));
    } catch (const std::exception& error) {
        scanFailedAndReset(fromUtf8(error.what()));
    }
}

void FileTreeModel::consumePending()
{
    {
        std::lock_guard lock(mutex_);
        if (!pending_ && completed_) {
            pending_.emplace(std::move(*completed_));
            completed_.reset();
        }
    }
    wake_.notify_one();
    if (pending_ && pending_->generation != generation_)
        pending_.reset();
    if (!pending_)
        return;
    auto& completed = *pending_;
    auto* item = nodes_.at(completed.parentId);
    if (!completed.result) {
        const auto message = completed.result.error().code == ArchiveErrorCode::ResourceLimitExceeded
            ? tr("The operation exceeded a safety limit.") : fromUtf8(completed.result.error().message);
        item->loading = false;
        item->loaded = true;
        if (item != root_.get())
            emit dataChanged(nodeIndex(item), nodeIndex(item));
        pending_.reset();
        emit scanFailed(message);
        return;
    }
    auto& entries = *completed.result;
    const auto count = std::min<std::size_t>(entries.size() - completed.consumed, 512);
    if (count > 0) {
        const auto first = static_cast<int>(item->children.size());
        item->children.reserve(std::max(item->children.size() + count, item->children.capacity() * 2));
        std::vector<std::unique_ptr<Node>> batch;
        batch.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            auto child = std::make_unique<Node>();
            child->entry = std::move(entries[completed.consumed + i]);
            child->parent = item;
            child->id = nextId_++;
            child->row = first + static_cast<int>(i);
            child->loaded = !child->entry.directory;
            batch.push_back(std::move(child));
        }
        try {
            nodes_.reserve(nodes_.size() + count);
            for (const auto& child : batch)
                nodes_.emplace(child->id, child.get());
        } catch (...) {
            for (const auto& child : batch)
                nodes_.erase(child->id);
            throw;
        }
        // All allocations are complete before Qt observes the inserted rows.
        beginInsertRows(nodeIndex(item), first, first + static_cast<int>(count) - 1);
        for (auto& child : batch)
            item->children.push_back(std::move(child));
        endInsertRows();
        completed.consumed += count;
    }
    if (completed.consumed == entries.size()) {
        item->loading = false;
        item->loaded = true;
        if (item != root_.get())
            emit dataChanged(nodeIndex(item), nodeIndex(item));
        pending_.reset();
    }
}

}
