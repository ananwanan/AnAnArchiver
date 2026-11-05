# Managed indexing and bounded metadata delivery

> Frontend update (2026-11-05): existing C++ task behavior below remains the
> baseline contract. `main` targets Flutter through a planned C ABI; see
> [ADR 0007](0007-flutter-frontend.md). Qt adapter behavior is migration evidence,
> not a completed Flutter implementation.

`TaskManager` owns each archive worker through `std::jthread`. Each operation
creates its own reader and libarchive handle. It keeps at most two active tasks
and sixteen retained task records; completed records are explicitly released.
There are no detached threads or GUI calls into libarchive.

`ArchiveService` batches 512 metadata entries. Each task queues at most eight
batches; a producer waits for queue space with a stop-aware condition variable.
The existing Qt controller drains bounded batches with one model notification
per batch. The Flutter binding must preserve backpressure while moving drained
metadata into the shared C++ index and exposing bounded query pages, not a
complete duplicate Dart index. Virtualized views and finite caches are required.
Metadata count, pathname length and aggregate index memory limits remain active.

Cancelling a task requests its stop token and wakes a waiting producer. The
adapter checks cancellation between headers and during input reads. Cancelled
and failed tasks discard queued metadata. Entries already displayed can remain
visible with an explicit incomplete-list status. Closing or opening another
archive retires the previous task; a generation/task identity prevents old
results from entering the new model. Shutdown requests all cancellations before
joining workers so a full metadata queue cannot deadlock application exit.

Filesystem and codec calls already in flight may delay cancellation. A stop token
cannot interrupt a blocked operating system read or an individual backend codec
call. No pause control is exposed because the backend cannot guarantee pause.

During indexing the total number of entries is unknown. The frontend displays
indeterminate progress, the current pathname, metadata count, and elapsed time.
The sum of entry sizes is not decompression progress or IO throughput; speed and
ETA remain unknown instead of deriving misleading numbers from those sizes.

Errors use `std::expected<T, ArchiveError>` with a machine-readable code, source
path, and backend detail. libarchive warnings have a distinct error code and are
surfaced rather than silently discarded. Worker exceptions are contained at
the task boundary. Archive opening never logs passwords or stores file data.

## C ABI and Flutter delivery requirements

Native task submission returns an ID promptly; archive IO, index/query work and
cleanup remain on managed workers. Snapshot polling or asynchronous callbacks
must be bounded and coalesced. Result batches have explicit release functions;
callback disposal and app shutdown cannot free state while workers still use it.
Closing an archive, disposing a view or changing a query invalidates generations.
New C ABI/binding tests must cover queue backpressure, late results, cancel,
conflict responses and shutdown without waiting on the UI isolate.
