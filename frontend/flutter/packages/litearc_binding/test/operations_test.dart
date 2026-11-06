import 'dart:async';
import 'dart:io';

import 'package:litearc_binding/litearc_binding.dart';
import 'package:test/test.dart';

void main() {
  final library = Platform.environment['LITEARC_TEST_LIBRARY'];
  final fixture = Platform.environment['LITEARC_TEST_ARCHIVE'];
  final fixtures = Platform.environment['LITEARC_TEST_OPERATIONS'];
  group(
    'real operation lane / ABI v1',
    () {
      late BrowserClient client;
      late Directory temp;
      setUp(() async {
        client = await BrowserClient.start(library: library);
        temp = await Directory.systemTemp.createTemp('litearc-f4-');
      });
      tearDown(() async {
        await client.close();
        await temp.delete(recursive: true);
      });
      Map<String, Object?> extraction(
        String source,
        String output, [
        int policy = 1,
      ]) => {
        'path': source,
        'destination': '${temp.path}/$output',
        'overwrite': policy,
      };
      final failed = isA<BrowserFailure>();
      test('extract-all ignores filter; native duplicate/implicit/empty selection; stale selection fails', () async {
        final opened = await client.request('open', {'path': fixture});
        var q = await client.request('query', {'search': 'CAFÉ'});
        await client.request('extractAll', extraction(fixture!, 'all'));
        expect(
          await File('${temp.path}/all/duplicate.txt').readAsString(),
          'second',
        );
        expect(
          await File('${temp.path}/all/隐式/子目录/可可🐄🐈.txt').readAsString(),
          'hello',
        );
        expect(await Directory('${temp.path}/all/empty').exists(), isTrue);
        q = await client.request('query', {
          'search': 'duplicate',
          'sort': 1,
          'descending': true,
        });
        final p = await client.request('page', {'query': q.query, 'offset': 0});
        expect(p.rows.first.size, 6);
        final selected = await client.request('select', {
          'query': q.query,
          'ranges': [
            [0, 1],
          ],
        });
        Future<BrowserPage> extractSelected(
          BrowserPage selection,
          String output,
        ) => client.request('extractSelected', {
          'archive': opened.archive,
          'query': selection.query,
          'selection': selection.selection,
          'destination': '${temp.path}/$output',
          'overwrite': 1,
        });
        await extractSelected(selected, 'duplicate');
        expect(
          await File('${temp.path}/duplicate/duplicate.txt').readAsString(),
          'second',
        );
        q = await client.request('query', {'search': '隐式'});
        await expectLater(extractSelected(selected, 'stale'), throwsA(failed));
        expect(await Directory('${temp.path}/stale').exists(), isFalse);
        var s = await client.request('select', {'query': q.query, 'all': true});
        await extractSelected(s, 'implicit');
        expect(
          await File('${temp.path}/implicit/隐式/子目录/可可🐄🐈.txt').readAsString(),
          'hello',
        );
        expect(await File('${temp.path}/implicit/CAFÉ.txt').exists(), isFalse);
        q = await client.request('query', {'search': 'empty'});
        s = await client.request('select', {'query': q.query, 'all': true});
        await extractSelected(s, 'empty');
        expect(await Directory('${temp.path}/empty/empty').exists(), isTrue);
        await expectLater(
          client.request('extractSelected', {
            'archive': opened.archive,
            'query': q.query,
            'selection': 0,
            'destination': '${temp.path}/invalid',
          }),
          throwsA(failed),
        );
      });

      test('password required, wrong, retry, cancellation; unsafe output rolls back', () async {
        final args = extraction('$fixtures/encrypted.zip', 'decrypted');
        await expectLater(
          client.request('extractAll', args),
          throwsA(failed.having((e) => e.code, 'required', 9)),
        );
        args['password'] = 'wrong-test-password';
        await expectLater(
          client.request('extractAll', args),
          throwsA(
            failed
                .having((e) => e.code, 'wrong', 8)
                .having(
                  (e) => '${e.message}${e.backend}${e.path}'.contains(
                    'wrong-test-password',
                  ),
                  'no password in diagnostics',
                  false,
                ),
          ),
        );
        args['password'] = 'test-only-passphrase';
        await client.request('extractAll', args);
        args.remove('password');
        expect(
          await File('${temp.path}/decrypted/secret.txt').readAsString(),
          'secret',
        );
        for (final name in ['unsafe.tar', 'unsafe-link.tar']) {
          await expectLater(
            client.request('extractAll', extraction('$fixtures/$name', name)),
            throwsA(failed.having((e) => e.code, 'unsafe', 16)),
          );
          expect(await File('${temp.path}/escape.txt').exists(), isFalse);
          expect(await File('${temp.path}/$name/safe.txt').exists(), isFalse);
        }
        await client.request('cancelOperation');
      });

      for (final policy in [1, 2, 3, 4]) {
        test(
          'direct overwrite policy $policy and timestamp behavior',
          () async {
            final destination = Directory('${temp.path}/policy');
            await destination.create();
            for (final name in ['a', 'b']) {
              final file = File('${destination.path}/$name.txt');
              await file.writeAsString('original-$name');
              if (policy == 4 && name == 'b')
                await file.setLastModified(DateTime.utc(2000));
            }
            await client.request(
              'extractAll',
              extraction('$fixtures/policies.tar', 'policy', policy),
            );
            expect(
              await File('${destination.path}/a.txt').readAsString(),
              policy == 1 ? 'archive-a' : 'original-a',
            );
            expect(
              await File('${destination.path}/b.txt').readAsString(),
              policy == 1 || policy == 4 ? 'archive-b' : 'original-b',
            );
            if (policy == 3) {
              expect(await destination.list().length, 4);
              final contents = await Future.wait(
                (await destination.list().toList()).cast<File>().map(
                  (f) => f.readAsString(),
                ),
              );
              expect(contents, containsAll(['archive-a', 'archive-b']));
            }
          },
        );
      }

      for (final policy in [1, 2, 3, 4]) {
        test(
          'Ask answer $policy applies only to this task, including subsequent conflicts',
          () async {
            await Directory('${temp.path}/ask-all').create();
            await File('${temp.path}/ask-all/a.txt')
                .writeAsString('original-a');
            await File('${temp.path}/ask-all/b.txt')
                .writeAsString('original-b');
            final observed = Completer<TaskProgress>();
            final ids = <int>{};
            final sub = client.progress.listen((p) {
              if (p.conflict != null) {
                ids.add(p.conflict!.id);
                if (!observed.isCompleted) observed.complete(p);
              }
            });
            final running = client.request(
              'extractAll',
              extraction('$fixtures/policies.tar', 'ask-all', 0),
            );
            final snapshot = await observed.future.timeout(
              const Duration(seconds: 5),
            );
            await client.request('resolveConflict', {
              'task': snapshot.taskId,
              'conflict': snapshot.conflict!.id,
              'overwrite': policy,
              'all': true,
            });
            await running;
            expect(ids.length, 1);
            expect(
              await File('${temp.path}/ask-all/b.txt').readAsString(),
              policy == 1 ? 'archive-b' : 'original-b',
            );
            if (policy == 3)
              expect(await Directory('${temp.path}/ask-all').list().length, 4);
            await sub.cancel();
            // A new task must ask again, rather than retaining the previous answer.
            final next = Completer<TaskProgress>();
            final sub2 = client.progress.listen((p) {
              if (p.conflict != null && !next.isCompleted) next.complete(p);
            });
            final cancelled = client.request(
              'extractAll',
              extraction('$fixtures/policies.tar', 'ask-all', 0),
            );
            final checked = expectLater(cancelled, throwsA(failed));
            await next.future.timeout(const Duration(seconds: 5));
            await client.request('cancelOperation');
            await checked;
            await sub2.cancel();
          },
        );
      }

      Future<TaskProgress> conflict(Completer<TaskProgress> observed) =>
          observed.future.timeout(const Duration(seconds: 10));
      test('Ask: browse while waiting, exact/stale/repeated IDs, apply-to-all, release', () async {
        await client.request('open', {'path': fixture});
        await Directory('${temp.path}/ask').create();
        await File('${temp.path}/ask/a.txt').writeAsString('original');
        await File('${temp.path}/ask/b.txt').writeAsString('original');
        final observed = Completer<TaskProgress>();
        final conflicts = <int>{};
        final subscription = client.progress.listen((p) {
          if (p.conflict != null) {
            conflicts.add(p.conflict!.id);
            if (!observed.isCompleted) observed.complete(p);
          }
        });
        final operation = client.request(
          'extractAll',
          extraction('$fixtures/policies.tar', 'ask', 0),
        );
        final p = await conflict(observed);
        await Future.wait(
          List.generate(
            12,
            (i) => client
                .request('query', {'search': '$i'})
                .then<void>(
                  (_) {},
                  onError: (Object e) {
                    expect(
                      e,
                      isA<BrowserFailure>().having(
                        (f) => f.status,
                        'superseded query',
                        9,
                      ),
                    );
                  },
                ),
          ),
        );
        final q = await client
            .request('query', {'search': 'CAFÉ'})
            .timeout(const Duration(seconds: 3));
        expect(q.total, 1);
        final args = <String, Object?>{
          'task': p.taskId,
          'conflict': p.conflict!.id + 1,
          'overwrite': 1,
          'all': true,
        };
        await expectLater(
          client.request('resolveConflict', args),
          throwsA(failed.having((e) => e.status, 'stale conflict', 4)),
        );
        args['conflict'] = p.conflict!.id;
        await client.request('resolveConflict', args);
        await expectLater(
          client.request('resolveConflict', args),
          throwsA(failed),
        );
        await operation;
        expect(conflicts.length, 1);
        expect(
          await File('${temp.path}/ask/b.txt').readAsString(),
          'archive-b',
        );
        await expectLater(
          client.request('resolveConflict', args),
          throwsA(failed),
        );
        await subscription.cancel();
        // Exceeds native retained-task capacity cumulatively; each must be released.
        for (var i = 0; i < 24; i++) {
          await client.request(
            'extractAll',
            extraction('$fixtures/policies.tar', 'repeat', 2),
          );
        }
      });

      for (final action in ['cancelOperation', 'close', 'switch', 'shutdown']) {
        test(
          'interaction waiting: $action never queues behind extraction',
          () async {
            await client.request('open', {'path': fixture});
            await Directory('${temp.path}/wait').create();
            await File('${temp.path}/wait/a.txt').writeAsString('original');
            final observed = Completer<TaskProgress>();
            final sub = client.progress.listen((p) {
              if (p.conflict != null && !observed.isCompleted)
                observed.complete(p);
            });
            final operation = client.request(
              'extractAll',
              extraction('$fixtures/policies.tar', 'wait', 0),
            );
            final checked = expectLater(operation, throwsA(failed));
            await conflict(observed);
            final watch = Stopwatch()..start();
            if (action == 'shutdown') {
              await client.close().timeout(const Duration(seconds: 5));
            } else if (action == 'switch') {
              await client
                  .request('open', {'path': fixture})
                  .timeout(const Duration(seconds: 5));
              expect((await client.request('query')).query, isNonZero);
            } else {
              await client.request(action).timeout(const Duration(seconds: 5));
            }
            await checked;
            expect(watch.elapsedMilliseconds, lessThan(5000));
            expect(
              await File('${temp.path}/wait/a.txt').readAsString(),
              'original',
            );
            expect(await File('${temp.path}/wait/b.txt').exists(), isFalse);
            await sub.cancel();
          },
        );
      }

      test('every advertised native writer pair creates/reopens/extracts; failure preserves original; request limits', () async {
        final caps = (await client.request('capabilities')).capabilities;
        expect(caps, isNotEmpty);
        expect(
          caps
              .map((c) => c.formatName.toLowerCase())
              .any((n) => n.contains('rar')),
          isFalse,
        );
        final input = File('${temp.path}/输入🐈.txt');
        await input.writeAsString('roundtrip Unicode');
        final empty = Directory('${temp.path}/empty');
        await empty.create();
        for (var i = 0; i < caps.length; i++) {
          final c = caps[i];
          final destination = '${temp.path}/created-$i.${c.extension}';
          final args = <String, Object?>{
            'paths': [input.path, empty.path],
            'destination': destination,
            'format': c.format,
            'method': c.method,
            'level': c.defaultLevel,
            'permissions': c.permissions,
            'timestamps': c.timestamps,
          };
          await client.request('compress', args);
          await client.request('open', {'path': destination});
          final q = await client.request('query');
          expect(q.total, 2);
          await client.request(
            'extractAll',
            extraction(destination, 'roundtrip-$i'),
          );
          expect(
            await File('${temp.path}/roundtrip-$i/输入🐈.txt').readAsString(),
            'roundtrip Unicode',
          );
          expect(
            await Directory('${temp.path}/roundtrip-$i/empty').exists(),
            isTrue,
          );
          final original = await File(destination).readAsBytes();
          args['replace'] = true;
          args['paths'] = ['${temp.path}/missing'];
          await expectLater(client.request('compress', args), throwsA(failed));
          expect(await File(destination).readAsBytes(), original);
        }
        final c = caps.first;
        final args = <String, Object?>{
          'paths': List.filled(4097, input.path),
          'destination': '${temp.path}/limit.zip',
          'format': c.format,
          'method': c.method,
          'level': c.defaultLevel,
        };
        await expectLater(
          client.request('compress', args),
          throwsA(failed.having((e) => e.code, 'count limit', 14)),
        );
        args['paths'] = List.filled(100, 'a' * 50000);
        await expectLater(
          client.request('compress', args),
          throwsA(failed.having((e) => e.code, 'byte limit', 14)),
        );
        expect(await File('${temp.path}/limit.zip').exists(), isFalse);
      });
    },
    skip: library == null || fixture == null || fixtures == null
        ? 'Set LITEARC_TEST_LIBRARY/ARCHIVE/OPERATIONS for real ABI operation tests'
        : false,
    timeout: const Timeout(Duration(minutes: 2)),
  );
}
