#include "compression_controller.h"
#include "platform/archive_error_details.h"
#include "platform/path_conversion.h"
#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <utility>

namespace ananarchiver::qt {

CompressionController::CompressionController(application::WriterFactory factory, QObject* parent)
    : QObject(parent), service_(tasks_, {}, std::move(factory)), status_(tr("Ready")) {
    capabilities_ = service_.compressionCapabilities();
    const CompressionRequest defaults;
    for (const auto& format : capabilities_) {
        QVariantList methods;
        for (const auto& method : format.methods) {
            methods.push_back(QVariantMap{
                {QStringLiteral("label"),
                 QCoreApplication::translate("CompressionController", method.name.c_str())},
                {QStringLiteral("supportsLevel"),
                 method.minLevel.has_value() && method.maxLevel.has_value()},
                {QStringLiteral("minimumLevel"), method.minLevel.value_or(0)},
                {QStringLiteral("maximumLevel"), method.maxLevel.value_or(0)},
                {QStringLiteral("defaultLevel"), method.defaultLevel.value_or(0)}});
        }
        int defaultMethod = 0;
        if (format.format == defaults.format) {
            const auto found =
                std::find_if(format.methods.begin(), format.methods.end(),
                             [&](const auto& method) { return method.method == defaults.method; });
            if (found != format.methods.end())
                defaultMethod = static_cast<int>(std::distance(format.methods.begin(), found));
        }
        formats_.push_back(QVariantMap{{QStringLiteral("label"), fromUtf8(format.name)},
                                       {QStringLiteral("extension"), fromUtf8(format.extension)},
                                       {QStringLiteral("defaultMethod"), defaultMethod},
                                       {QStringLiteral("methods"), methods}});
    }
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, &CompressionController::poll);
    timer_.start();
}

CompressionController::~CompressionController() {
    if (active_)
        tasks_.cancel(*active_);
}

void CompressionController::addUrls(const QList<QUrl>& urls) {
    if (busy())
        return;
    QSet<QString> existing(inputs_.begin(), inputs_.end());
    bool added = false;
    for (const auto& url : urls) {
        if (!url.isLocalFile()) {
            showError({ArchiveErrorCode::UnsupportedOperation,
                       "Only local inputs can be compressed.",
                       {},
                       {}});
            continue;
        }
        const auto path = url.toLocalFile();
        if (!path.isEmpty() && !existing.contains(path)) {
            existing.insert(path);
            inputs_.push_back(path);
            added = true;
        }
    }
    if (added)
        emit inputsChanged();
}

void CompressionController::addInput(const QString& path) {
    if (busy() || path.trimmed().isEmpty() || inputs_.contains(path))
        return;
    inputs_.push_back(path);
    emit inputsChanged();
}

void CompressionController::removeInput(int index) {
    if (busy() || index < 0 || index >= inputs_.size())
        return;
    inputs_.removeAt(index);
    emit inputsChanged();
}

bool CompressionController::create(const QString& destination, int format, int method, int level,
                                   bool overwrite) {
    if (busy())
        return false;
    if (inputs_.isEmpty() || destination.trimmed().isEmpty()) {
        showError({ArchiveErrorCode::UnsupportedOperation,
                   "Select inputs and an output archive.",
                   {},
                   {}});
        return false;
    }
    if (format < 0 || static_cast<std::size_t>(format) >= capabilities_.size() || method < 0 ||
        static_cast<std::size_t>(method) >=
            capabilities_[static_cast<std::size_t>(format)].methods.size()) {
        showError({ArchiveErrorCode::UnsupportedCompression,
                   "Choose an available format and method.",
                   {},
                   {}});
        return false;
    }
    try {
        CompressionRequest request;
        request.destination = toPath(destination);
        if (!request.destination.is_absolute()) {
            showError({ArchiveErrorCode::UnsafePath,
                       "Choose an absolute output archive path.",
                       {},
                       request.destination});
            return false;
        }
        for (const auto& input : inputs_)
            request.inputPaths.push_back(toPath(input));
        const auto& capability = capabilities_[static_cast<std::size_t>(format)];
        request.format = capability.format;
        request.method = capability.methods[static_cast<std::size_t>(method)].method;
        request.level = level;
        request.overwriteExisting = overwrite;
        auto result = service_.compress(std::move(request));
        if (!result) {
            showError(result.error());
            return false;
        }
        active_ = *result;
        hasTask_ = true;
        succeeded_ = false;
        finalizing_ = false;
        destination_ = destination;
        status_ = tr("Scanning input files…");
        if (auto snapshot = tasks_.snapshot(*active_))
            emit taskObserved(*snapshot, destination);
        errorMessage_.clear();
        errorDetails_.clear();
        currentEntry_.clear();
        processedBytes_ = 0;
        totalBytes_ = -1;
        processedEntries_ = 0;
        bytesPerSecond_ = 0;
        elapsedMilliseconds_ = 0;
        remainingMilliseconds_ = -1;
        emit changed();
        return true;
    } catch (const std::bad_alloc&) {
        showError({ArchiveErrorCode::ResourceLimitExceeded, "Out of memory", {}, {}});
    } catch (const std::exception& error) {
        showError({ArchiveErrorCode::Unknown, error.what(), {}, {}});
    }
    return false;
}

void CompressionController::cancel() {
    if (active_ && tasks_.cancel(*active_)) {
        status_ = tr("Cancelling…");
        emit changed();
    }
}

void CompressionController::cancelTask(application::TaskId id) {
    if (active_ && *active_ == id)
        cancel();
}

void CompressionController::dismissError() {
    errorMessage_.clear();
    errorDetails_.clear();
    emit changed();
}

void CompressionController::dismissProgress() {
    if (!busy()) {
        hasTask_ = false;
        emit changed();
    }
}

void CompressionController::showError(const ArchiveError& error) {
    switch (error.code) {
    case ArchiveErrorCode::FileNotFound:
        errorMessage_ = tr("An input file could not be found.");
        break;
    case ArchiveErrorCode::PermissionDenied:
        errorMessage_ = tr("Permission to access a file was denied.");
        break;
    case ArchiveErrorCode::UnsupportedFormat:
        errorMessage_ = tr("This archive format is not supported.");
        break;
    case ArchiveErrorCode::UnsupportedCompression:
        errorMessage_ = tr("This compression method or level is not supported.");
        break;
    case ArchiveErrorCode::UnsupportedOperation:
        errorMessage_ = tr("This operation is not supported.");
        break;
    case ArchiveErrorCode::DiskFull:
        errorMessage_ = tr("There is not enough free disk space.");
        break;
    case ArchiveErrorCode::OutputExists:
        errorMessage_ =
            tr("The output already exists. Enable replacement or choose a different path.");
        break;
    case ArchiveErrorCode::Cancelled:
        errorMessage_ = tr("The operation was cancelled.");
        break;
    case ArchiveErrorCode::CleanupFailed:
        errorMessage_ = tr("Archive creation cleanup failed. See the details for remaining files.");
        break;
    case ArchiveErrorCode::ResourceLimitExceeded:
        errorMessage_ = tr("The operation exceeded a safety limit.");
        break;
    case ArchiveErrorCode::InvalidPathEncoding:
        errorMessage_ = tr("A filename contains invalid text encoding.");
        break;
    case ArchiveErrorCode::UnsafePath:
        errorMessage_ = tr("An input or output path is unsafe.");
        break;
    case ArchiveErrorCode::IoError:
        errorMessage_ = tr("A file could not be read or written.");
        break;
    default:
        errorMessage_ = tr("Archive creation failed.");
        break;
    }
    errorDetails_ = formatArchiveErrorDetails(error);
    emit changed();
}

void CompressionController::poll() {
    if (!active_)
        return;
    try {
        const auto id = *active_;
        const auto snapshot = tasks_.snapshot(id);
        if (!snapshot)
            return;
        emit taskObserved(*snapshot, destination_);
        currentEntry_ = fromUtf8(snapshot->currentFile);
        processedBytes_ = static_cast<double>(snapshot->processedBytes);
        totalBytes_ = snapshot->totalBytes ? static_cast<double>(*snapshot->totalBytes) : -1;
        processedEntries_ = static_cast<double>(snapshot->processedEntries);
        bytesPerSecond_ = snapshot->bytesPerSecond;
        elapsedMilliseconds_ = snapshot->elapsed.count();
        remainingMilliseconds_ =
            snapshot->estimatedRemaining ? snapshot->estimatedRemaining->count() : -1;
        finalizing_ = snapshot->compressionPhase == CompressionPhase::Finalizing;
        using application::TaskState;
        switch (snapshot->state) {
        case TaskState::Pending:
        case TaskState::Running:
            status_ = finalizing_ ? tr("Finishing archive…")
                      : snapshot->compressionPhase == CompressionPhase::Writing
                          ? tr("Creating archive…")
                          : tr("Scanning input files…");
            break;
        case TaskState::Cancelling:
            status_ = tr("Cancelling…");
            break;
        case TaskState::Cancelled:
            status_ = tr("Archive creation cancelled");
            break;
        case TaskState::Succeeded:
            status_ = tr("Archive created");
            break;
        case TaskState::Failed:
            status_ = tr("Archive creation failed");
            break;
        }
        const auto terminal = snapshot->state == TaskState::Succeeded ||
                              snapshot->state == TaskState::Cancelled ||
                              snapshot->state == TaskState::Failed;
        if (terminal && tasks_.release(id)) {
            active_.reset();
            succeeded_ = snapshot->state == TaskState::Succeeded;
            finalizing_ = false;
            if (snapshot->error && snapshot->state == TaskState::Failed)
                showError(*snapshot->error);
            emit changed();
            emit operationFinished(succeeded_);
            return;
        }
        emit changed();
    } catch (const std::bad_alloc&) {
        tasks_.cancel(*active_);
        showError({ArchiveErrorCode::ResourceLimitExceeded, "Out of memory", {}, {}});
    } catch (const std::exception& error) {
        tasks_.cancel(*active_);
        showError({ArchiveErrorCode::Unknown, error.what(), {}, {}});
    }
}

} // namespace ananarchiver::qt
