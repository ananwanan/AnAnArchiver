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
          info.ref.features & AA_FEATURE_DESKTOP_EXPORT == 0 ||
          info.ref.features & (AA_FEATURE_EXTRACT | AA_FEATURE_COMPRESS) !=
              (AA_FEATURE_EXTRACT | AA_FEATURE_COMPRESS) ||
          info.ref.max_active_tasks < 2 ||
          info.ref.max_retained_tasks < 2) {
        throw const BrowserFailure(
          AA_ABI_MISMATCH,
          0,
          'ABI v1 / browser / desktop export required',
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
    if (Platform.isMacOS) {
      return '$dir/../Frameworks/libananarchiver_c_api.dylib';
    }
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

  void string(aa_string dest, String source, Arena a, {bool secret = false}) {
    final bytes = utf8.encode(source);
    if (bytes.length > AA_MAX_STRING_BYTES) {
      if (secret) bytes.fillRange(0, bytes.length, 0);
      throw const BrowserFailure(
        AA_RESOURCE_LIMIT,
        AA_ERROR_RESOURCE_LIMIT,
        'UTF-8 input limit',
      );
    }
    final ptr = a<Uint8>(bytes.isEmpty ? 1 : bytes.length);
    ptr.asTypedList(bytes.length).setAll(0, bytes);
    if (secret) {
      bytes.fillRange(0, bytes.length, 0);
      a.onReleaseAll(
        () => ptr.asTypedList(dest.size).fillRange(0, dest.size, 0),
      );
    }
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
        case 'probe':
          final r = a<aa_open_request>();
          r.ref.struct_size = sizeOf<aa_open_request>();
          r.ref.probe_only = 1;
          string(r.ref.path, args['path'] as String, a);
          status = api.aa_archive_open(engine, r, t, error);
        case 'open':
          final r = a<aa_open_request>();
          r.ref.struct_size = sizeOf<aa_open_request>();
          string(r.ref.path, args['path'] as String, a);
          string(
            r.ref.password,
            args['password'] as String? ?? '',
            a,
            secret: true,
          );
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

  int submitOperation(String op, Map<String, Object?> args) => using((a) {
    final t = a<Uint64>();
    final error = a<Pointer<aa_error>>();
    int status;
    if (op == 'compress') {
      final paths = (args['paths'] as List).cast<String>();
      if (paths.isEmpty || paths.length > AA_MAX_REQUEST_ITEMS) {
        throw const BrowserFailure(
          AA_RESOURCE_LIMIT,
          AA_ERROR_RESOURCE_LIMIT,
          'Input path count limit',
        );
      }
      final r = a<aa_compress_request>();
      r.ref.struct_size = sizeOf<aa_compress_request>();
      r.ref.format = args['format'] as int;
      r.ref.method = args['method'] as int;
      r.ref.level = args['level'] as int;
      r.ref.overwrite_existing = args['replace'] == true ? 1 : 0;
      r.ref.preserve_permissions = args['permissions'] == true ? 1 : 0;
      r.ref.preserve_timestamps = args['timestamps'] == true ? 1 : 0;
      string(r.ref.destination, args['destination'] as String, a);
      final buffer = a<aa_string>(paths.length);
      var bytes = 0;
      for (var i = 0; i < paths.length; i++) {
        string(buffer[i], paths[i], a);
        bytes += buffer[i].size;
        if (bytes > 4 * 1024 * 1024) {
          throw const BrowserFailure(
            AA_RESOURCE_LIMIT,
            AA_ERROR_RESOURCE_LIMIT,
            'Input path byte limit',
          );
        }
      }
      r.ref.input_paths = buffer;
      r.ref.input_path_count = paths.length;
      status = api.aa_archive_compress(engine, r, t, error);
    } else {
      final r = a<aa_extract_request>();
      r.ref.struct_size = sizeOf<aa_extract_request>();
      r.ref.overwrite = args['overwrite'] as int? ?? AA_OVERWRITE_ASK;
      r.ref.links = AA_LINK_SAFE;
      string(r.ref.destination, args['destination'] as String, a);
      string(
        r.ref.password,
        args['password'] as String? ?? '',
        a,
        secret: true,
      );
      if (op == 'extractAll') {
        string(r.ref.path, args['path'] as String, a);
        r.ref.extract_all = 1;
        status = api.aa_archive_extract(engine, r, t, error);
      } else {
        final selection = args['selection'] as int;
        if (selection == 0) {
          throw const BrowserFailure(
            AA_STALE_VIEW,
            AA_ERROR_UNSUPPORTED_OPERATION,
            'No completed selection',
          );
        }
        final extract = op == 'export'
            ? api.aa_browser_export
            : api.aa_browser_extract;
        status = extract(
          engine,
          args['archive'] as int,
          args['query'] as int,
          selection,
          r,
          t,
          error,
        );
      }
    }
    check(status, error.value);
    return t.value;
  });

  BrowserPage exportControl(Map<String, Object?> args) => using((a) {
    final r = a<aa_export_request>();
    r.ref.struct_size = sizeOf<aa_export_request>();
    r.ref.action = args['action'] as int;
    string(r.ref.path, args['path'] as String? ?? '', a);
    final out = a<Pointer<aa_export_result>>();
    final error = a<Pointer<aa_error>>();
    check(api.aa_export_control(engine, r, out, error), error.value);
    final ptr = out.value;
    try {
      final result = ptr.ref;
      if (result.count > 256 || result.error_count > 16) {
        throw const BrowserFailure(
          AA_RESOURCE_LIMIT,
          14,
          'Export response budget',
        );
      }
      return BrowserPage(
        paths: List.generate(result.count, (i) => text(result.paths[i])),
        failures: List.generate(
          result.error_count,
          (i) => failure(AA_ARCHIVE_ERROR, result.errors[i]),
        ),
      );
    } finally {
      api.aa_export_result_release(ptr);
    }
  });

  BrowserPage capabilities() => using((a) {
    final out = a<Pointer<aa_capability_batch>>();
    final error = a<Pointer<aa_error>>();
    check(api.aa_compression_capabilities(engine, out, error), error.value);
    final ptr = out.value;
    try {
      if (ptr.ref.count > AA_MAX_REQUEST_ITEMS) {
        throw const BrowserFailure(
          AA_RESOURCE_LIMIT,
          AA_ERROR_RESOURCE_LIMIT,
          'Capability count limit',
        );
      }
      return BrowserPage(
        capabilities: List.generate(ptr.ref.count, (i) {
          final c = ptr.ref.capabilities[i];
          return CompressionCapability(
            format: c.format,
            method: c.method,
            formatName: text(c.format_name),
            methodName: text(c.method_name),
            extension: text(c.extension),
            hasLevels: c.has_levels != 0,
            minLevel: c.min_level,
            maxLevel: c.max_level,
            defaultLevel: c.default_level,
            permissions: c.supports_permissions != 0,
            timestamps: c.supports_timestamps != 0,
          );
        }, growable: false),
      );
    } finally {
      api.aa_capability_batch_release(ptr);
    }
  });

  void resolve(Map<String, Object?> args) {
    check(
      api.aa_task_resolve_conflict(
        engine,
        args['task'] as int,
        args['conflict'] as int,
        args['overwrite'] as int,
        args['all'] == true ? 1 : 0,
      ),
      nullptr,
    );
  }

  TaskProgress poll(String op, {int? id, bool throwTerminal = true}) => using((
    a,
  ) {
    final out = a<Pointer<aa_task_snapshot>>();
    final error = a<Pointer<aa_error>>();
    check(api.aa_task_poll(engine, id ?? task, out, error), error.value);
    final ptr = out.value;
    try {
      final s = ptr.ref;
      if (throwTerminal &&
          (s.state == AA_TASK_FAILED || s.state == AA_TASK_CANCELLED)) {
        throw failure(AA_ARCHIVE_ERROR, s.error);
      }
      return TaskProgress(
        op,
        s.state,
        s.processed_entries,
        s.elapsed_ms,
        taskId: s.task_id,
        bytes: s.processed_bytes,
        totalBytes: s.flags & AA_HAS_TOTAL_BYTES != 0 ? s.total_bytes : null,
        totalEntries: s.flags & AA_HAS_TOTAL_ENTRIES != 0
            ? s.total_entries
            : null,
        bytesPerSecond: s.bytes_per_second,
        currentFile: text(s.current_file),
        phase: s.compression_phase,
        conflict: s.flags & AA_HAS_CONFLICT != 0
            ? TaskConflict(
                s.conflict_id,
                text(s.conflict_entry.path),
                text(s.conflict_destination),
              )
            : null,
        error: s.error != nullptr ? failure(AA_ARCHIVE_ERROR, s.error) : null,
      );
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

  void cancelTask(int id) {
    if (id != 0) api.aa_task_cancel(engine, id);
  }

  void releaseTask(int id) {
    if (id != 0) api.aa_task_release(engine, id);
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
