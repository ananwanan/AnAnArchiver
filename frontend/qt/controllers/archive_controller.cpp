#include "archive_controller.h"
#include "platform/path_conversion.h"

#include <algorithm>
#include <utility>

namespace ananarchiver::qt {

ArchiveController::ArchiveController(application::ReaderFactory factory, QObject* parent)
    : QObject(parent), service_(tasks_, std::move(factory)), status_(tr("Ready"))
{
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &ArchiveController::poll);
    timer_.start();
}

ArchiveController::~ArchiveController()
{
    if (active_)
        tasks_.cancel(*active_);
    if (retired_)
        tasks_.cancel(*retired_);
}

void ArchiveController::retireActive()
{
    if (!active_)
        return;
    tasks_.cancel(*active_);
    Q_ASSERT(!retired_);
    retired_ = *active_;
    active_.reset();
}

void ArchiveController::openUrl(const QUrl& url)
{
    if (url.isLocalFile())
        openFile(url.toLocalFile());
    else
        showError({ArchiveErrorCode::UnsupportedOperation, "Only local archives can be opened.", {}, {}});
}

void ArchiveController::openFile(const QString& path)
{
    try {
        requestOpen(path);
    } catch (const std::bad_alloc&) {
        allocationFailed();
    } catch (const std::exception& error) {
        status_ = tr("Failed");
        showError({ArchiveErrorCode::InvalidPathEncoding, error.what(), {}, {}});
        emit operationFinished(false);
    }
}

void ArchiveController::requestOpen(const QString& path)
{
    if (path.isEmpty())
        return;
    retireActive();
    entries_.clear();
    currentFile_ = path;
    formatName_.clear();
    currentEntry_.clear();
    errorMessage_.clear();
    errorDetails_.clear();
    elapsedMilliseconds_ = 0;
    pendingFile_ = path;
    status_ = !retired_ ? tr("Indexing archive…") : tr("Waiting for the previous operation to stop…");
    if (!retired_)
        startPending();
    emit changed();
}

void ArchiveController::startPending()
{
    const auto path = std::exchange(pendingFile_, {});
    auto result = service_.open(toPath(path));
    if (!result) {
        status_ = tr("Failed");
        showError(result.error());
        emit operationFinished(false);
        return;
    }
    active_ = *result;
    status_ = tr("Indexing archive…");
    emit changed();
}

void ArchiveController::cancel()
{
    if (!pendingFile_.isEmpty()) {
        pendingFile_.clear();
        status_ = tr("Cancelled — the list may be incomplete");
        emit changed();
        emit operationFinished(false);
    }
    if (active_) {
        tasks_.cancel(*active_);
        status_ = tr("Cancelling…");
        emit changed();
    }
}

void ArchiveController::close()
{
    retireActive();
    pendingFile_.clear();
    entries_.clear();
    currentFile_.clear();
    currentEntry_.clear();
    formatName_.clear();
    errorMessage_.clear();
    errorDetails_.clear();
    status_ = tr("Ready");
    elapsedMilliseconds_ = 0;
    emit changed();
}

void ArchiveController::dismissError()
{
    errorMessage_.clear();
    errorDetails_.clear();
    emit changed();
}

void ArchiveController::showError(const ArchiveError& error)
{
    switch (error.code) {
    case ArchiveErrorCode::FileNotFound: errorMessage_ = tr("The archive could not be found."); break;
    case ArchiveErrorCode::PermissionDenied: errorMessage_ = tr("Permission to read the archive was denied."); break;
    case ArchiveErrorCode::UnsupportedFormat: errorMessage_ = tr("This archive format is not supported."); break;
    case ArchiveErrorCode::UnsupportedCompression: errorMessage_ = tr("This compression method is not supported."); break;
    case ArchiveErrorCode::UnsupportedOperation: errorMessage_ = tr("This operation is not supported."); break;
    case ArchiveErrorCode::InvalidArchive: errorMessage_ = tr("The file is not a valid archive."); break;
    case ArchiveErrorCode::CorruptedArchive: errorMessage_ = tr("The archive is damaged or incomplete."); break;
    case ArchiveErrorCode::WrongPassword: errorMessage_ = tr("The archive password is incorrect."); break;
    case ArchiveErrorCode::PasswordRequired: errorMessage_ = tr("A password is required for this archive."); break;
    case ArchiveErrorCode::DiskFull: errorMessage_ = tr("There is not enough free disk space."); break;
    case ArchiveErrorCode::OutputExists: errorMessage_ = tr("The output already exists."); break;
    case ArchiveErrorCode::Cancelled: errorMessage_ = tr("The operation was cancelled."); break;
    case ArchiveErrorCode::ResourceLimitExceeded: errorMessage_ = tr("The operation exceeded a safety limit."); break;
    case ArchiveErrorCode::InvalidPathEncoding: errorMessage_ = tr("A filename contains invalid text encoding."); break;
    case ArchiveErrorCode::IoError: errorMessage_ = tr("A file could not be read."); break;
    case ArchiveErrorCode::BackendWarning: errorMessage_ = tr("The archive could not be read reliably."); break;
    case ArchiveErrorCode::BackendError:
    case ArchiveErrorCode::Unknown: errorMessage_ = tr("The archive operation failed."); break;
    }
    errorDetails_ = fromUtf8(error.message);
    if (!error.backendMessage.empty())
        errorDetails_ += QStringLiteral("\n") + fromUtf8(error.backendMessage);
    if (!error.path.empty())
        errorDetails_ += QStringLiteral("\n") + fromPath(error.path);
    emit changed();
}

void ArchiveController::poll()
{
    try {
        pollTasks();
    } catch (const std::bad_alloc&) {
        allocationFailed();
    } catch (const std::exception& error) {
        if (active_)
            tasks_.cancel(*active_);
        pendingFile_.clear();
        status_ = tr("Failed");
        showError({ArchiveErrorCode::Unknown, error.what(), {}, {}});
    }
}

void ArchiveController::allocationFailed()
{
    if (active_)
        tasks_.cancel(*active_);
    pendingFile_.clear();
    entries_.clear();
    status_ = tr("Failed");
    showError({ArchiveErrorCode::ResourceLimitExceeded, "Out of memory", {}, {}});
    if (!active_)
        emit operationFinished(false);
}

void ArchiveController::pollTasks()
{
    if (retired_) {
        tasks_.takeEntries(*retired_, 2);
        if (tasks_.release(*retired_))
            retired_.reset();
    }
    if (!active_ && !pendingFile_.isEmpty() && !retired_)
        startPending();
    if (!active_)
        return;
    const auto id = *active_;
    auto snapshot = tasks_.snapshot(id);
    if (!snapshot)
        return;
    entries_.append(tasks_.takeEntries(id, 2));
    currentEntry_ = fromUtf8(snapshot->currentFile);
    elapsedMilliseconds_ = snapshot->elapsed.count();
    if (snapshot->archive)
        formatName_ = fromUtf8(snapshot->archive->formatName);
    using application::TaskState;
    switch (snapshot->state) {
    case TaskState::Pending:
    case TaskState::Running: status_ = tr("Indexing archive…"); break;
    case TaskState::Cancelling: status_ = tr("Cancelling…"); break;
    case TaskState::Cancelled: status_ = tr("Cancelled — the list may be incomplete"); break;
    case TaskState::Succeeded: status_ = tr("Archive indexed"); break;
    case TaskState::Failed: status_ = tr("Failed — the list may be incomplete"); break;
    }
    const auto terminal = snapshot->state == TaskState::Cancelled ||
        snapshot->state == TaskState::Succeeded || snapshot->state == TaskState::Failed;
    if (terminal && tasks_.release(id)) {
        active_.reset();
        if (snapshot->error && snapshot->state == TaskState::Failed)
            showError(*snapshot->error);
        emit changed();
        emit operationFinished(snapshot->state == TaskState::Succeeded);
        return;
    }
    emit changed();
}

}
