# Managed indexing and bounded metadata delivery

`TaskManager` owns each archive worker through `std::jthread`. Each operation
creates its own reader and libarchive handle. It keeps at most two active tasks
and sixteen retained task records; completed records are explicitly released.
There are no detached threads or GUI calls into libarchive.

`ArchiveService` batches 512 metadata entries. Each task queues at most eight
batches; a producer waits for queue space with a stop-aware condition variable.
The Qt controller periodically drains a small number of batches and uses one
model insertion notification per batch. The file view is virtualized. Metadata
count, pathname length, and aggregate index memory limits bound app allocations.

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
