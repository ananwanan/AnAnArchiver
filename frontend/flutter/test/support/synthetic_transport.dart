import 'dart:async';

import 'package:litearc_binding/litearc_binding.dart';

// Explicit synthetic transport: exercises Dart cache/rendering only. It does
// not claim native indexing, Unicode folding, backend IO or ABI coverage.
class SyntheticTransport implements BrowserTransport {
  SyntheticTransport({this.count = 1000, this.pageRows = 128});
  final int count, pageRows;
  int query = 0;
  bool closed = false;
  int maxConcurrent = 0, active = 0;
  final requests = <String>[];
  Completer<void>? gate;
  @override
  Stream<TaskProgress> get progress => const Stream.empty();
  @override
  Future<BrowserPage> request(
    String op, [
    Map<String, Object?> args = const {},
  ]) async {
    requests.add(op);
    active++;
    if (active > maxConcurrent) maxConcurrent = active;
    try {
      if (op == 'open') return BrowserPage(archive: 1, entryCount: count);
      if (op == 'query') {
        return BrowserPage(
          archive: 1,
          query: ++query,
          total: args['folder'] == 'empty' ? 0 : count,
        );
      }
      if (op == 'folders') return const BrowserPage(archive: 1);
      if (op == 'select') {
        return BrowserPage(
          archive: 1,
          query: args['query'] as int,
          selection: 1,
        );
      }
      if (op == 'page') {
        final q = args['query'] as int;
        final offset = args['offset'] as int;
        if (gate != null) await gate!.future;
        return BrowserPage(
          archive: 1,
          query: q,
          offset: offset,
          total: count,
          transferBytes: 32768,
          rows: List.generate(
            (count - offset).clamp(0, pageRows),
            (i) => BrowserRow(
              identity: offset + i,
              index: offset + i,
              name: 'file-${offset + i}.txt',
              path: 'file-${offset + i}.txt',
              folder: false,
              type: 1,
              flags: 1,
              size: i,
              modified: 0,
            ),
          ),
        );
      }
      return const BrowserPage();
    } finally {
      active--;
    }
  }

  @override
  Future<void> close() async {
    closed = true;
  }
}
