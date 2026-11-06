# litearc_binding

Pure Dart ABI v1 transport. `BrowserClient.start()` owns one persistent isolate;
all pointers, allocation, UTF-8 transfer, native calls, polling and provider frees
stay on that isolate. Call `close()` and await it before normal application exit.
Do not kill the isolate to dispose it.

The generator is `dart run tool/generate.dart [absolute-libclang-library-path]`.
It reads the repository's authoritative C header and emits `lib/src/generated.dart`
and its SHA-256 in `binding-source.json`. The checked-in lockfile records ffigen.
The opaque-engine diagnostic during generation is expected: the C ABI intentionally
has no engine definition. Regenerate and review the diff after header changes.

Run `dart test` for trace/layout/startup tests. Set `LITEARC_TEST_LIBRARY` and
`LITEARC_TEST_ARCHIVE` to run real C ABI tests; generate the default 300-file fixture
using the app's `tool/generate_fixture.py`. Missing integration inputs are reported
as skipped tests, never as native integration success.

F4 adds `capabilities`, `extractAll`, `extractSelected`, `compress`,
`resolveConflict` and `cancelOperation`. Browser tasks run beside one native
operation; controls bypass both polling loops. `open`/`close` cancel operations;
`query` leaves a submitted operation running. `close()` cancels interactions,
waits for the operation and destroys the engine on the worker before exit.
Pass the archive/query/completed selection handles to `extractSelected`; an
invalid selection is always an error. Use `extractAll` explicitly for all entries.

Set `LITEARC_TEST_OPERATIONS` to the directory produced by the C ABI test
executable's `--generate-flutter-fixtures <directory>` mode for real password,
conflict, safety and creation tests. `tool/measure_operations.dart` generates
streamed payload/small-file data and tests native million-metadata selection;
its separate CLI measurements do not measure Flutter rendering or AOT Dart.

See [thread, queue and lifetime contract](../../../../docs/development/flutter.md).
