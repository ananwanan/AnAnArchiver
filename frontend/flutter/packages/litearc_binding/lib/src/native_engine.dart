import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'generated.dart';
import 'models.dart';

// Only constructed on the persistent binding isolate. No pointer leaves it.
class NativeEngine {
  NativeEngine(String? library)
    : api = LiteArcNative(DynamicLibrary.open(library ?? _libraryPath())) {
    using((a) {
      final info = a<aa_api_info>();
      info.ref.struct_size = sizeOf<aa_api_info>();
      check(api.aa_get_api_info(info), nullptr);
      if (info.ref.abi_version != AA_ABI_VERSION ||
          info.ref.features & AA_FEATURE_BROWSER == 0 ||
          info.ref.max_active_tasks < 1 ||
          info.ref.max_retained_tasks < 1) {
        throw const BrowserFailure(
          AA_ABI_MISMATCH,
          0,
          'ABI v1 / browser required',
        );
      }
      final out = a<Pointer<aa_engine>>();
      final error = a<Pointer<aa_error>>();
      check(api.aa_engine_create(AA_ABI_VERSION, out, error), error.value);
      engine = out.value;
    });
  }
  final LiteArcNative api;
  late final Pointer<aa_engine> engine;
  int archive = 0, query = 0, task = 0;

  static String _libraryPath() {
    final dir = File(Platform.resolvedExecutable).parent.path;
    if (Platform.isWindows) return '$dir/ananarchiver_c_api.dll';
    if (Platform.isMacOS)
      return '$dir/../Frameworks/libananarchiver_c_api.dylib';
    return '$dir/lib/libananarchiver_c_api.so';
  }

  String text(aa_string value) => value.size == 0
      ? ''
      : utf8.decode(value.data.cast<Uint8>().asTypedList(value.size));
  BrowserFailure failure(int status, Pointer<aa_error> error) =>
      error == nullptr
      ? BrowserFailure(status, 0, 'C ABI status $status')
      : BrowserFailure(
          status,
          error.ref.code,
          text(error.ref.message),
          text(error.ref.backend_message),
          text(error.ref.path),
        );
  void check(int status, Pointer<aa_error> error) {
    try {
      if (status != AA_OK) throw failure(status, error);
    } finally {
      if (error != nullptr) api.aa_error_release(error);
    }
  }

  void string(aa_string dest, String source, Arena a) {
    final bytes = utf8.encode(source);
    if (bytes.length > AA_MAX_STRING_BYTES) {
      throw const BrowserFailure(
        AA_RESOURCE_LIMIT,
        AA_ERROR_RESOURCE_LIMIT,
        'UTF-8 input limit',
      );
    }
    final ptr = a<Uint8>(bytes.isEmpty ? 1 : bytes.length);
    ptr.asTypedList(bytes.length).setAll(0, bytes);
    dest.data = ptr.cast();
    dest.size = bytes.length;
  }

  void submit(String op, Map<String, Object?> args) {
    using((a) {
      final t = a<Uint64>();
      final id = a<Uint64>();
      final error = a<Pointer<aa_error>>();
      int status;
      switch (op) {
        case 'open':
          final r = a<aa_open_request>();
          r.ref.struct_size = sizeOf<aa_open_request>();
          string(r.ref.path, args['path'] as String, a);
          status = api.aa_browser_open(engine, r, id, t, error);
          if (status == AA_OK) {
            archive = id.value;
            query = 0;
          }
        case 'query':
          final r = a<aa_browser_query_request>();
          r.ref.struct_size = sizeOf<aa_browser_query_request>();
          r.ref.archive = archive;
          r.ref.sort_column = args['sort'] as int? ?? 0;
          r.ref.descending = args['descending'] == true ? 1 : 0;
          string(r.ref.folder, args['folder'] as String? ?? '', a);
          string(r.ref.search, args['search'] as String? ?? '', a);
          status = api.aa_browser_query(engine, r, id, t, error);
          if (status == AA_OK) query = id.value;
        case 'page':
          final r = a<aa_browser_page_request>();
          r.ref.struct_size = sizeOf<aa_browser_page_request>();
          r.ref.archive = archive;
          r.ref.query = args['query'] as int;
          r.ref.offset = args['offset'] as int;
          r.ref.max_rows = 128;
          r.ref.max_bytes = AA_MAX_PAGE_BYTES;
          status = api.aa_browser_request_page(engine, r, t, error);
        case 'folders':
          final r = a<aa_browser_folder_page_request>();
          r.ref.struct_size = sizeOf<aa_browser_folder_page_request>();
          r.ref.archive = archive;
          r.ref.offset = args['offset'] as int? ?? 0;
          r.ref.max_rows = 128;
          r.ref.max_bytes = AA_MAX_PAGE_BYTES;
          string(r.ref.folder, args['folder'] as String? ?? '', a);
          status = api.aa_browser_request_folders(engine, r, t, error);
        case 'select':
          final r = a<aa_browser_select_request>();
          r.ref.struct_size = sizeOf<aa_browser_select_request>();
          r.ref.archive = archive;
          r.ref.query = args['query'] as int;
          r.ref.select_all = args['all'] == true ? 1 : 0;
          final ranges = (args['ranges'] as List?) ?? [];
          if (ranges.length > AA_MAX_REQUEST_ITEMS) {
            throw const BrowserFailure(
              AA_RESOURCE_LIMIT,
              AA_ERROR_RESOURCE_LIMIT,
              'Selection range limit',
            );
          }
          final buffer = a<aa_browser_range>(
            ranges.isEmpty ? 1 : ranges.length,
          );
          for (var i = 0; i < ranges.length; i++) {
            buffer[i].first = (ranges[i] as List)[0] as int;
            buffer[i].count = (ranges[i] as List)[1] as int;
          }
          r.ref.ranges = buffer;
          r.ref.range_count = ranges.length;
          status = api.aa_browser_select(engine, r, id, t, error);
        default:
          throw ArgumentError.value(op);
      }
      check(status, error.value);
      task = t.value;
    });
  }

  TaskProgress poll(String op) => using((a) {
    final out = a<Pointer<aa_task_snapshot>>();
    final error = a<Pointer<aa_error>>();
    check(api.aa_task_poll(engine, task, out, error), error.value);
    final ptr = out.value;
    try {
      final s = ptr.ref;
      if (s.state == AA_TASK_FAILED || s.state == AA_TASK_CANCELLED) {
        throw failure(AA_ARCHIVE_ERROR, s.error);
      }
      return TaskProgress(op, s.state, s.processed_entries, s.elapsed_ms);
    } finally {
      api.aa_snapshot_release(ptr);
    }
  });
  BrowserPage result() => using((a) {
    final out = a<Pointer<aa_browser_result>>();
    final error = a<Pointer<aa_error>>();
    check(api.aa_browser_task_result(engine, task, out, error), error.value);
    final ptr = out.value;
    try {
      final r = ptr.ref;
      if (r.count > 128 || r.transfer_bytes > AA_MAX_PAGE_BYTES) {
        throw const BrowserFailure(AA_RESOURCE_LIMIT, 0, 'Invalid page limits');
      }
      return BrowserPage(
        archive: r.archive,
        query: r.query,
        selection: r.selection,
        entryCount: r.entry_count,
        folderCount: r.folder_count,
        indexBytes: r.index_bytes,
        offset: r.offset,
        total: r.total_rows,
        transferBytes: r.transfer_bytes,
        rows: List.generate(r.count, (i) {
          final row = r.rows[i];
          final e = row.entry;
          return BrowserRow(
            identity: row.identity,
            index: e.index,
            name: text(row.name),
            path: text(e.path),
            folder: row.is_folder != 0,
            type: e.type,
            flags: e.flags,
            size: e.size,
            modified: e.modified_unix_ms,
          );
        }, growable: false),
      );
    } finally {
      api.aa_browser_result_release(ptr);
    }
  });
  void cancel() {
    if (task != 0) api.aa_task_cancel(engine, task);
  }

  void release() {
    if (task != 0) {
      api.aa_task_release(engine, task);
      task = 0;
    }
  }

  void closeArchive() {
    if (archive != 0) {
      api.aa_browser_close(engine, archive);
      archive = 0;
      query = 0;
    }
  }

  void destroy() {
    release();
    closeArchive();
    api.aa_engine_shutdown(engine);
    api.aa_engine_destroy(engine);
  }
}
