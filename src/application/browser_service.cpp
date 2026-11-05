#include <algorithm>
#include <ananarchiver/application/browser_service.h>
#include <ananarchiver/libarchive_reader.h>
#include <ananarchiver/password.h>
#include <array>

namespace ananarchiver::application {
namespace {
ArchiveError stale() {
    return {
        ArchiveErrorCode::UnsupportedOperation, "Stale or incomplete browser generation", {}, {}};
}
ArchiveError limit() {
    return {ArchiveErrorCode::ResourceLimitExceeded, "Browser retirement limit reached", {}, {}};
}
struct PasswordGuard {
    std::string* value{};
    ~PasswordGuard() {
        if (value)
            scrubPassword(*value);
    }
};
struct Secret {
    std::string value;
    ~Secret() { scrubPassword(value); }
};
// Merge task, archive and view cancellation without retaining a callback into UI.
struct Stop {
    std::stop_source source;
    std::stop_callback<std::function<void()>> task;
    std::stop_callback<std::function<void()>> archive;
    std::stop_callback<std::function<void()>> view;
    Stop(CancellationToken a, CancellationToken b, CancellationToken c = {})
        : task(a, [this] { source.request_stop(); }), archive(b, [this] { source.request_stop(); }),
          view(c, [this] { source.request_stop(); }) {}
};
} // namespace
struct BrowserService::View {
    std::uint64_t id{};
    std::stop_source stop;
    CancellationToken taskStop;
    std::shared_ptr<const MetadataIndex> index;
    std::shared_ptr<const std::vector<std::uint64_t>> rows;
    std::uint64_t selectionId{};
    std::uint64_t readySelectionId{};
    std::shared_ptr<const MetadataSelection> selection;
    CancellationToken selectionStop;
};
struct BrowserService::State {
    std::uint64_t id{};
    std::filesystem::path path;
    std::stop_source stop;
    CancellationToken taskStop;
    std::shared_ptr<const MetadataIndex> index;
    std::shared_ptr<View> view;
};
struct BrowserService::Control {
    std::mutex mutex_;
    std::shared_ptr<State> current_;
    std::array<std::shared_ptr<void>, 32> retired_{};
    std::size_t retiredCount_{};
    std::uint64_t next_{1};
    void retire(std::shared_ptr<void> object) { retired_[retiredCount_++] = std::move(object); }
};
BrowserService::BrowserService(TaskManager& tasks, ReaderFactory factory)
    : tasks_(tasks), factory_(std::move(factory)), archives_(tasks, factory_),
      control_(std::make_shared<Control>()) {
    if (!factory_)
        factory_ = [] { return makeLibArchiveReader(); };
}
BrowserService::~BrowserService() {
    std::lock_guard lock(control_->mutex_);
    if (control_->current_)
        control_->current_->stop.request_stop();
}
void BrowserService::reclaim() {
    auto control = control_;
    std::array<std::shared_ptr<void>, 32> retired{};
    {
        std::lock_guard lock(control->mutex_);
        retired.swap(control->retired_);
        control->retiredCount_ = 0;
    }
}
bool BrowserService::valid(std::uint64_t archive, std::uint64_t query,
                           std::uint64_t selection) const {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    const auto& state = control->current_;
    return state && state->id == archive && !state->stop.stop_requested() &&
           !state->taskStop.stop_requested() &&
           (!query ||
            (state->view && state->view->id == query && !state->view->taskStop.stop_requested() &&
             (!selection || (state->view->selectionId == selection &&
                             !state->view->selectionStop.stop_requested()))));
}
bool BrowserService::close(std::uint64_t archive) {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    if (!control->current_ || control->current_->id != archive ||
        control->retiredCount_ >= control->retired_.size())
        return false;
    control->current_->stop.request_stop();
    control->retire(std::move(control->current_));
    return true;
}
bool BrowserService::contains(std::uint64_t archive) const {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    return control->current_ && control->current_->id == archive;
}
Result<BrowserSubmission> BrowserService::open(const std::filesystem::path& path,
                                               std::string password, MetadataLimits limits) {
    auto control = control_;
    const PasswordGuard guard{&password};
    auto secret = std::make_shared<Secret>();
    secret->value = std::move(password);
    std::lock_guard lock(control->mutex_);
    if (control->retiredCount_ >= control->retired_.size())
        return std::unexpected(limit());
    if (control->current_) {
        control->current_->stop.request_stop();
        control->retire(std::move(control->current_));
    }
    auto state = std::make_shared<State>();
    state->id = control->next_++;
    state->path = path;
    control->current_ = state;
    auto task = tasks_.start(
        TaskType::OpenArchive,
        [control, state, secret, limits, factory = factory_](TaskContext& context) -> Result<void> {
            Stop stop(context.cancellation(), state->stop.get_token());
            auto reader = factory();
            if (!reader)
                return std::unexpected(
                    ArchiveError{ArchiveErrorCode::BackendError, "Missing archive reader", {}, {}});
            auto info = secret->value.empty()
                            ? reader->open(state->path, stop.source.get_token())
                            : reader->open(state->path, secret->value, stop.source.get_token());
            scrubPassword(secret->value);
            if (!info)
                return std::unexpected(info.error());
            context.setArchiveInfo(*info);
            std::vector<MetadataBatch> batches;
            std::vector<ArchiveEntry> batch;
            batch.reserve(512);
            std::size_t count = 0, bytes = 0;
            std::optional<ArchiveError> failure;
            auto enumerated = reader->enumerate(
                [&](const ArchiveEntry& entry) {
                    if (stop.source.stop_requested())
                        return false;
                    const auto cost = sizeof(ArchiveEntry) + entry.path.size() + 1;
                    if (count >= limits.maxEntries || entry.path.size() > limits.maxPathBytes ||
                        cost > limits.maxIndexBytes - std::min(bytes, limits.maxIndexBytes)) {
                        failure = ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                               "Browser metadata input budget exceeded",
                                               {},
                                               {}};
                        return false;
                    }
                    bytes += cost;
                    batch.push_back(entry);
                    batch.back().index = count++;
                    if (batch.size() == 512) {
                        context.report(entry.path, 0, count, false);
                        batches.push_back(
                            std::make_shared<const std::vector<ArchiveEntry>>(std::move(batch)));
                        batch = {};
                        batch.reserve(512);
                    }
                    return true;
                },
                stop.source.get_token());
            if (failure)
                return std::unexpected(*failure);
            if (!enumerated)
                return enumerated;
            if (!batch.empty())
                batches.push_back(
                    std::make_shared<const std::vector<ArchiveEntry>>(std::move(batch)));
            auto index = MetadataIndex::build(std::move(batches), limits, stop.source.get_token());
            if (!index)
                return std::unexpected(index.error());
            auto result = std::make_shared<BrowserTaskResult>();
            result->archive = state->id;
            result->entryCount = (*index)->count();
            result->folderCount = (*index)->hierarchy().folders.size() - 1;
            result->indexBytes = (*index)->accountedBytes();
            {
                std::lock_guard guard(control->mutex_);
                if (control->current_ != state || stop.source.stop_requested())
                    return std::unexpected(stale());
                state->index = std::move(*index);
                state->taskStop = context.cancellation();
            }
            context.report({}, 0, count, false);
            context.publishBrowserResult(std::move(result));
            return {};
        });
    if (!task)
        return std::unexpected(task.error());
    return BrowserSubmission{*task, state->id, 0, 0};
}
Result<BrowserSubmission> BrowserService::query(std::uint64_t archive, MetadataQuery request) {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    if (!control->current_ || control->current_->id != archive || !control->current_->index ||
        control->current_->taskStop.stop_requested())
        return std::unexpected(stale());
    if (control->retiredCount_ >= control->retired_.size())
        return std::unexpected(limit());
    auto state = control->current_;
    if (state->view) {
        state->view->stop.request_stop();
        control->retire(std::move(state->view));
    }
    auto view = std::make_shared<View>();
    view->id = control->next_++;
    view->index = state->index;
    state->view = view;
    auto task = tasks_.start(
        TaskType::BrowseArchive,
        [control, state, view, request = std::move(request)](TaskContext& context) -> Result<void> {
            Stop stop(context.cancellation(), state->stop.get_token(), view->stop.get_token());
            auto rows = view->index->query(request, stop.source.get_token());
            if (!rows)
                return std::unexpected(rows.error());
            auto result = std::make_shared<BrowserTaskResult>();
            result->archive = state->id;
            result->query = view->id;
            result->page.total = rows->size();
            auto owned = std::make_shared<const std::vector<std::uint64_t>>(std::move(*rows));
            {
                std::lock_guard guard(control->mutex_);
                if (control->current_ != state || state->view != view ||
                    stop.source.stop_requested())
                    return std::unexpected(stale());
                view->rows = std::move(owned);
                view->taskStop = context.cancellation();
            }
            context.publishBrowserResult(std::move(result));
            return {};
        });
    if (!task)
        return std::unexpected(task.error());
    return BrowserSubmission{*task, state->id, view->id, 0};
}
Result<TaskId> BrowserService::page(std::uint64_t archive, std::uint64_t query,
                                    std::uint64_t offset, std::size_t rows, std::size_t bytes) {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    if (!control->current_ || control->current_->id != archive || !control->current_->view ||
        control->current_->view->id != query || !control->current_->view->rows ||
        control->current_->taskStop.stop_requested() ||
        control->current_->view->taskStop.stop_requested())
        return std::unexpected(stale());
    auto state = control->current_;
    auto view = state->view;
    return tasks_.start(
        TaskType::BrowseArchive,
        [state, view, offset, rows, bytes](TaskContext& context) -> Result<void> {
            Stop stop(context.cancellation(), state->stop.get_token(), view->stop.get_token());
            auto page =
                view->index->page(*view->rows, offset, rows, bytes, stop.source.get_token());
            if (!page)
                return std::unexpected(page.error());
            auto result = std::make_shared<BrowserTaskResult>();
            result->archive = state->id;
            result->query = view->id;
            result->page = std::move(*page);
            context.publishBrowserResult(std::move(result));
            return {};
        });
}
Result<TaskId> BrowserService::folderPage(std::uint64_t archive, std::string folder,
                                          std::uint64_t offset, std::size_t rows,
                                          std::size_t bytes) {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    if (!control->current_ || control->current_->id != archive || !control->current_->index ||
        control->current_->taskStop.stop_requested())
        return std::unexpected(stale());
    auto state = control->current_;
    return tasks_.start(TaskType::BrowseArchive,
                        [state, folder = std::move(folder), offset, rows,
                         bytes](TaskContext& context) -> Result<void> {
                            Stop stop(context.cancellation(), state->stop.get_token());
                            auto page = state->index->folderPage(folder, offset, rows, bytes,
                                                                 stop.source.get_token());
                            if (!page)
                                return std::unexpected(page.error());
                            auto result = std::make_shared<BrowserTaskResult>();
                            result->archive = state->id;
                            result->page = std::move(*page);
                            context.publishBrowserResult(std::move(result));
                            return {};
                        });
}

Result<BrowserSubmission> BrowserService::select(std::uint64_t archive, std::uint64_t query,
                                                 std::vector<MetadataRange> ranges, bool all) {
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    if (!control->current_ || control->current_->id != archive || !control->current_->view ||
        control->current_->view->id != query || !control->current_->view->rows ||
        control->current_->taskStop.stop_requested() ||
        control->current_->view->taskStop.stop_requested())
        return std::unexpected(stale());
    auto state = control->current_;
    auto view = state->view;
    auto id = control->next_++;
    view->selectionId = id;
    auto task = tasks_.start(
        TaskType::BrowseArchive,
        [control, state, view, id, ranges = std::move(ranges),
         all](TaskContext& context) mutable -> Result<void> {
            Stop stop(context.cancellation(), state->stop.get_token(), view->stop.get_token());
            if (all)
                ranges = {{0, view->rows->size()}};
            auto selection = view->index->resolve(*view->rows, ranges, stop.source.get_token());
            if (!selection)
                return std::unexpected(selection.error());
            auto owned = std::make_shared<const MetadataSelection>(std::move(*selection));
            auto result = std::make_shared<BrowserTaskResult>();
            result->archive = state->id;
            result->query = view->id;
            result->selection = id;
            std::shared_ptr<const MetadataSelection> previous;
            {
                std::lock_guard guard(control->mutex_);
                if (control->current_ != state || state->view != view || view->selectionId != id ||
                    stop.source.stop_requested())
                    return std::unexpected(stale());
                previous = std::exchange(view->selection, std::move(owned));
                view->selectionStop = context.cancellation();
                view->readySelectionId = id;
            }
            context.publishBrowserResult(std::move(result));
            return {};
        });
    if (!task)
        return std::unexpected(task.error());
    return BrowserSubmission{*task, archive, query, id};
}
Result<TaskId> BrowserService::extract(std::uint64_t archive, std::uint64_t query,
                                       std::uint64_t selection, ExtractionRequest request) {
    const PasswordGuard guard{request.password ? &*request.password : nullptr};
    auto control = control_;
    std::lock_guard lock(control->mutex_);
    if (!control->current_ || control->current_->id != archive || !control->current_->view ||
        control->current_->view->id != query || !control->current_->view->rows ||
        control->current_->view->selectionId != selection ||
        control->current_->view->readySelectionId != selection ||
        !control->current_->view->selection ||
        control->current_->view->selectionStop.stop_requested() ||
        control->current_->taskStop.stop_requested() ||
        control->current_->view->taskStop.stop_requested())
        return std::unexpected(stale());
    auto state = control->current_;
    auto view = state->view;
    auto selected = view->selection;
    return archives_.extract(
        state->path, std::move(request),
        [control, state, view, selection, selected](ExtractionRequest& target,
                                                    CancellationToken stop) -> Result<void> {
            {
                std::lock_guard guard(control->mutex_);
                if (control->current_ != state || state->view != view ||
                    view->selectionId != selection || state->taskStop.stop_requested() ||
                    view->taskStop.stop_requested() || view->selectionStop.stop_requested() ||
                    stop.stop_requested())
                    return std::unexpected(stale());
            }
            target.selectedIndices = selected->indices;
            target.selectedDirectoryPaths = selected->directories;
            return {};
        });
}
} // namespace ananarchiver::application
