import 'dart:async';
import 'dart:io';

import 'package:litearc_binding/litearc_binding.dart';
import 'package:test/test.dart';

void main() {
  final library = Platform.environment['LITEARC_TEST_LIBRARY'];
  final archive = Platform.environment['LITEARC_TEST_ARCHIVE'];
  group(
    'real desktop C ABI',
    () {
      late BrowserClient client;
      late Directory temp;
      setUp(() async {
        client = await BrowserClient.start(library: library);
        temp = await Directory.systemTemp.createTemp('litearc-f5-');
      });
      tearDown(() async {
        await client.close();
        await temp.delete(recursive: true);
      });
      test(
        'content-based drop classification, missing path and directory inputs',
        () async {
          expect(
            (await client.request('probe', {'path': archive})).classification,
            'archive',
          );
          expect(
            (await client.request('probe', {'path': temp.path})).classification,
            'inputs',
          );
          final plain = File('${temp.path}/plain.zip');
          await plain.writeAsString('not an archive');
          expect(
            (await client.request('probe', {
              'path': plain.path,
            })).classification,
            'inputs',
          );
          await expectLater(
            client.request('probe', {'path': '${temp.path}/missing'}),
            throwsA(isA<BrowserFailure>()),
          );
        },
      );
      test('native selection, prepared output, durable shutdown/delayed read/restart and explicit release', () async {
        final opened = await client.request('open', {'path': archive});
        final q = await client.request('query', {'search': 'duplicate'});
        final selection = await client.request('select', {
          'query': q.query,
          'ranges': [
            [1, 1],
          ],
        });
        final root = (await client.request('exportControl', {
          'action': 1,
          'path': temp.path,
        })).paths.single;
        final observed = <TaskProgress>[];
        final sub = client.taskEvents.listen(observed.add);
        await client.request('export', {
          'archive': opened.archive,
          'query': q.query,
          'selection': selection.selection,
          'destination': root,
        });
        final paths = (await client.request('exportControl', {
          'action': 2,
          'path': root,
        })).paths;
        expect(paths.length, 1);
        expect(await File(paths.single).readAsString(), 'second');
        await client.request('exportControl', {'action': 3, 'path': root});
        await client.request('close');
        await client.close();
        await sub.cancel();
        expect(await File(paths.single).readAsString(), 'second');
        expect(
          observed.where((p) => p.operation == 'export' && p.state == 5),
          isNotEmpty,
        );
        client = await BrowserClient.start(library: library);
        expect(
          (await client.request('exportControl', {
            'action': 7,
            'path': temp.path,
          })).paths,
          [root],
        );
        await client.request('exportControl', {'action': 4, 'path': root});
        for (var i = 0; i < 100 && await Directory(root).exists(); i++) {
          await Future<void>.delayed(const Duration(milliseconds: 20));
        }
        expect(await Directory(root).exists(), false);
      });
      test('rapid replacement terminal tasks and repeated release stay below engine capacity', () async {
        final history = <TaskProgress>[];
        final sub = client.taskEvents.listen(history.add);
        for (var i = 0; i < 24; i++) {
          await client.request('open', {'path': archive});
          final old = client.request('query', {'search': 'stale-$i'});
          final outcome = old.then<Object>((r) => r, onError: (Object e) => e);
          await client.request('query', {'search': 'duplicate'});
          await outcome;
        }
        await Future<void>.delayed(Duration.zero);
        final latest = <int, TaskProgress>{
          for (final p in history) p.taskId: p,
        };
        expect(latest.values.every((p) => p.terminal), true);
        await sub.cancel();
      });
    },
    skip: library == null || archive == null
        ? 'Set real library/archive paths'
        : false,
    timeout: const Timeout(Duration(minutes: 3)),
  );
}
