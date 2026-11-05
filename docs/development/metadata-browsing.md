# Shared metadata browsing (F2)

The authoritative browser implementation is C++ Application, independent of Qt,
Flutter and Dart. `MetadataIndex` builds immutable metadata/hierarchy snapshots;
`BrowserService` schedules archive indexing, queries, windows and selections on
`TaskManager`. Qt models adapt the same index/query/selection methods. Real
filesystem trees continue to use `DirectoryService` and `FileTreeModel`.
F2 did not implement a Flutter app. F3 now consumes these APIs through the
[Flutter frontend and binding](flutter.md).

## Directory and Unicode rules

An empty folder path denotes the archive root. Normalize archive separators,
remove empty and `.` components for grouping, synthesize implicit parents, retain
explicit empty directories, and deduplicate folder rows. Original entry paths
and enumeration ordinals are retained, including duplicate filenames. A folder
uses a distinct identity namespace; its metadata index field has no backend
meaning. Directory paths are revalidated with `secureArchivePath` before selected
extraction; the existing backend matches descendants at component boundaries.
Display normalization is never extraction authorization.

Scoped queries contain direct children only, with folders first. Folder names
are ordered case insensitively, with original UTF-16 names breaking ties. Only
name-descending reverses folders; other sorts retain their folder-name order.
Files preserve source order when unsorted. Name, numeric size, entry type and
exact timestamp sorting retain source order for ties. Unknown sizes/timestamps
stay last in both directions. The legacy standalone flat Qt adapter retains its
old path-search/type-order behavior through the same query implementation.

Search uses locale-independent Unicode **simple** default folding (C + S), as
`QString::toCaseFolded` actually does in the retained Qt version. Comparison keys
are UTF-16, including supplementary characters. There is no NFC/NFD normalization,
locale collation, Turkic override or full-fold expansion: `ß` does not become
`ss`, and dotted capital I does not become `i` plus combining dot. Chinese,
Japanese, Korean, emoji, accented text, Greek, sharp S and supplementary case
pairs have regression coverage, including direct Qt parity checks.

`scripts/generate-case-fold.py` resolves the final UCD at Unicode's official
latest-data endpoint, rejects a non-final ReadMe, and records version, URL,
SHA-256 and UTC retrieval time in `case_fold_data.inc`. The reviewed generated
table is compiled into Application; builds do not download data or require Qt.
The resolved data is Unicode 18.0.0, hash
`a004797658a457bec4dc11683e39f69249ea3b595b752dbea6721c4c9f587b0d`.
The Unicode-3.0 license is included and installed. Regeneration is an explicit
data update, not a build-time dependency resolution or an indefinite version pin.

## Generations, tasks and selection

There is one current native archive generation and one current list-query
generation per engine. These are engine-scoped nonzero numeric handles, not
pointers. Starting another archive invalidates the old archive and stops its
index/query workers. A directory/search/sort change submits a new query and
immediately invalidates its predecessor and selections. Replacement failure or
cancellation does not restore an old view. A failed index never publishes a
partial browser index. Query/page/select results cannot authorize operations
against a different generation.

A select request consists of sorted, nonoverlapping `(first, count)` row ranges,
or select-all for the entire current filtered query. It is not limited to the
currently fetched page. The worker resolves original file ordinals and validated
directory subtrees, retaining the selection in C++; only a selection handle
crosses the ABI. Each new selection invalidates the previous selection handle.
A pending/failed selection cannot use an older selection's data. Selected
extraction checks archive/query/selection again when its worker starts and then
uses the existing safe extraction pipeline. Extract-all remains the existing
explicit `aa_archive_extract` operation and does not inherit a visible filter.

`aa_browser_request_folders` returns an independent direct-folder window for a
lazy tree. It does not replace the list query or clear its selection. Its result
has query zero and cannot authorize a list selection. Navigation uses the
returned folder path; callers derive the parent by removing its final component.

The Qt adapter coalesces requests/results, uses an owned `jthread`, and never
implements its own search or sort. Directory snapshots that have not structurally
changed are retained without resetting the tree. Large selection resolution for
extraction and drag preparation is deferred through `SelectionResolver` into the
Application worker. The UI stores row ranges; select-all is one interval. Resolver
closures hold weak source references and reject disposed/changed generations.
Legacy synchronous inspection helpers are for tests/off-UI callers; production
controllers use the deferred resolver. Closing a view schedules native snapshot
reclamation on the worker; final model teardown still joins its owned worker.

## Resource and lifetime contract

Default per-index limits are:

| Resource | Limit |
| --- | ---: |
| Source metadata entries | 2,000,000 |
| Folder nodes including root | 1,000,000 |
| Path bytes | 64 KiB |
| Path depth | 256 components |
| Cumulative folder text allowance | 128 MiB |
| Conservative index allocation accounting | 512 MiB |
| Rows per result page | 512 |
| Bytes per page including record/string allowance | 1 MiB |
| Input selection ranges through C ABI | 4,096 |

Index accounting includes retained entry batches/paths, Unicode keys, item
storage, folder/hash nodes, spare vector capacity and allocator allowances. It
is a conservative allocation budget, not an OS RSS promise. Temporary normalized
paths and a single folding operation are bounded by the pathname limit. Workers
check budgets before persistent growth. Source enumeration also checks count,
path and input-memory limits before accumulating its batches.

Query row IDs and native selection arrays are bounded by source/folder counts;
they are never transferred wholesale to Dart. Query row storage is at most
`(entries + folders) * 8` bytes, plus bounded sorting stack. Selection ordinals
are at most `entries * 8`; directory text is bounded by the hierarchy allowance.
An old/new query may overlap during cancellation. At most two tasks are active
and sixteen retained per engine, including page tasks. There are at most 32
retired source/view references awaiting the cleaner. These are finite per-engine
limits, not a single shared process-memory budget across multiple engines.

The page byte allowance includes 128 bytes for the result header, 160 bytes per
row (at least the C record size), and full path/name UTF-8 bytes. A page that
cannot fit its first row fails explicitly; a caller must increase its byte
budget within the 1 MiB ceiling. Offset equal to total returns an empty page;
offset beyond total is an error. Never retry an undersized page forever without
checking the task error. Result buffers must also be released at a bounded rate.

`aa_task_release` immediately invalidates a task ID and delegates cancellation,
result disposal and join to the engine cleaner. Archive close invalidates the
archive/query handles and queues index retirement without joining on the caller.
Provider-owned page strings remain valid until their matching result release,
even after task release, archive close or engine destruction. Results do not
borrow index memory. Cancellation is checked during indexing, comparisons,
selection resolution and page creation. Shutdown cancels tasks before joining;
`aa_engine_destroy` remains a final off-UI operation. Release bounded results and
prepare/copy input arrays on the binding worker, as in F1.

Archive extraction reopens the source path, as it did before F2; an archive
handle is a metadata-generation identity, not a filesystem snapshot or a held
copy of file contents. Reopen externally modified archives before selecting.
The service does not cache payloads or replace backend extraction validation.

## Verification and next stage

Build the existing Core presets without GUI SDKs. `metadata` and `browser_c_api`
add public-service and C ABI regression suites; affected Qt tests exercise the
same methods. The metadata test executable accepts `100000` or `1000000` to
generate data in memory, verify all windows and original identities, resolve a
native large selection and report timings/accounting. It writes no large data.
See [F2 validation](../validation/2026-11-06-metadata-browsing.md) for commands,
actual memory/timing measurements and platform limits.

F3 implements the Flutter/Dart binding and browser UI using the native open/query,
lazy tree, bounded page and selection contracts. Windows measurements do not
establish Linux/macOS, sanitizer, Dart-isolate or Flutter-rendering acceptance.
