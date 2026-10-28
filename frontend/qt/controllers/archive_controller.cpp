#include "archive_controller.h"
#include "platform/archive_error_details.h"
#include "platform/path_conversion.h"

#include <algorithm>
#include <utility>

namespace ananarchiver::qt {

ArchiveController::ArchiveController(application::ReaderFactory factory, QObject* parent)
    : QObject(parent), service_(tasks_, std::move(factory)), status_(tr("Ready"))
{
    entries_.setFolderNavigationEnabled(true);
    connect(&entries_, &ArchiveEntryModel::selectionChanged, this, &ArchiveController::changed);
    connect(&entries_, &ArchiveEntryModel::folderChanged, this, &ArchiveController::changed);
    connect(&entries_, &ArchiveEntryModel::queryingChanged, this, &ArchiveController::changed);
    connect(&entries_, &ArchiveEntryModel::queryFailed, this, [this](const QString& message) {
        showError({ArchiveErrorCode::ResourceLimitExceeded, message.toStdString(), {}, {}});
    });
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
    emit archiveOpening(path);
    retireActive();
    indexed_ = false;
    extracting_ = false;
    passwordRequested_ = false;
    extractionNeedsPassword_ = false;
    conflictId_ = 0;
    conflictPath_.clear();
    processedBytes_ = 0;
    totalBytes_ = -1;
    bytesPerSecond_ = 0;
    remainingMilliseconds_ = -1;
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
    if (auto snapshot = tasks_.snapshot(*active_))
        emit taskObserved(*snapshot, currentFile_);
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

void ArchiveController::cancelTask(application::TaskId id)
{
    if (active_ && *active_ == id)
        cancel();
    else if (retired_ && *retired_ == id)
        tasks_.cancel(id);
}

Result<ExtractionRequest> ArchiveController::selectedExtractionRequest(
    const std::filesystem::path& destination) const
{
    if (!canExtract() || selectionCount() == 0)
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
            "Select archive entries after indexing has finished.", {}, {}});
    ExtractionRequest request;
    request.destination = destination;
    request.selectedIndices = entries_.selectedIndices();
    request.selectedDirectoryPaths = entries_.selectedDirectoryPaths();
    request.overwrite = OverwritePolicy::Overwrite;
    request.links = LinkPolicy::Skip;
    return request;
}

void ArchiveController::close()
{
    emit archiveClosed();
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
    indexed_ = false;
    extracting_ = false;
    passwordRequested_ = false;
    conflictId_ = 0;
    conflictPath_.clear();
    processedBytes_ = 0;
    totalBytes_ = -1;
    bytesPerSecond_ = 0;
    remainingMilliseconds_ = -1;
    emit changed();
}

void ArchiveController::selectRow(int row, bool toggle, bool range)
{
    if (canExtract())
        entries_.selectRow(row, toggle, range);
}

void ArchiveController::selectAll()
{
    if (canExtract())
        entries_.selectAll();
}

void ArchiveController::clearSelection()
{
    if (!busy())
        entries_.clearSelection();
}

void ArchiveController::navigateToFolder(const QString& path)
{
    entries_.navigateToFolder(path);
}

bool ArchiveController::navigateToRow(int row)
{
    return entries_.navigateToRow(row);
}

void ArchiveController::navigateUp()
{
    const auto folder = currentFolder();
    const auto slash = folder.lastIndexOf(QChar(u'/'));
    navigateToFolder(slash < 0 ? QString() : folder.left(slash));
}

namespace {

// Passwords stay within the operation. QML also clears its input on every close.
std::string takePassword(QString& password)
{
    auto bytes = password.toUtf8();
    std::string result(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    bytes.fill('\0');
    password.fill(QChar(u'\0'));
    password.clear();
    return result;
}

QString displayDestination(const std::filesystem::path& path)
{
    auto text = fromPath(path);
#ifdef _WIN32
    // The long-path prefix belongs to native IO, not the overwrite prompt.
    if (text.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive))
        return QStringLiteral("\\\\") + text.mid(8);
    if (text.startsWith(QStringLiteral("\\\\?\\")) && text.size() > 6 && text.at(5) == QChar(u':'))
        return text.mid(4);
#endif
    return text;
}

}

void ArchiveController::extract(const QString& destination, bool onlySelected, QString password,
                                int overwrite, int links)
{
    extractionNeedsPassword_ = false;
    if (!canExtract() || destination.trimmed().isEmpty() ||
        (onlySelected && selectionCount() == 0) || overwrite < 0 || overwrite > 4 ||
        links < 0 || links > 2) {
        password.fill(QChar(u'\0'));
        return;
    }
    try {
        ExtractionRequest request;
        request.destination = toPath(destination);
        if (!request.destination.is_absolute()) {
            showError({ArchiveErrorCode::UnsafePath, "Choose an absolute extraction destination.", {}, request.destination});
            password.fill(QChar(u'\0'));
            return;
        }
        request.overwrite = static_cast<OverwritePolicy>(overwrite);
        request.links = static_cast<LinkPolicy>(links);
        if (onlySelected) {
            request.selectedIndices = entries_.selectedIndices();
            request.selectedDirectoryPaths = entries_.selectedDirectoryPaths();
        }
        entries_.setExtractionTotals(request, onlySelected);
        if (!password.isEmpty())
            request.password = takePassword(password);
        auto result = service_.extract(toPath(currentFile_), std::move(request));
        if (!result) {
            showError(result.error());
            return;
        }
        active_ = *result;
        extracting_ = true;
        passwordRequested_ = false;
        conflictId_ = 0;
        conflictPath_.clear();
        errorMessage_.clear();
        errorDetails_.clear();
        elapsedMilliseconds_ = 0;
        processedBytes_ = 0;
        totalBytes_ = -1;
        bytesPerSecond_ = 0;
        remainingMilliseconds_ = -1;
        currentEntry_.clear();
        status_ = tr("Extracting archive…");
        if (auto snapshot = tasks_.snapshot(*active_))
            emit taskObserved(*snapshot, destination);
        emit changed();
    } catch (const std::bad_alloc&) {
        allocationFailed();
    } catch (const std::exception& error) {
        showError({ArchiveErrorCode::Unknown, error.what(), {}, {}});
    }
    password.fill(QChar(u'\0'));
}

void ArchiveController::resolveConflict(int overwrite, bool applyToAll)
{
    if (!active_ || !conflictId_ || overwrite < 1 || overwrite > 4)
        return;
    if (tasks_.resolveConflict(*active_, conflictId_,
        {static_cast<OverwritePolicy>(overwrite), applyToAll})) {
        conflictId_ = 0;
        conflictPath_.clear();
        emit changed();
    }
}

void ArchiveController::retryOpenPassword(QString password)
{
    if (!passwordRequested_ || busy() || currentFile_.isEmpty()) {
        password.fill(QChar(u'\0'));
        return;
    }
    try {
        auto secret = takePassword(password);
        auto result = service_.open(toPath(currentFile_), std::move(secret));
        passwordRequested_ = false;
        errorMessage_.clear();
        errorDetails_.clear();
        entries_.clear();
        if (!result) {
            showError(result.error());
            return;
        }
        active_ = *result;
        extracting_ = false;
        status_ = tr("Indexing archive…");
        emit changed();
    } catch (const std::exception& error) {
        password.fill(QChar(u'\0'));
        showError({ArchiveErrorCode::Unknown, error.what(), {}, {}});
    }
}

void ArchiveController::dismissPassword()
{
    passwordRequested_ = false;
    emit passwordDismissed();
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
    case ArchiveErrorCode::PermissionDenied: errorMessage_ = tr("Permission to access a file was denied."); break;
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
    case ArchiveErrorCode::CleanupFailed: errorMessage_ = tr("Extraction cleanup or recovery failed. See the details for remaining files."); break;
    case ArchiveErrorCode::ResourceLimitExceeded: errorMessage_ = tr("The operation exceeded a safety limit."); break;
    case ArchiveErrorCode::InvalidPathEncoding: errorMessage_ = tr("A filename contains invalid text encoding."); break;
    case ArchiveErrorCode::UnsafePath: errorMessage_ = tr("An extraction path is unsafe."); break;
    case ArchiveErrorCode::IoError: errorMessage_ = tr("A file could not be read or written."); break;
    case ArchiveErrorCode::BackendWarning: errorMessage_ = tr("The archive could not be read reliably."); break;
    case ArchiveErrorCode::BackendError:
    case ArchiveErrorCode::Unknown: errorMessage_ = tr("The archive operation failed."); break;
    }
    errorDetails_ = formatArchiveErrorDetails(error);
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
        auto snapshot = tasks_.snapshot(*retired_);
        const bool released = tasks_.release(*retired_);
        if (snapshot) {
            using application::TaskState;
            const bool terminal = snapshot->state == TaskState::Succeeded ||
                snapshot->state == TaskState::Cancelled || snapshot->state == TaskState::Failed;
            if (terminal && !released) snapshot->state = TaskState::Running;
            emit taskObserved(*snapshot, {});
        }
        if (released)
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
    if (!extracting_)
        entries_.append(tasks_.takeEntries(id, 2));
    currentEntry_ = fromUtf8(snapshot->currentFile);
    elapsedMilliseconds_ = snapshot->elapsed.count();
    if (extracting_) {
        processedBytes_ = static_cast<double>(snapshot->processedBytes);
        totalBytes_ = snapshot->totalBytes ? static_cast<double>(*snapshot->totalBytes) : -1;
        bytesPerSecond_ = snapshot->bytesPerSecond;
        remainingMilliseconds_ = snapshot->estimatedRemaining ? snapshot->estimatedRemaining->count() : -1;
        conflictId_ = snapshot->conflict ? snapshot->conflictId : 0;
        conflictPath_ = snapshot->conflict ? displayDestination(snapshot->conflict->destination) : QString();
    }
    if (snapshot->archive)
        formatName_ = fromUtf8(snapshot->archive->formatName);
    using application::TaskState;
    switch (snapshot->state) {
    case TaskState::Pending:
    case TaskState::Running: status_ = extracting_
        ? (awaitingConflict() ? tr("Waiting for an overwrite decision…") : tr("Extracting archive…"))
        : tr("Indexing archive…"); break;
    case TaskState::Cancelling: status_ = tr("Cancelling…"); break;
    case TaskState::Cancelled: status_ = extracting_ ? tr("Extraction cancelled") : tr("Cancelled — the list may be incomplete"); break;
    case TaskState::Succeeded: status_ = extracting_ ? tr("Extraction complete") : tr("Archive indexed"); break;
    case TaskState::Failed: status_ = extracting_ ? tr("Extraction failed") : tr("Failed — the list may be incomplete"); break;
    }
    const auto terminal = snapshot->state == TaskState::Cancelled ||
        snapshot->state == TaskState::Succeeded || snapshot->state == TaskState::Failed;
    const bool released = terminal && tasks_.release(id);
    // Backend completion can precede draining the final metadata batches.
    // Keep the task active in the center until the controller has finished too.
    auto observed = *snapshot;
    if (terminal && !released)
        observed.state = TaskState::Running;
    emit taskObserved(observed, currentFile_);
    if (released) {
        active_.reset();
        extractionNeedsPassword_ = extracting_ && snapshot->error &&
            (snapshot->error->code == ArchiveErrorCode::PasswordRequired ||
             snapshot->error->code == ArchiveErrorCode::WrongPassword);
        conflictId_ = 0;
        conflictPath_.clear();
        if (!extracting_)
            indexed_ = snapshot->state == TaskState::Succeeded;
        if (!extracting_ && indexed_)
            emit archiveIndexed(currentFile_);
        if (!extracting_ && snapshot->error &&
            (snapshot->error->code == ArchiveErrorCode::PasswordRequired ||
             snapshot->error->code == ArchiveErrorCode::WrongPassword))
            passwordRequested_ = true;
        if (snapshot->error && snapshot->state == TaskState::Failed)
            showError(*snapshot->error);
        emit changed();
        emit operationFinished(snapshot->state == TaskState::Succeeded);
        return;
    }
    emit changed();
}

}
