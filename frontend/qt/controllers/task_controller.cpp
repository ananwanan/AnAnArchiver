#include "task_controller.h"
#include "platform/archive_error_details.h"
#include "platform/path_conversion.h"
#include <QLocale>
#include <algorithm>
#include <limits>

namespace ananarchiver::qt {
namespace {
QString duration(std::chrono::milliseconds value) {
    const auto seconds = std::max<std::int64_t>(0, value.count() / 1000);
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QChar(u'0'))
        .arg(seconds % 60, 2, 10, QChar(u'0'));
}
QString bytes(std::uint64_t value) {
    return QLocale().formattedDataSize(static_cast<qint64>(std::min<std::uint64_t>(value,
        static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()))));
}
} // namespace

TaskController::TaskController(QObject* parent) : QAbstractListModel(parent) {}
bool TaskController::terminal(application::TaskState state) {
    using application::TaskState;
    return state == TaskState::Succeeded || state == TaskState::Failed || state == TaskState::Cancelled;
}
int TaskController::activeCount() const {
    return static_cast<int>(std::count_if(records_.begin(), records_.end(),
        [](const auto& record) { return !terminal(record.snapshot.state); }));
}
int TaskController::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QHash<int, QByteArray> TaskController::roleNames() const {
    return {{IdRole,"taskId"},{LabelRole,"taskLabel"},{StateRole,"taskState"},{PathRole,"taskPath"},
        {CurrentFileRole,"taskCurrentFile"},{ProgressRole,"taskProgress"},{BytesRole,"taskBytes"},
        {SpeedRole,"taskSpeed"},{ElapsedRole,"taskElapsed"},{RemainingRole,"taskRemaining"},
        {ErrorRole,"taskError"},{DetailsRole,"taskDetails"},{CanCancelRole,"taskCanCancel"}};
}
QVariant TaskController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.column() || index.row() < 0 || index.row() >= count())
        return {};
    const auto& record = records_[static_cast<std::size_t>(index.row())];
    const auto& task = record.snapshot;
    using application::TaskState;
    const bool payload = task.type == application::TaskType::ExtractArchive ||
        (task.type == application::TaskType::CompressArchive &&
            task.compressionPhase == CompressionPhase::Writing);
    switch (role) {
    case IdRole: return record.id;
    case LabelRole:
        if (record.id.startsWith(QStringLiteral("drag:"))) return tr("Drag and drop");
        switch (task.type) {
        case application::TaskType::OpenArchive: return tr("Open archive");
        case application::TaskType::ExtractArchive: return tr("Extract archive");
        case application::TaskType::CompressArchive: return tr("Create archive");
        }
        return {};
    case StateRole:
        switch (task.state) {
        case TaskState::Pending: return tr("Pending");
        case TaskState::Running: return task.conflict ? tr("Waiting for an overwrite decision…")
            : task.compressionPhase == CompressionPhase::Finalizing ? tr("Finishing archive…") : tr("Running");
        case TaskState::Cancelling: return tr("Cancelling…");
        case TaskState::Cancelled: return tr("Cancelled");
        case TaskState::Succeeded: return tr("Succeeded");
        case TaskState::Failed: return tr("Failed");
        }
        return {};
    case PathRole: return record.path;
    case CurrentFileRole: return fromUtf8(task.currentFile);
    case ProgressRole:
        if (task.state == TaskState::Succeeded) return 1.0;
        if (payload && task.totalBytes && *task.totalBytes)
            return std::clamp(static_cast<double>(task.processedBytes) / static_cast<double>(*task.totalBytes), 0.0, 1.0);
        return -1.0;
    case BytesRole:
        return payload || task.type == application::TaskType::ExtractArchive
            ? (task.totalBytes ? tr("%1 / %2").arg(bytes(task.processedBytes), bytes(*task.totalBytes)) : bytes(task.processedBytes))
            : tr("%1 entries").arg(task.processedEntries);
    case SpeedRole: return task.bytesPerSecond > 0 && payload
        ? tr("%1/s").arg(bytes(static_cast<std::uint64_t>(task.bytesPerSecond))) : QString();
    case ElapsedRole: return duration(task.elapsed);
    case RemainingRole: return task.estimatedRemaining ? duration(*task.estimatedRemaining) : QString();
    case ErrorRole: return task.error && task.state == TaskState::Failed ? tr("The archive operation failed.") : QString();
    case DetailsRole:
        if (task.error)
            return formatArchiveErrorDetails(*task.error);
        return QString();
    case CanCancelRole: return !terminal(task.state) && task.state != TaskState::Cancelling;
    default: return {};
    }
}

void TaskController::observe(const QString& source, const application::TaskSnapshot& snapshot,
                             const QString& path, Cancel cancel) {
    const auto id = source + QChar(u':') + QString::number(snapshot.id);
    const auto found = std::find_if(records_.begin(), records_.end(),
        [&](const auto& record) { return record.id == id; });
    if (found != records_.end()) {
        const auto row = static_cast<int>(found - records_.begin());
        found->snapshot = snapshot;
        if (found->path.isEmpty()) found->path = path;
        emit dataChanged(index(row), index(row));
    } else {
        Record prepared{id, path, snapshot, std::move(cancel)};
        if (records_.capacity() < records_.size() + 1)
            records_.reserve(std::max<std::size_t>(65, records_.capacity() * 2));
        // Bounded session history. Active operations are never evicted.
        if (records_.size() >= 64) {
            const auto oldest = std::find_if(records_.rbegin(), records_.rend(),
                [](const auto& record) { return terminal(record.snapshot.state); });
            if (oldest != records_.rend()) {
                const auto row = static_cast<int>(records_.rend() - oldest - 1);
                beginRemoveRows({}, row, row);
                records_.erase(records_.begin() + row);
                endRemoveRows();
            }
        }
        beginInsertRows({}, 0, 0);
        records_.insert(records_.begin(), std::move(prepared));
        endInsertRows();
    }
    emit changed();
}
void TaskController::cancelTask(const QString& id) {
    const auto found = std::find_if(records_.begin(), records_.end(), [&](const auto& record) { return record.id == id; });
    if (found != records_.end() && !terminal(found->snapshot.state) && found->cancel) {
        // The callback may publish another snapshot; do not retain a vector reference across it.
        const auto callback = found->cancel;
        const auto task = found->snapshot.id;
        callback(task);
    }
}
void TaskController::clearFinished() {
    for (int row = count() - 1; row >= 0; --row) {
        if (!terminal(records_[static_cast<std::size_t>(row)].snapshot.state)) continue;
        beginRemoveRows({}, row, row);
        records_.erase(records_.begin() + row);
        endRemoveRows();
    }
    emit changed();
}
} // namespace ananarchiver::qt
