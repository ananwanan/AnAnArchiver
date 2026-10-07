# Streaming creation and atomic output (M3)

`ArchiveWriter` is a Qt-free worker API. `CompressionRequest` contains selected
filesystem paths, destination, format, method, level, metadata preferences,
explicit replacement permission and finite scan/work limits. The Qt frontend
uses `ArchiveService::compressionCapabilities` and `ArchiveService::compress`;
only the libarchive adapter configures and owns libarchive handles.

## Capabilities and inputs

The adapter advertises only format/method combinations that its linked backend
can configure without external helper programs. The frontend derives format,
method and compression-level controls from that list. Read support never implies
write support. M3 exposes ZIP, supported 7z and TAR filter combinations; encryption,
parallel-worker settings and solid-mode controls are not exposed. Supplied creation
passwords fail explicitly before scanning or creating output.

Each selected file or directory is archived beneath its basename. Repeated
selections and descendants already covered by a selected directory are deduplicated.
Independent inputs producing the same archive path fail instead of silently losing
one input. Names pass the same portable path policy as extraction, so newly created
archives are safe to round-trip through the application.

Scanning runs in the compression worker, with entry/path/depth/metadata/byte/time
budgets and cancellation checks. The retained index contains metadata only;
payloads use a reused transfer buffer. Scanning has unknown totals and does not
count metadata sizes as bytes read. Writing reports bytes actually read from
inputs and measured metadata totals. Finalization remains cancellable and uses
indeterminate progress rather than promising an estimated codec finish time.

Directory enumeration uses platform-owned cursors (Windows FindFirst/FindNext,
POSIX fdopendir/readdir) and an iterative, depth-limited traversal stack. Native
handles close on success, failure and cancellation. Windows device prefixes stay
at IO boundaries; logical parents/relative paths use real filesystem components.
This avoids observed MinGW directory-iterator behavior that enumerated the process
working directory when entering a long directory path. Test expectations use
generated names and independent native fixture inspection, including payload
comparison, so that runtime defect cannot validate itself.

M3 rejects input symbolic links, Windows reparse points and special files rather
than following links or recursively archiving cycles. It rejects an output inside
a selected input directory, output aliases of inputs, and conflicting input
basenames. Native input handles check identity, size, timestamps and permissions
against the scan; detected source changes fail creation rather than silently
producing an inconsistent result. This detects common mutations, not a complete
filesystem snapshot or an adversarial change that restores all metadata.

## Output transaction

A native platform adapter owns an exclusively created private staging directory
on the destination filesystem and a temporary archive. It secures parent
components without following links. Payload writes and backend close errors are
checked; the completed temporary file is flushed and closed before installation.

Replacement requires explicit request permission. Installation performs one
atomic namespace operation; a failed or cancelled precommit operation leaves the
old destination intact. A no-replace installation also checks conflicts atomically,
so another process creating the destination during compression cannot be clobbered.
Normal failure/cancellation removes staging files. Cleanup failures return
`CleanupFailed` with a diagnostic location rather than hiding residual files.

Successful `create` means the archive is installed. Cancellation is checked
immediately before installation. A stop racing an already committed operation
cannot undo the install: `TaskManager` preserves a successful compression result
instead of reporting that installed output was cancelled. Postcommit cleanup or
durability errors remain diagnostic failures and may accompany an installed archive.
Abrupt process termination and power loss are outside this operation transaction;
staging files can remain and there is no crash-recovery journal in M3.

## Tasks and Qt

`TaskManager` owns creation workers just as it owns opening/extraction workers.
Compression snapshots include scanning/writing/finalizing phase, current file,
actual bytes, entry counts, optional totals, elapsed time and throughput. A bounded
snapshot is polled by the frontend; there is no signal or queued event per block.
Cancellation and application shutdown request stop and join the owned workers.
A blocked operating-system or codec call cannot be forcibly preempted by a stop
token, and backend codec allocations are not a hard process RSS limit.

`CompressionController` is a separate Qt adapter. The creation dialog supports
multiple files, repeated folder selections, destination, advertised format/method/
level options and explicit replacement. It starts a managed task without scanning
inputs on the GUI thread. The status area shows progress, cancellation and errors,
then provides an action to open a successfully created archive. Paths and backend
details are rendered as plain text; creation options do not persist secrets.
