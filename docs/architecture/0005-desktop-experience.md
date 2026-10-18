# Desktop experience (M4)

The Qt frontend continues to depend on the Application API. Content probing is
an `ArchiveService` task: it opens a reader on a managed worker without enumerating
payloads or exposing backend handles. Every operation retains its own reader.

## Metadata views

The entry model stores immutable batches of Core metadata. A managed query worker
shares those batches, caches case-folded Unicode keys, and produces a row-position
view. Numeric sizes, enum types and timestamps are compared as values; missing
sizes/timestamps sort last. Ties preserve archive order. The view never changes
the original enumeration index used by extraction, including duplicate paths.

Queries and sort changes cancel obsolete generations and clear selection. Metadata
appends coalesce the next snapshot without repeatedly cancelling an in-flight
query; partial views can advance during indexing. A stale archive generation or
older source snapshot cannot replace a newer view. Selection is disabled until
the latest view is ready, and select-all covers only visible rows. Extract-all
retains complete archive totals. List rows remain virtualized and batches do not
produce one event per entry.

## Preferences and task history

`DesktopController` owns the in-memory theme and a twenty-path recent list. Loading
does not stat recent files, which may reside on unavailable drives. Successful
indexing and creation add records; reopening uses the normal asynchronous error
path. A dedicated persistence worker owns its QSettings instance and coalesces
pending writes. It reports the latest revision's save errors and flushes the last
snapshot on shutdown. Passwords are never part of preferences or history.

The task-center model observes actual Application snapshots from the archive,
compression and drag controllers. Source-prefixed ids avoid collisions between
their managers. Cancellation routes back to the original owner. Its session
history keeps sixty-four records and never evicts active tasks. Opening stays
active until all queued metadata has been drained, even if its backend has
finished. Unknown totals/finalization do not acquire invented byte progress,
speed or ETA. There is no pause action.

## Drag transfer

Incoming file URLs are checked as a complete batch before accepting inputs.
Single-file content classification, filesystem inspection and directory work run
on workers. Password-required content follows the archive opening path. Ordinary
files and directories populate the existing creation dialog.

Outgoing selection is captured as original entry indices. A managed task creates
a private temporary root; the existing safe extraction service prepares files
with link skipping and finite byte/time limits; another worker lists immediate
children and builds bounded file URLs. There is no synchronous extraction inside
a mouse handler. An explicit subsequent held-mouse gesture starts QDrag with
copy-only text/uri-list MIME data. A released-button activation only explains the
gesture, avoiding Windows native drag waiting for a later mouse event.

The temporary manager caps owned roots, reserves creation slots and runs deletion
on a worker. Active native drag holds its source root; after return the source is
retained for ten minutes so asynchronous desktop copies have a grace period.
Expiry callbacks use generations so an old drag cannot expire a later one.
Cleanup and holding a root are synchronized; a root already being removed cannot
be offered for a new drag. Failed removal retries finitely and reports its path,
with a final shutdown attempt. A long copy can exceed the grace period, and abrupt
termination can leave temporary files; this is not a filesystem transaction or a
crash-recovery journal.

Native adapter work remains under `frontend/qt/platform`. UI text, file paths and
backend details are plain text. The icon set is original SVG artwork with its
license alongside the assets. Existing Core security policies are unchanged.

## Product name and component provenance

The product is 轻匣 in Chinese and LiteArc in English. Window titles, About/help
content and localized system-integration labels use the selected display name.
Existing API, QML, settings and application identities remain stable for
compatibility; public executable and distribution names use LiteArc.

The open-source components dialog presents the application version and available
application Git revision, plus each component's actual build version, available
Git revision and source repository. Build-generated metadata is derived from
the verified `dependency-build.json` and `build-trace-config.json` inputs shared
with the final provenance record; current dependency checkout revisions and
example versions must not be substituted for the recorded build. Missing source
metadata is explicitly unavailable. License and upstream links remain accessible.
