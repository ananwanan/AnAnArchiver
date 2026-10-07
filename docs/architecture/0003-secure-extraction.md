# Secure streaming extraction (M2)

The public `ExtractionRequest` belongs to Core and contains destination,
selection, overwrite/link policy, password, resource limits and conflict
callbacks. The Qt frontend calls `ArchiveService::extract`; only the backend
adapter uses libarchive. Every task owns a reader, stop token and output
transaction. There is no GUI dependency in Core or Application.

## Paths and output ownership

Archive paths are UTF-8 relative paths, not authorized output filenames.
`secureArchivePath` rejects parent traversal, absolute/drive/UNC paths, control
characters, alternate data streams and Windows device/filename aliases.
Both slash styles are separators. Dot components are normalized; a root
directory entry is allowed, but an empty regular-file destination is not.
The portable filename policy intentionally rejects names that would be unsafe
or ambiguous on Windows even when running on a POSIX filesystem.

The platform output adapter walks directories without following links.
Windows holds ancestor handles without write/delete sharing and rejects all
reparse points, including junctions. POSIX uses directory descriptors and
`openat`/`fstatat` with no-follow checks. Output does not change process working
directory or umask and does not use `archive_write_disk`. Existing hardlinks
are replaced by moving directory entries; their target content is never opened
for truncation or written through.
POSIX resolves aliases in the explicitly selected root's parent before the
descriptor walk (including system temporary-directory aliases); the selected
root itself and all output ancestors inside it still reject links.

Data is streamed into an exclusively created staging directory under the
destination filesystem. Files are finished and closed before installation;
staging directories grant access only to their owner (protected Windows ACLs
and POSIX mode 0700). The staging namespace is reserved and cannot be named by
an archive entry or link target.
Original outputs are backed up before replacement. A rollback journal removes
new outputs and restores originals on error or cancellation. The staging
directory is task-owned, with explicit cleanup on normal paths and destruction
as an exception fallback. Cleanup failures must retain recoverable backups and
surface an error instead of deleting the last copy of an original file.
`CleanupFailed` takes precedence over normal cancellation at the task boundary,
so an accepted stop request cannot hide failed recovery or residual-file details.
This is an operation transaction, not a crash-recovery database: abrupt process
termination or power loss can leave staging/recovery files.

## Policies and bounds

Overwrite policies are Ask, Overwrite, Skip, Rename and OverwriteIfNewer.
Ask is resolved by a callback in the worker; Core never opens a dialog. Newer
comparison uses file timestamps and conservatively skips when the archive
timestamp is unavailable. Links use Safe, Preserve or Skip. Link targets are
checked relative to the extraction root; preserved links never authorize
subsequent payload writes through them. Unsupported special entries fail
explicitly. Windows symbolic links still require the operating system to allow
their creation.
When a directory conflicts with an existing regular file, Rename relocates its
whole subtree and internal links; Skip skips that subtree. This also works when
child entries precede the directory header. Incompatible file/directory types
are never destructively replaced by Overwrite.

Entry identities are zero-based enumeration ordinals, so duplicate pathnames
can be selected independently. Selecting a directory includes its descendants.
Selection in a streaming format can require another metadata pass; it never
requires retaining payloads in memory. Unknown totals remain indeterminate
during processing; the final snapshot settles to actual processed totals.

Finite request limits bound entries, pathname bytes, depth, per-entry/aggregate
logical output bytes, retained metadata and elapsed work. Actual data blocks
and sparse offsets are checked rather than trusting header size estimates.
Cancellation is checked at input/header/block boundaries and before output
installation. An in-flight OS call or codec call cannot be forcibly interrupted
by a C++ stop token; backend codec allocations also remain backend-owned.

Payload reads validate backend results, including CRC/authentication failures,
unexpected EOF and unsupported codecs. Listing remains a metadata operation
and is not a payload integrity test. Passwords stay within the operation,
are not logged or persisted, and owned buffers are overwritten on lifetime end.
Qt clears password input fields when dialogs close.
Extraction also drains unselected and policy-skipped payloads to check integrity
before committing outputs. Those entries count towards safety budgets. Byte
progress counts decoded data blocks of chosen entries, including overwrite
skips, and is distinct from sparse logical file size or total decoder work on
unselected entries.

## Tasks and frontend

The existing `TaskManager` also owns extraction workers. Byte counters represent
actual extraction work; indexing retains its distinct metadata size counters.
Snapshots expose speed, optional totals/ETA, elapsed time and the current entry.
Progress callbacks update a single snapshot; the frontend polls instead of
queuing one signal per block.

An overwrite prompt has a task ID and a monotonically increasing conflict ID.
The worker waits on a stop-aware condition variable. Responses to stale prompts
are rejected. Apply-to-all is held only within that task. Cancel and shutdown
wake pending prompts; the UI never waits for a worker to answer.

Qt provides virtualized multiselection, all/selected extraction, folder choice,
password input and overwrite/link controls. Extraction is available only after
indexing succeeds. A cancelled/failed index cannot authorize extraction from
an incomplete list. Paths and diagnostics are rendered as plain text.
