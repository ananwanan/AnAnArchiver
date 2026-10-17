# Core and frontend boundaries

The first refactoring milestone implements archive opening and metadata browsing.
The default desktop frontend remains Qt 6 Quick/QML. Archive processing and
application services are ordinary C++23 and can be built and tested without Qt.

Dependency direction:

```text
QML -> Qt ArchiveController / model adapters
    -> Application ArchiveService / TaskManager / DirectoryService
    -> ArchiveReader / ArchiveEntry / ArchiveInfo / ArchiveError
    -> libarchive adapter -> libarchive/libarchive
```

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

Only the backend adapter includes libarchive headers. Only `frontend/qt` includes
Qt headers. Public headers under `include/ananarchiver` contain standard C++
types. Native filesystem paths and UTF-8 archive names have a centralized
conversion boundary; entry sizes and timestamps remain numeric core metadata.
Display sizes, dates, icons, and translated errors belong to the frontend.

The existing application bootstrap and file view move into `frontend/qt`.
`AppContext` is replaced by an archive controller. `FileTreeModel` remains a Qt
adapter, but obtains immediate directory children from `DirectoryService` on
its owned worker instead of recursively traversing the entire filesystem.
Archive metadata uses a separate flat `ArchiveEntryModel`; it is never confused
with the real-directory tree.

Opening uses content detection. Extensions affect the file dialog only.
Enumeration streams metadata without retaining entry contents. Some formats
need sequential decompression to reach subsequent headers; indexing is not
random access and is not a CRC or integrity test. Encrypted metadata is browsable
only when the backend can enumerate it without a password. Password prompting
belongs to the extraction milestone.

Extraction, compression, archive rewriting, previews, shell integration, and
packaging remain separate later milestones. There are no stub writer or
extraction implementations that report success. Until extraction exists, opening
an entry containing a traversal path only displays that pathname and never writes
it to the filesystem. The extraction milestone must add long-lived traversal,
symlink/hardlink, overwrite, bomb-limit, and incomplete-output regression tests
before exposing extraction in the GUI.
