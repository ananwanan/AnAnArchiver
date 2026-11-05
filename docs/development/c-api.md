# C ABI v1

`ananarchiver_c_api` is a shared library over C++ Application services. The public
header is `include/ananarchiver/c_api/ananarchiver.h`. It has no Qt, Flutter, Dart,
STL or libarchive types. F1 implements native task transport; F2 adds the native
authoritative browser index, generation-checked queries/pages and selections. There is no
Flutter app or Dart binding yet.

## Build and validation

The existing Core presets build the library by default, without a GUI SDK:

```sh
cmake -P scripts/init-dependencies.cmake
cmake --preset core-release
cmake --build --preset core-release
ctest --preset core-release
```

Dependency initialization resolves current stable sources. An existing verified
manifest can instead replay one recorded resolution; do not describe a replay as
a new latest-version check. Native backend/codecs use the existing superbuild,
PIC and provenance pipeline. Set `ANANARCHIVER_BUILD_C_API=OFF` to omit this library;
the superbuild forwards that option. `ANANARCHIVER_BUILD_QT=OFF` avoids Qt discovery.
The build also installs the shared library, import library where applicable,
header and existing provenance records; complete runtime/license packaging remains
F6. Windows exports only marked C
functions; ELF/Mach-O export lists hide bundled C++/backend/codec symbols.

On Windows, execute commands from an MSVC developer shell or through
`scripts/Invoke-Msvc.ps1`. The tests `c_api_consumer` and `c_api` cover a C11
consumer and real archive operations. The C consumer links only this shared
library; C++ integration fixtures additionally link the backend to generate
archives. Windows x64 Release is verified; Linux/macOS native verification and
Dart binding verification remain outstanding.

## ABI and ownership

Call `aa_get_api_info` with `struct_size = sizeof(aa_api_info)`, then pass its
supported `abi_version` to `aa_engine_create`. An incompatible version returns
`AA_ABI_MISMATCH` without creating an engine. ABI v1 uses native C alignment,
explicit `__cdecl` on Windows, fixed-width integer fields and length-delimited
UTF-8 strings. Do not pack structures. Input structures accept the v1 prefix or
a larger structure with an ignored suffix; a shorter prefix is rejected.
Output `struct_size` identifies the layout available from the provider.

An engine is an opaque owned pointer. Task IDs belong to that engine; never pass
one engine's IDs to another. After `aa_task_release`, its ID immediately returns
`AA_NOT_FOUND`. An engine pointer is invalid after destroy. As with other C APIs,
every non-null caller pointer must address valid, suitably aligned storage;
arbitrary dangling pointers cannot be validated. Destroy requires exclusive
ownership. Other engine operations are serialized safely across callers.

All input strings and arrays are borrowed only during submission. The library
copies them before returning a task ID. `aa_string.size` counts UTF-8 bytes, not
characters; no trailing NUL is required. Malformed UTF-8, embedded NUL, invalid
flags, short structures and invalid pointer/count pairs fail before IO starts.
Numeric `AA_*` constants are independent of internal C++ enum ordinals.

Each returned snapshot, metadata batch, capability batch or standalone error is
provider-owned and immutable. Use its matching `aa_*_release` function exactly
once; never use `free`, Dart's allocator or another CRT. Null release is safe.
Do not release a snapshot's nested error separately. String views and nested
rows last as long as their owning result, even if the task or engine is released.
Release an old result before reusing its output slot. Outputs are reset on
failure. Always inspect `aa_status`: low memory may prevent allocating an optional
error, so a null error does not mean success. Unexpected C++ exceptions become
`AA_INTERNAL_ERROR`; allocation failure becomes `AA_RESOURCE_LIMIT`.

Submission success means a task was accepted. IO/backend errors arrive later in
the snapshot as a terminal failed state and structured error with code, message,
backend detail and path. Diagnostics are plain UTF-8 text, bounded to 64 KiB each;
invalid backend bytes use a diagnostic replacement. Display paths and diagnostics
can be shortened; never use them as an authorization or extraction destination.
Raw entry paths remain complete metadata and are not authorized output paths.

## Tasks and bounded transport

`aa_archive_open` submits content detection plus streaming enumeration, or
content detection alone with `probe_only = 1`. Probing does not accept a password.
Opening is not an integrity test. Metadata retains original zero-based archive
ordinals, including duplicate names; it does not read all payloads into RAM.

`aa_task_poll` copies one task snapshot. Flags indicate whether totals, ETA,
archive info or a conflict exist. Unknown totals remain unknown; enumeration
does not invent IO throughput. All snapshots are independent; poll at a bounded
frontend cadence (for example, 50–100 ms while active), release old snapshots,
and stop polling when disposed. No callbacks retain frontend state.

`aa_task_take_entries` transfers at most 512 entries. A zero count means no batch
is currently queued. Continue draining through a zero batch after terminal
success: a terminal snapshot can precede the last consumption. A failure/cancel
invalidates any partially consumed index. The producer queue holds at most eight
batches per task, plus one pending producer batch; a full queue applies
backpressure. Entry paths are capped at 64 KiB by the existing reader, giving a
finite count and byte bound. There are at most two active tasks and sixteen
retained tasks per engine. Returned results remain caller-owned until release;
callers must not accumulate them without limit.

F1 request arrays contain at most 4,096 items each. Each string is at most
64 KiB, and compression paths or selected directory paths together are at most
4 MiB per request. Existing backend metadata, nesting, payload and time limits
remain in force; ABI v1 cannot silently relax them. Large selections and current-folder
pages use F2's native browser/selection representation.
Do not accumulate the metadata stream into a full Dart-side archive index.

## Operations and conflicts

`aa_archive_extract` invokes the existing safe extraction service. `extract_all`
must be explicitly set to 1 for the whole archive and cannot carry a selection.
With 0, an empty selection extracts nothing. File selections use original
ordinals; directory selections use validated path-component boundaries and
include implicit folders. Extraction retains traversal/link defenses, rollback,
overwrite policies and resource limits.

An `AA_HAS_CONFLICT` snapshot owns the conflict entry, display destination and
`conflict_id`. Submit a final overwrite decision through
`aa_task_resolve_conflict`; stale IDs/repeated answers return `AA_BUSY`. `Ask` is
not a final answer. `apply_to_all` applies only within that task. Cancellation or
shutdown wakes workers waiting for a response. Passwords are copied for the
operation and scrubbed on completion, cancellation and exceptional submission
paths; the caller must also clear its own input. They are not stored in settings
or logs.

`aa_compression_capabilities` returns actual backend format/method pairs and
supported levels/options. `aa_archive_compress` uses those values and the existing
atomic writer. Unsupported combinations fail with backend errors. The current
writer does not support password creation; this request has no password field.
There is no fake pause, integrity-test or archive-edit operation in ABI v1.

## Cancellation and shutdown

`aa_task_cancel` requests cancellation without waiting. Terminal tasks return
`AA_BUSY`; unknown/released IDs return `AA_NOT_FOUND`. The operation may still be
unwinding, so poll its terminal result if needed. A successfully committed writer
retains success even if a stop races its final commit.

`aa_task_release` invalidates the ID immediately and schedules cancellation,
metadata discard and final join on the engine's owned cleanup worker. It can
release an active task; no UI-thread join is performed. A new submission may
temporarily hit the task limit until reclamation finishes.

`aa_engine_shutdown` is idempotent and nonblocking. It rejects further operations
with `AA_SHUTTING_DOWN`; the owned worker destroys the services, requesting all
task stops before joining any, including producers blocked on backpressure and
workers waiting for conflicts. `aa_engine_is_stopped` reports cleanup completion.
The engine and DLL must stay alive during this process.

Finally call `aa_engine_destroy` with exclusive ownership **on a background
thread/isolate**: it requests shutdown if needed and joins/frees the cleanup
worker. Even after polling stopped, keep this final join off the UI isolate.
Result-buffer deallocation and large request preparation should also run on the
binding worker. Do not unload the library until every engine and result has been
released. No worker is detached, and results do not borrow service memory.

The C11 smoke consumer in `tests/c_api/c_consumer.c` is a minimal ownership and
version-negotiation example; `tests/c_api/c_api_tests.cpp` demonstrates complete
real archive tasks, conflict responses and shutdown.

## F2 native browser extension (ABI v1 compatible)

`AA_FEATURE_BROWSER` advertises additive functions/structures; all F1 layouts,
constants and exports remain compatible. Negotiate this feature before generating
a Dart binding. See [shared metadata browsing](metadata-browsing.md) for exact
Unicode, directory, memory and generation semantics, and
[F2 validation](../validation/2026-11-06-metadata-browsing.md) for evidence.

1. `aa_browser_open` takes a non-probe `aa_open_request` and returns an archive
   generation plus task ID. Poll the task and retrieve the owned summary with
   `aa_browser_task_result`. No full metadata stream is emitted by this operation.
2. `aa_browser_query` submits folder/search/sort and returns the new query ID
   immediately. It invalidates its predecessor and selections. After success,
   retrieve total rows; keep the full native query in C++.
3. `aa_browser_request_page` submits offset/row/byte limits against those IDs.
   Its successful task result is a batch of complete row records and strings.
   `aa_browser_request_folders` independently loads direct folders for the tree
   without invalidating list selection. The page limit is 512 rows and 1 MiB,
   including a conservative record/header allowance and complete UTF-8 strings.
4. `aa_browser_select` submits at most 4,096 nonoverlapping position ranges or
   select-all for the current query. It returns a selection handle, with resolved
   metadata retained natively after its task succeeds. Each new selection replaces
   its predecessor. `aa_browser_extract` accepts the native selection plus regular
   extraction options, reusing the existing worker and safe backend pipeline.
5. `aa_browser_close` invalidates the archive and schedules index reclamation.
   Release every owned result with `aa_browser_result_release` and every task with
   `aa_task_release`; closing an archive does not release task IDs automatically.

All input strings/arrays are copied during submission. Browser result strings
last until that result is released, including after close/shutdown. Results never
borrow the native full index. `AA_STALE_VIEW` rejects old archive/query IDs;
pending tasks report `AA_BUSY`, failed/cancelled tasks `AA_ARCHIVE_ERROR` (inspect
the ordinary task snapshot for the structured error). A successful non-browser
task has no browser result and returns `AA_INVALID_ARGUMENT`. Output slots clear
on failure. Invalid native selections fail explicitly, never become extract-all.
F1 `aa_archive_open` streaming remains available for native compatibility; Flutter
browsing should use `aa_browser_open` and bounded windows instead.
