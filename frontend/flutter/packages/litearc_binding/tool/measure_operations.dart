// Generated IO / real C ABI measurements. Run as a separate Dart CLI process;
// these timings do not measure Flutter frames or a Release AOT Dart frontend.
import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:litearc_binding/litearc_binding.dart';

Future<void> main(List<String> args) async {
  if (args.length < 2)
    throw ArgumentError(
      'library, generated million-entry ZIP, optional MiB / small file count',
    );
  final temp = await Directory.systemTemp.createTemp('litearc-f4-measure-');
  final client = await BrowserClient.start(library: args[0]);
  final metrics = <String, Object?>{};
  final mib = args.length > 2 ? int.parse(args[2]) : 1024;
  final smallCount = args.length > 3 ? int.parse(args[3]) : 5000;
  Future<int> timed(Future<void> Function() action) async {
    final watch = Stopwatch()..start();
    await action();
    return watch.elapsedMilliseconds;
  }

  void verify(bool condition, String message) {
    if (!condition) throw StateError(message);
  }

  try {
    final caps = (await client.request('capabilities')).capabilities;
    final store = caps.firstWhere((c) => c.format == 1 && c.method == 1);
    final deflate = caps.firstWhere((c) => c.format == 1 && c.method == 2);
    Map<String, Object?> compression(
      CompressionCapability c,
      List<String> inputs,
      String destination, {
      bool replace = false,
    }) => {
      'paths': inputs,
      'destination': destination,
      'format': c.format,
      'method': c.method,
      'level': c.defaultLevel,
      'permissions': c.permissions,
      'timestamps': c.timestamps,
      'replace': replace,
    };
    final input = File('${temp.path}/stream.bin');
    final block = Uint8List(1024 * 1024);
    var seed = 123456789;
    for (var i = 0; i < block.length; i++) {
      seed = (1664525 * seed + 1013904223) & 0xffffffff;
      block[i] = seed >> 24;
    }
    final file = await input.open(mode: FileMode.write);
    for (var i = 0; i < mib; i++) {
      await file.writeFrom(block);
    }
    await file.close();
    final archive = '${temp.path}/large.zip';
    metrics['largeBytes'] = await input.length();
    metrics['largeCreateMs'] = await timed(() async {
      await client.request(
        'compress',
        compression(store, [input.path], archive),
      );
    });
    metrics['largeArchiveBytes'] = await File(archive).length();
    final output = '${temp.path}/large-output';
    metrics['largeExtractMs'] = await timed(() async {
      await client.request('extractAll', {
        'path': archive,
        'destination': output,
        'overwrite': 1,
      });
    });
    verify(
      await sha256.bind(input.openRead()).first ==
          await sha256.bind(File('$output/stream.bin').openRead()).first,
      'stream payload hash mismatch',
    );
    metrics['largeSha256Matches'] = true;
    metrics['largeRoundtripMaxRssBytes'] = ProcessInfo.maxRss;
    final source = Directory('${temp.path}/small');
    await source.create();
    for (var i = 0; i < smallCount; i++) {
      await File('${source.path}/file-$i.txt')
          .writeAsString('generated small payload $i');
    }
    final many = '${temp.path}/small.zip';
    metrics['smallFileCount'] = smallCount;
    metrics['smallCreateMs'] = await timed(() async {
      await client.request('compress', compression(store, [source.path], many));
    });
    metrics['smallExtractMs'] = await timed(() async {
      await client.request('extractAll', {
        'path': many,
        'destination': '${temp.path}/small-output',
        'overwrite': 1,
      });
    });
    verify(
      await Directory('${temp.path}/small-output/small').list().length ==
          smallCount,
      'small file count mismatch',
    );
    for (final i in [0, smallCount ~/ 2, smallCount - 1]) {
      verify(
        await File('${temp.path}/small-output/small/file-$i.txt')
                .readAsString() ==
            'generated small payload $i',
        'small payload mismatch',
      );
    }
    metrics['smallRoundtripMaxRssBytes'] = ProcessInfo.maxRss;
    for (final type in ['compress', 'extractAll']) {
      final destination = '${temp.path}/cancel-$type';
      if (type == 'compress')
        await File(destination).writeAsString('original archive');
      final began = Completer<void>();
      final finished = Completer<TaskProgress>();
      final subscription = client.progress.listen((p) {
        if (p.operation != type) return;
        if (p.bytes > 0 && !began.isCompleted) began.complete();
        if (p.terminal && !finished.isCompleted) finished.complete(p);
      });
      final operation = client.request(
        type,
        type == 'compress'
            ? compression(deflate, [input.path], destination, replace: true)
            : {'path': archive, 'destination': destination, 'overwrite': 1},
      );
      final checked = operation.then<void>(
        (_) => throw StateError(
          'Cancellation completed as success before test could cancel',
        ),
        onError: (Object e) {
          verify(
            e is BrowserFailure && e.code == 12,
            'unexpected cancellation error: $e',
          );
        },
      );
      await began.future.timeout(const Duration(seconds: 30));
      final watch = Stopwatch()..start();
      await client.request('cancelOperation');
      await checked;
      final result = await finished.future;
      metrics['${type}CancelMs'] = watch.elapsedMilliseconds;
      metrics['${type}BytesBeforeCancel'] = result.bytes;
      if (type == 'compress')
        verify(
          await File(destination).readAsString() == 'original archive',
          'cancelled writer destroyed original',
        );
      else
        verify(
          !await File('$destination/stream.bin').exists(),
          'cancelled extraction retained partial file',
        );
      await subscription.cancel();
    }
    late BrowserPage opened;
    metrics['millionOpenMs'] = await timed(() async {
      opened = await client.request('open', {'path': args[1]});
    });
    final q = await client.request('query', {'folder': 'many'});
    verify(q.total == 1000000, 'expected one million metadata rows');
    late BrowserPage selection;
    metrics['millionSelectMs'] = await timed(() async {
      selection = await client.request('select', {
        'query': q.query,
        'all': true,
      });
    });
    verify(
      selection.selection != 0 && selection.rows.isEmpty,
      'native selection expanded into Dart',
    );
    metrics['millionSelectionRowsTransferred'] = selection.rows.length;
    metrics['millionSelectionHandle'] = selection.selection;
    final started = Completer<void>();
    final sub = client.progress.listen((p) {
      if (p.operation == 'extractSelected' && !started.isCompleted)
        started.complete();
    });
    final selectedOutput = '${temp.path}/million-selected';
    final extraction = client.request('extractSelected', {
      'archive': opened.archive,
      'query': q.query,
      'selection': selection.selection,
      'destination': selectedOutput,
      'overwrite': 1,
    });
    final checked = extraction.then<void>(
      (_) => throw StateError('Expected selected extraction cancellation'),
      onError: (Object e) {
        verify(
          e is BrowserFailure && e.code == 12,
          'selected extraction cancel failed',
        );
      },
    );
    await started.future.timeout(const Duration(seconds: 10));
    metrics['millionSelectedExtractCancelMs'] = await timed(() async {
      await client.request('cancelOperation');
      await checked;
    });
    if (await Directory(selectedOutput).exists())
      verify(
        await Directory(selectedOutput)
                .list(recursive: true)
                .where((e) => e is File)
                .length ==
            0,
        'selected extraction retained incomplete files',
      );
    await sub.cancel();
    await client.request('close');
    metrics['shutdownMs'] = await timed(client.close);
    metrics['maxRssBytes'] = ProcessInfo.maxRss;
    metrics['runtime'] = Platform.version;
    print('LITEARC_OPERATION_METRICS ${jsonEncode(metrics)}');
  } finally {
    await client.close();
    // Only this generator's uniquely created temporary root is removed.
    await temp.delete(recursive: true);
  }
}
