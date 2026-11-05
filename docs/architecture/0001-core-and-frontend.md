# Core and frontend boundaries

Updated 2026-11-05: the selected frontend is Flutter/Dart on `main`. The previous
Qt-default decision is superseded by [ADR 0007](0007-flutter-frontend.md).
The preserved Qt baseline is `codex/qt-baseline-2026-11-05` at
`53185742ad1e640b24da397cfa3544507af4edef`.

## Target architecture and implementation state

```text
Flutter widgets / Dart Controller / ViewModel
    -> Dart FFI -> ananarchiver_c_api
    -> C++ Application ArchiveService / TaskManager / metadata query service
    -> ArchiveReader / ArchiveWriter / policies / errors / cancellation
    -> libarchive adapter -> libarchive/libarchive
```

Core, Backend and Application already use ordinary C++23 and build without Qt.
The C ABI, shared metadata query service and Flutter app are planned migration
work, not implemented functionality. Existing Qt controllers/models and QML
remain baseline code while each replacement is verified. Current Qt commands
are documented in [the baseline guide](../development/qt-baseline.md).

Only the backend includes libarchive headers. Neither Dart nor its platform
plugins may use backend handles. The C ABI wraps Application, exports opaque
handles/length-delimited UTF-8 data and contains exceptions; native allocations
have explicit provider-owned release functions. C ABI and Application do not
link Flutter, Dart or Qt. Native platform UI integrations remain adapters.

The existing Qt entry/folder models contain reusable metadata/query/selection
behavior. Extract it into Application, retaining bounded directory construction,
Unicode semantics, current-folder views, original entry identities and stale
query invalidation. Flutter receives bounded result pages and keeps finite
caches, rather than copying the entire C++ metadata index. Real-directory
browsing remains a separate lazy DirectoryService operation on managed workers.

Numeric sizes/timestamps and structured errors cross the ABI as data. Frontends
format dates, sizes, icons and translated error messages. Native filesystem paths
and UTF-8 archive names keep centralized conversion. GUI code never owns archive
handles or synchronously performs archive IO.

## Native dependency policy

Dependency policy uses the latest stable upstream releases rather than a fixed
historical version. Use the latest stable release from the official
libarchive/libarchive repository. Resolve sources before building
and record the actual revisions used. See the [dependency plan](../../third_party/README.md)
for source selection, matching nested modules, and current implementation status.

The official libarchive checkout stays unchanged. Backend compatibility fixes for
Mbed TLS 4 / TF-PSA-Crypto and ZIP abort cleanup are applied by
`cmake/PrepareLibarchive.cmake` to a generated export of the verified revision.
`cmake/patches/libarchive-compatibility.patch` remains backend code rather than
application or GUI logic. The build records the official base revision and patch
hash, and installs a matching patch copy with its provenance so replaying the
official source manifest and building the corresponding application source can
reproduce the modified backend. An incompatible patch fails explicitly.

## Existing archive capabilities

Opening uses content detection; extensions only guide file selection. Enumeration
streams metadata and does not retain entry contents. Sequential formats may
decode data to reach later headers; listing is not a CRC/integrity test.
Application already supports safe extraction and capability-driven creation.
Flutter must use those implementations, including password/overwrite interaction,
atomic output, resource limits and cancellation, with new frontend evidence.
TestArchive, archive rewriting and bounded previews remain separate future work.
Historical Qt milestone results are not Flutter acceptance results.
