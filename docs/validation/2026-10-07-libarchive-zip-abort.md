# libarchive ZIP abort cleanup — 2026-10-07

M3 cancellation/time-limit regressions exposed an upstream ZIP writer leak:
`archive_write_fail` bypasses entry finalization, while the ZIP format's free
callback did not release its active compressor. Linux LeakSanitizer reported
536,224 leaked bytes across ten allocations in two interrupted Deflate entries.
This is backend lifecycle logic, so the fix belongs in the pinned libarchive
repository rather than an application parser or GUI workaround.

The backend now tracks an initialized entry compressor, releases it from format
free without writing or finishing output, and shares that cleanup with normal
finish and output-error paths. It clears the active flag to avoid double release,
handles Zstandard allocation failure, and cleans residual state before reusing
the codec union after a recoverable failure.

The minimal `test_write_format_zip_abort` uses a 64 KiB memory output buffer and
four-byte payloads. It covers Deflate, bzip2, LZMA, XZ and Zstandard; abort after
header/partial data/finished entry; and switching a finished codec to Store or
Deflate before aborting another entry. Unsupported optional codecs are explicitly
skipped by the upstream fixture.

The standalone backend and its test executable were built under Ubuntu 24.04
WSL2/GCC 13.3 with ASan/UBSan, using only the project-built pinned codecs. All
22 selected ZIP writer tests passed, including the new abort regression, codec,
ZIP64, large-stream and encryption tests, without sanitizer failures. The full
application sanitizer suite also ceased reporting the abort leak. The remaining
libarchive format tests were not run as part of this dependency change.

Base revision: `8bb3bbdc7b117a1e22086a2260f2087aafa90687`.
Fixed revision: `0a6ff818eefd3f13a941ccf6994bcf72cdc122bb`.
The fix is committed locally on `codex/m3-zip-abort-cleanup`. It has not been
pushed to the backend remote; publishing that revision is required before an
independent remote checkout can fetch the updated gitlink. The application
dependency update is kept separate from the M3 implementation changes.
