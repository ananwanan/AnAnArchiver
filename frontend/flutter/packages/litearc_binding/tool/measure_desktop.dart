import 'dart:io';

import 'package:litearc_binding/litearc_binding.dart';

// CLI/JIT + Release C ABI. Generated archives only; never a renderer benchmark.
Future<void> main(List<String> args) async {
  if (args.length != 3)
    throw ArgumentError('library million-entry.zip oversized.zip');
  final temp = await Directory.systemTemp.createTemp('litearc-f5-budget-');
  final c = await BrowserClient.start(library: args[0]);
  final total = Stopwatch()..start();
  try {
    for (final source in args.skip(1)) {
      final clock = Stopwatch()..start();
      final archive = await c.request('open', {'path': source});
      final openMs = clock.elapsedMilliseconds;
      final query = await c.request('query');
      clock.reset();
      final selected = await c.request('select', {
        'query': query.query,
        'all': true,
      });
      final selectMs = clock.elapsedMilliseconds;
      final root = (await c.request('exportControl', {
        'action': 1,
        'path': temp.path,
      })).paths.single;
      clock.reset();
      BrowserFailure? failure;
      try {
        await c.request('export', {
          'archive': archive.archive,
          'query': query.query,
          'selection': selected.selection,
          'destination': root,
        });
      } on BrowserFailure catch (e) {
        failure = e;
      }
      if (failure?.code != 14)
        throw StateError('Expected real export resource rejection: $failure');
      final rejectMs = clock.elapsedMilliseconds;
      final children = await Directory(root).list().length;
      if (children != 0)
        throw StateError('Rejected export left partial output');
      stdout.writeln({
        'source': source,
        'entries': archive.entryCount,
        'open_ms': openMs,
        'native_select_ms': selectMs,
        'Dart_rows': selected.rows.length,
        'reject_ms': rejectMs,
        'error_code': failure!.code,
        'remaining_children': children,
        'maxRss': ProcessInfo.maxRss,
      });
      await c.request('exportControl', {'action': 4, 'path': root});
      await c.request('close');
    }
  } finally {
    final shutdown = Stopwatch()..start();
    await c.close();
    stdout.writeln({
      'shutdown_ms': shutdown.elapsedMilliseconds,
      'total_ms': total.elapsedMilliseconds,
    });
    await temp.delete(recursive: true);
  }
}
