#include "drag_controller.h"
#include "archive_controller.h"
#include "compression_controller.h"
#include "platform/path_conversion.h"
#include "platform/temp_file_manager.h"

#include <ananarchiver/password.h>
#include <QDrag>
#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <algorithm>
#include <chrono>
#include <utility>

namespace ananarchiver::qt {
namespace {

bool terminal(application::TaskState state) {
    using application::TaskState;
    return state == TaskState::Succeeded || state == TaskState::Failed || state == TaskState::Cancelled;
}

ArchiveError cancelled() {
    return {ArchiveErrorCode::Cancelled, "Drag operation cancelled", {}, {}};
}

} // namespace

struct DragController::Operation {
    ExtractionRequest request;
    std::filesystem::path source;
    std::filesystem::path root;
    QList<QUrl> outputUrls;
    bool directory{};
    ~Operation() {
        if (request.password)
            scrubPassword(*request.password);
    }
};

DragController::DragController(ArchiveController& archive, CompressionController& compression,
                               QObject* parent, application::ReaderFactory factory,
                               std::function<Qt::DropAction(QDrag&)> nativeDragExecutor)
    : QObject(parent), archive_(archive), compression_(compression),
      temporary_(std::make_shared<TempFileManager>()), service_(tasks_, std::move(factory)),
      nativeDragExecutor_(std::move(nativeDragExecutor)),
      status_(tr("Ready")) {
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &DragController::poll);
    connect(&archive_, &ArchiveController::changed, this, &DragController::invalidateExport);
    timer_.start();
}

DragController::~DragController() {
    if (active_)
        tasks_.cancel(*active_);
    // TaskManager joins the producer before the manager's last owner performs
    // worker cleanup. Never delete an output directory while extraction runs.
}

bool DragController::begin(Result<application::TaskId> task, Phase phase, const QString& status) {
    if (!task) {
        showError(task.error());
        return false;
    }
    active_ = *task;
    phase_ = phase;
    status_ = status;
    if (const auto snapshot = tasks_.snapshot(*active_))
        observeTask(*snapshot);
    emit changed();
    return true;
}

void DragController::observeTask(application::TaskSnapshot snapshot) {
    // InvalidArchive is a completed classification for a plain input, and
    // password-required is a completed classification for an archive. The
    // task center must not label those successful routing decisions as errors.
    if (phase_ == Phase::ProbeDrop && snapshot.state == application::TaskState::Failed && snapshot.error) {
        const auto code = snapshot.error->code;
        if (code == ArchiveErrorCode::InvalidArchive || code == ArchiveErrorCode::UnsupportedFormat ||
            code == ArchiveErrorCode::PasswordRequired || code == ArchiveErrorCode::WrongPassword ||
            code == ArchiveErrorCode::UnsupportedCompression) {
            snapshot.state = application::TaskState::Succeeded;
            snapshot.error.reset();
        }
    }
    emit taskObserved(snapshot, operation_ ? fromPath(operation_->source) : QString());
}

void DragController::dropUrls(const QList<QUrl>& urls) {
    if (busy() || archive_.busy() || compression_.busy() || urls.isEmpty())
        return;
    for (const auto& url : urls) {
        if (!url.isLocalFile() || !url.host().isEmpty() || url.toLocalFile().isEmpty()) {
            showError({ArchiveErrorCode::UnsupportedOperation,
                       "Only local file URLs can be dropped here", {}, {}});
            return;
        }
    }
    dismissError();
    droppedUrls_ = urls;
    if (urls.size() > 1) {
        addDroppedInputs();
        return;
    }
    try {
        operation_ = std::make_shared<Operation>();
        operation_->source = toPath(urls.first().toLocalFile());
        auto task = tasks_.start(application::TaskType::OpenArchive,
            [operation = operation_](application::TaskContext& context) -> Result<void> {
                if (context.cancellation().stop_requested())
                    return std::unexpected(cancelled());
                const QFileInfo input(fromPath(operation->source));
                if (!input.exists())
                    return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                        "Could not inspect dropped input", {}, operation->source});
                operation->directory = input.isDir();
                if (!operation->directory && !input.isFile())
                    return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
                        "Dropped input must be a regular file or directory", {}, operation->source});
                return {};
            });
        begin(std::move(task), Phase::InspectDrop, tr("Checking dropped input…"));
    } catch (const std::exception& error) {
        showError({ArchiveErrorCode::ResourceLimitExceeded,
                   "Could not start dropped input task", error.what(), {}});
    }
}

void DragController::addDroppedInputs() {
    if (!archive_.busy() && !compression_.busy()) {
        compression_.addUrls(droppedUrls_);
        status_ = tr("Files added for compression");
        emit changed();
        emit createRequested();
    }
    droppedUrls_.clear();
    operation_.reset();
}

void DragController::prepareExport(QString password) {
    if (busy() || compression_.busy()) {
        password.fill(QChar{});
        return;
    }
    discardExport();
    dismissError();
    try {
        auto request = archive_.selectedExtractionRequest({});
        if (!request) {
            password.fill(QChar{});
            showError(request.error());
            return;
        }
        operation_ = std::make_shared<Operation>();
        operation_->request = std::move(*request);
        operation_->source = toPath(archive_.currentFile());
        exportedArchive_ = archive_.currentFile();
        exportedIndices_ = operation_->request.selectedIndices;
        exportedDirectoryPaths_ = operation_->request.selectedDirectoryPaths;
        auto& extraction = operation_->request;
        extraction.overwrite = OverwritePolicy::Skip;
        extraction.links = LinkPolicy::Skip;
        extraction.limits.maxTotalBytes = 16ULL * 1024 * 1024 * 1024;
        extraction.limits.maxEntryBytes = extraction.limits.maxTotalBytes;
        extraction.limits.maxElapsed = std::chrono::hours{2};
        if (!password.isEmpty()) {
            auto utf8 = password.toUtf8();
            extraction.password = std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())};
            utf8.fill('\0');
        }
        password.fill(QChar{});
        password.clear();
        auto task = tasks_.start(application::TaskType::ExtractArchive,
            [operation = operation_, temporary = temporary_](application::TaskContext& context) -> Result<void> {
                if (context.cancellation().stop_requested())
                    return std::unexpected(cancelled());
                auto root = temporary->create();
                if (!root)
                    return std::unexpected(root.error());
                operation->root = *root;
                operation->request.destination = *root;
                if (context.cancellation().stop_requested())
                    return std::unexpected(cancelled());
                return {};
            });
        if (!begin(std::move(task), Phase::CreateRoot, tr("Preparing selected files…")))
            operation_.reset();
    } catch (const std::exception& error) {
        password.fill(QChar{});
        showError({ArchiveErrorCode::ResourceLimitExceeded,
                   "Could not prepare selected files", error.what(), {}});
    }
}

void DragController::startExtract() {
    exportRoot_ = operation_->root;
    auto request = std::move(operation_->request);
    if (!begin(service_.extract(operation_->source, std::move(request)),
               Phase::Extract, tr("Extracting selected files for dragging…"))) {
        discardExport();
        operation_.reset();
    }
}

void DragController::startCollect() {
    auto task = tasks_.start(application::TaskType::ExtractArchive,
        [operation = operation_](application::TaskContext& context) -> Result<void> {
            // Listing only the immediate children preserves selected folder
            // structure without scanning the complete extracted directory tree.
            const QDir directory(fromPath(operation->root));
            if (!directory.exists() || !directory.isReadable())
                return std::unexpected(ArchiveError{ArchiveErrorCode::IoError,
                    "Could not list exported files", {}, operation->root});
            const QDirListing listing(directory.path(), QDirListing::IteratorFlag::IncludeHidden);
            for (const auto& item : listing) {
                if (context.cancellation().stop_requested())
                    return std::unexpected(cancelled());
                if (item.isSymLink() || (!item.isFile() && !item.isDir()))
                    continue;
                // Bound native MIME serialization, which desktop drag APIs
                // perform on the GUI thread. A selected folder has one URL
                // regardless of the number of files it contains.
                if (operation->outputUrls.size() >= 10000)
                    return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                        "Drag export exceeds 10,000 top-level URLs; select a folder or use Extract instead", {}, {}});
                operation->outputUrls.push_back(QUrl::fromLocalFile(item.filePath()));
            }
            if (operation->outputUrls.isEmpty())
                return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedOperation,
                    "The selected entries produced no regular files or directories; links are skipped", {}, {}});
            return {};
        });
    if (!begin(std::move(task), Phase::Collect, tr("Preparing file URLs…"))) {
        discardExport();
        operation_.reset();
    }
}

void DragController::finishExport(const application::TaskSnapshot& snapshot) {
    if (snapshot.state == application::TaskState::Succeeded) {
        exportUrls_ = std::move(operation_->outputUrls);
        status_ = tr("Selected files are ready. Drag again to copy them.");
        // Extraction completion cannot start a delayed native drag: it must
        // remain an explicit new user gesture after the files are ready.
    } else {
        discardExport();
    }
    operation_.reset();
    emit changed();
    invalidateExport();
}

void DragController::startDrag() {
    if (!ready())
        return;
    invalidateExport();
    if (!ready())
        return;
    if (!nativeDragExecutor_ && !(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
        status_ = tr("Drag selected rows or the Export button into a folder.");
        emit changed();
        return;
    }
    // A native drag can run a nested event loop for an arbitrary duration.
    // Hold the output throughout it; start desktop-copy retention afterwards.
    if (!temporary_->hold(exportRoot_)) {
        discardExport();
        status_ = tr("Export expired. Prepare the selected files again.");
        emit changed();
        return;
    }
    retained_ = true;
    const auto root = exportRoot_;
    const auto generation = ++retentionGeneration_;
    QPointer<DragController> alive(this);
    QPointer<QDrag> drag(new QDrag(this));
    auto* mime = new QMimeData;
    mime->setUrls(exportUrls_);
    drag->setMimeData(mime);
    dragging_ = true;
    status_ = tr("Dragging selected files…");
    emit changed();
    const auto action = nativeDragExecutor_ ? nativeDragExecutor_(*drag) : drag->exec(Qt::CopyAction, Qt::CopyAction);
    if (!alive)
        return;
    if (drag)
        delete drag;
    dragging_ = false;
    constexpr auto retention = std::chrono::minutes{10};
    temporary_->retain(root, std::chrono::duration_cast<std::chrono::seconds>(retention));
    QTimer::singleShot(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(retention).count()), this,
        [this, root, generation] {
            if (exportRoot_ == root && retentionGeneration_ == generation && !dragging_) {
                discardExport();
                status_ = tr("Export expired. Prepare the selected files again.");
                emit changed();
            }
        });
    status_ = action == Qt::CopyAction ? tr("Selected files copied") : tr("Drag finished. Files remain ready.");
    emit changed();
    invalidateExport();
}

void DragController::cancel() {
    if (dragging_) {
        QDrag::cancel();
        status_ = tr("Cancelling…");
        emit changed();
    } else if (active_) {
        tasks_.cancel(*active_);
        status_ = tr("Cancelling…");
        if (const auto snapshot = tasks_.snapshot(*active_))
            observeTask(*snapshot);
        emit changed();
    } else if (!dragging_) {
        discardExport();
        status_ = tr("Ready");
        emit changed();
    }
}

void DragController::cancelTask(application::TaskId id) {
    if (active_ && *active_ == id)
        cancel();
}

void DragController::discardExport() {
    ++retentionGeneration_;
    if (!exportRoot_.empty() && !retained_)
        temporary_->discard(exportRoot_);
    exportRoot_.clear();
    exportUrls_.clear();
    exportedArchive_.clear();
    exportedIndices_.reset();
    exportedDirectoryPaths_.clear();
    retained_ = false;
}

void DragController::invalidateExport() {
    if (exportUrls_.isEmpty() || dragging_)
        return;
    auto current = archive_.selectedExtractionRequest({});
    if (!current || archive_.currentFile() != exportedArchive_ || current->selectedIndices != exportedIndices_ ||
        current->selectedDirectoryPaths != exportedDirectoryPaths_) {
        discardExport();
        status_ = tr("Selection changed. Prepare the selected files again.");
        emit changed();
    }
}

void DragController::dismissError() {
    errorMessage_.clear();
    errorDetails_.clear();
    emit changed();
}

void DragController::showError(const ArchiveError& error) {
    errorMessage_ = error.code == ArchiveErrorCode::PasswordRequired ||
                    error.code == ArchiveErrorCode::WrongPassword
                        ? tr("The selected files require the correct archive password.")
                        : tr("The drag and drop operation could not be completed.");
    errorDetails_ = fromUtf8(error.message);
    if (!error.backendMessage.empty())
        errorDetails_ += QStringLiteral("\n") + fromUtf8(error.backendMessage);
    if (!error.path.empty())
        errorDetails_ += QStringLiteral("\n") + fromPath(error.path);
    emit changed();
}

void DragController::poll() {
    for (const auto& error : temporary_->takeCleanupErrors())
        showError(error);
    if (!active_)
        return;
    try {
        const auto id = *active_;
        const auto snapshot = tasks_.snapshot(id);
        if (!snapshot)
            return;
        observeTask(*snapshot);
        if (!terminal(snapshot->state) || !tasks_.release(id))
            return;
        active_.reset();
        const auto succeeded = snapshot->state == application::TaskState::Succeeded;
        if (!succeeded && phase_ != Phase::ProbeDrop) {
            if (!operation_->root.empty())
                temporary_->discard(operation_->root);
            discardExport();
            status_ = snapshot->state == application::TaskState::Cancelled
                          ? tr("Drag operation cancelled") : tr("Drag operation failed");
            if (snapshot->state == application::TaskState::Failed && snapshot->error)
                showError(*snapshot->error);
            operation_.reset();
            droppedUrls_.clear();
            emit changed();
            return;
        }
        switch (phase_) {
        case Phase::InspectDrop:
            if (operation_->directory)
                addDroppedInputs();
            else
                begin(service_.probe(operation_->source), Phase::ProbeDrop, tr("Detecting archive contents…"));
            break;
        case Phase::ProbeDrop:
            if (succeeded || (snapshot->error &&
                (snapshot->error->code == ArchiveErrorCode::PasswordRequired ||
                 snapshot->error->code == ArchiveErrorCode::WrongPassword ||
                 snapshot->error->code == ArchiveErrorCode::UnsupportedCompression))) {
                const auto path = fromPath(operation_->source);
                operation_.reset();
                droppedUrls_.clear();
                status_ = tr("Opening dropped archive…");
                emit changed();
                if (!archive_.busy() && !compression_.busy())
                    emit openRequested(path);
            } else if (snapshot->state == application::TaskState::Cancelled) {
                status_ = tr("Drag operation cancelled");
                operation_.reset();
                droppedUrls_.clear();
                emit changed();
            } else if (snapshot->error &&
                (snapshot->error->code == ArchiveErrorCode::InvalidArchive ||
                 snapshot->error->code == ArchiveErrorCode::UnsupportedFormat)) {
                addDroppedInputs();
            } else {
                if (snapshot->error)
                    showError(*snapshot->error);
                status_ = tr("Drag operation failed");
                operation_.reset();
                droppedUrls_.clear();
                emit changed();
            }
            break;
        case Phase::CreateRoot:
            startExtract();
            break;
        case Phase::Extract:
            startCollect();
            break;
        case Phase::Collect:
            finishExport(*snapshot);
            break;
        }
    } catch (const std::exception& error) {
        if (active_)
            tasks_.cancel(*active_);
        showError({ArchiveErrorCode::ResourceLimitExceeded,
                   "Could not update drag and drop task", error.what(), {}});
    }
}

} // namespace ananarchiver::qt
