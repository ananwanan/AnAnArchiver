# Flutter frontend decision and migration contract

- Decision date: 2026-11-05.
- Status: accepted; F1 C ABI and F2 native metadata browsing implemented; F3–F6 pending.
- Development branch: `main`.
- Preserved Qt branch: [`codex/qt-baseline-2026-11-05`](https://github.com/ananwanan/AnAnArchiver/tree/codex/qt-baseline-2026-11-05).
- Qt baseline commit: `53185742ad1e640b24da397cfa3544507af4edef`.

## Decision and current state

Flutter/Dart replaces Qt Quick/QML as the frontend development direction on
`main`. This supersedes the previous Qt-default policy in AGENTS.md and the
frontend choice in ADR 0001. C++23, CMake, the Application/Core boundaries and
official libarchive remain the archive implementation.

The initial decision changed requirements and documentation first. F1 now adds
`ananarchiver_c_api` over the existing Application services, with a pure C
consumer and lifecycle/operation tests. There is currently no Flutter app or
Dart binding; F2 now supplies shared native metadata browsing. Existing
`frontend/qt`, desktop presets, packaging scripts and the manual Core/Qt CI
workflow remain operational baseline code until their migration stages are
implemented. Old Qt validation does not establish Flutter completion.

The preserved branch points to the complete pre-decision source snapshot. It is
the reference for the existing Qt behavior; Flutter development stays on `main`.
Do not force-update that baseline branch to a Flutter revision. Core fixes and
any necessary legacy maintenance require explicit scope and their own evidence.

## Dependency boundary

```text
Flutter widgets
  -> Dart Controller / ViewModel
  -> Dart FFI binding
  -> ananarchiver_c_api
  -> C++ Application services / TaskManager / metadata query service
  -> Archive Core / platform archive IO / libarchive adapter
  -> libarchive/libarchive
```

Core, Application and C ABI must build and test without Qt, Flutter or a Dart
runtime. Dart and platform plugins never receive libarchive handles. Platform
dialogs, window events and native file dragging belong to frontend/platform
adapters. They invoke the same Application operations as the regular UI.

## Stable C ABI

Public C headers are under `include/ananarchiver/c_api`, implementation under
`src/c_api`, with the `ananarchiver_c_api` shared CMake target. ABI v1 and its
current evidence/limits are documented in [C ABI development](../development/c-api.md).
Generate the Dart binding from that C interface
with `ffigen` or an equivalent reviewed generator.

F1 exposes engine-scoped task IDs and bounded metadata streams. F2 adds archive/query
handles and generation-aware current-folder pages; the stream is
not permission to duplicate the complete metadata index in Dart. Cleanup is
owned by an engine worker. Final engine destruction is explicitly blocking and
must run off the UI isolate. Existing backend extraction/compression limits are
retained; ABI request arrays add their own finite input budgets.

The ABI must define:

- Version/capability negotiation, exported symbols, calling conventions,
  fixed-width scalar types and extensible structure sizes.
- Opaque engine/archive/query handles and task IDs with explicit ownership,
  lifetime, stale-handle behavior and thread-safety rules.
- UTF-8 strings with lengths, numeric sizes/timestamps and structured error
  codes/details. Convert native paths at one boundary; do not export STL types.
- Provider-owned allocation/release functions for result batches, strings and
  errors. Never require Dart or another CRT to free C++ allocations.
- Contained C++ exceptions and checked inputs; failure never crosses the ABI
  as an exception or silently becomes a successful operation.
- Asynchronous task submission, bounded snapshots/results, cancellation,
  conflict-ID responses and shutdown. Any blocking wait/join belongs off the
  UI isolate. Task completion includes draining/releasing queued metadata.
- Optional asynchronous notification with coalescing/backpressure, event
  ownership and callback shutdown rules. A native callback cannot refer to
  disposed Dart state or freed buffers. Polling is acceptable initially.

Keep `TaskManager` workers and cancellation as the task authority. A Dart
`Future` alone does not move native work off the calling isolate. Passwords
must stay operation-scoped, never enter settings/logs/provenance, and receive
best-effort cleanup of owned native buffers and prompt input state.

## Shared metadata and selection

F2 extracts directory construction, Unicode simple-case-folded search, sorting
and selection mapping from Qt into the Qt-free Application `MetadataIndex` and
`BrowserService`. Qt models now adapt that implementation. See
[shared metadata browsing](../development/metadata-browsing.md).
Define Unicode comparison behavior explicitly while replacing `QString` helpers;
include Chinese, Japanese, Korean, emoji and accented-name regressions.

Keep one authoritative C++ metadata index. Flutter requests bounded pages/windows
of the current directory's direct children and a bounded visible folder tree;
it does not retain a complete second million-entry index. Batch transfer includes
stable identities and source/query generations. Avoid per-cell FFI calls and
whole-index JSON serialization. Result ownership must survive concurrent view
changes until the consumer releases it.

Preserve implicit parents, explicit empty folders, folder-first ordering, root
and parent navigation, path/depth/node/memory budgets and explicit view errors.
Folder rows have folder identities, never fabricated backend entry indices.
Selected files retain enumeration ordinals even for duplicate names. Selected
directories use already-validated directory paths and component boundaries.
Changing folder/query/sort clears selection and invalidates older generations;
select-all covers visible rows, while extract-all covers the entire archive.
Invalid/incomplete views cannot authorize selected extraction or drag export.
Real filesystem browsing remains a separate lazy `DirectoryService` consumer.

## Flutter and native desktop behavior

Use virtualized/lazily built list, table and tree widgets with bounded data
caches and local state updates. Never scan, sort a large index, decode archive
data, copy files or delete temporary exports from a widget build/input handler.

Port Home, archive browsing, creation/extraction dialogs, password retry,
overwrite policies, errors, task center, light/dark/system theme, recent records,
keyboard navigation, localization (`zh-CN`/`en`), accessibility and high DPI.
Preserve application identities and define migration from old QSettings theme
and recent paths. Do not stat recent paths on UI startup. Reuse original brand
assets and carry their license notices into the Flutter bundle.

System drag/drop is a platform acceptance requirement. Flutter's in-app
`Draggable`/`DragTarget` does not establish Explorer/Finder/file-manager transfer.
First prepare exports asynchronously through existing safe extraction. Extract
the Qt-dependent temporary manager into a reusable implementation; retain bounded
roots, byte/time/URL limits, source holds, delayed-copy lifetime and diagnostics.
Test cancel, failed cleanup, closing a page/app and native asynchronous copying.
Desktop plugins must be checked per platform, including Linux display/file
manager behavior; an available plugin alone is not acceptance evidence.

Preserve file-open and shell command semantics, Unicode/long paths, optional
system registration, non-destructive defaults and installation ownership.
Android remains experimental: Flutter mobile layout, storage permissions and
Storage Access Framework/`content://` IO require separate implementation and
device validation. Do not inherit Qt APK results or expand mobile release claims.

## Platforms, toolchain and release requirements

The target scope remains Windows 10/11 x64, Linux x86_64, macOS arm64 and macOS
x86_64 compatibility. Flutter currently lists those desktop architectures, but
has announced Intel Mac deprecation, including a plan to stop building Intel
macOS applications. Check the latest stable SDK during implementation. If a
required target becomes unsupported, report the unresolved platform decision;
do not claim full migration acceptance or silently freeze an old SDK.

Use Flutter's latest stable SDK and bundled Dart, with compatible latest stable
plugins. Record the SDK/engine revision, Dart version, dependency resolution
time, application lockfile, native dependency manifest, toolchain and platform
SDK for each build. A lockfile records one resolution and is updated under the
latest-stable policy; it is not an indefinite historical version requirement.

Integrate existing target-based CMake output using a build hook or the platform
runner. Reuse the managed native source resolver/provenance pipeline. Match
architecture, PIC, symbol exports and MSVC runtime settings. Core-only builds
do not acquire a Flutter SDK prerequisite. Flutter packages contain engine/AOT
assets, the C ABI/backend, required plugins, licenses and actual build provenance;
they do not depend on Qt. Existing `Package-*` scripts are Qt-only until adapted.

Preserve LiteArc executable/distribution names, application IDs, settings and
registration ownership, and `share/AnAnArchiver` compatibility metadata. Recheck
portable launch, installers, MSIX identity, Linux MIME, macOS document-open,
entitlements, signing/notarization and clean-machine runtime deployment against
the actual Flutter payload. Historical Qt loader/renderer patches are not
requirements to patch Flutter without evidence.

## Migration stages and completion evidence

| Stage | Scope | Required evidence |
| --- | --- | --- |
| F1 | Stable C ABI over existing services | C consumer/binding smoke, ownership/error/version tests, cancel/conflict/shutdown tests |
| F2 | Shared metadata query service | Current-folder behavior, Unicode, duplicate identities, budgets, stale generations, bounded pages |
| F3 | Flutter open and browse | Real content detection, Home, folder tree/navigation, virtualized list, search/sort/selection |
| F4 | Archive operations | Real extraction/creation, password retry, overwrite, progress, cancellation; Core security regression |
| F5 | Desktop parity | Settings migration, theme/history/tasks, keyboard/DPI/accessibility, real system drag/open flows |
| F6 | Release and performance | Native target builds/runs, package/upgrade/provenance checks, large metadata benchmarks |

Core tests remain mandatory. Add C ABI tests, Dart binding tests, widget tests
and end-to-end tests with real archives; do not replace Core tests with GUI
screenshots. CI currently has automatic triggers disabled and retained manual
Core/Qt jobs. Implement Flutter format/analyze/test and native platform release
jobs before reporting Flutter CI support; do not change trigger policy implicitly.

Compare Qt baseline and Flutter Release builds using the same backend/codecs,
settings and generated datasets. Measure startup/first usable view, 100k/1M
metadata peak memory, scrolling frame times, query latency, cancellation,
archive throughput and package size. Do not assume a framework switch improves
those metrics. Unsupported platform targets and incomplete checks stay explicit.

Qt files may be retired from `main` only after the relevant Flutter behavior
and release path are verified; the preserved baseline branch remains available.

## Official references checked for this decision

- [Flutter desktop support](https://docs.flutter.dev/platform-integration/desktop).
- [Supported deployment platforms](https://docs.flutter.dev/reference/supported-platforms).
- [Intel Mac deprecation tracking](https://github.com/flutter/flutter/issues/188328).
- [Native code binding and FFI build hooks](https://docs.flutter.dev/platform-integration/bind-native-code).
- [NativeCallable.listener lifetime and thread rules](https://api.dart.dev/dart-ffi/NativeCallable/NativeCallable.listener.html).
- [DataTable performance considerations](https://api.flutter.dev/flutter/material/DataTable-class.html).
- [Official lazy TableView/TreeView package](https://pub.dev/packages/two_dimensional_scrollables).
- [Flutter Draggable scope](https://api.flutter.dev/flutter/widgets/Draggable-class.html).
- [Native file selection](https://pub.dev/packages/file_selector).
