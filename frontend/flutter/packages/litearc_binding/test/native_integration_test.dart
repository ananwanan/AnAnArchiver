import 'dart:async';
import 'dart:io';
import 'dart:ffi';
import 'dart:convert';

import 'package:crypto/crypto.dart';

import 'package:test/test.dart';
import 'package:litearc_binding/litearc_binding.dart';
import 'package:litearc_binding/src/generated.dart';

void main() {
  test('generated binding is traceable to the current public header', () {
    final record =
        jsonDecode(File('binding-source.json').readAsStringSync()) as Map;
    final source = File('../../../../${record['header']}').readAsBytesSync();
    expect(sha256.convert(source).toString(), record['sha256']);
    final header = utf8.decode(source);
    final generated = File('lib/src/generated.dart').readAsStringSync();
    for (final name in RegExp(
      r'AA_CALL (aa_\w+)\(',
    ).allMatches(header).map((m) => m[1]!)) {
      expect(
        generated.contains("'$name'"),
        isTrue,
        reason: "Missing generated export $name",
      );
    }
  });
  final library = Platform.environment['LITEARC_TEST_LIBRARY'];
  final fixture = Platform.environment['LITEARC_TEST_ARCHIVE'];
  final longFixture = Platform.environment['LITEARC_TEST_LONG_ARCHIVE'];
  test('ABI v1 fixed-width layouts match the native C consumer', () {
    expect(sizeOf<aa_api_info>(), 32);
    expect(sizeOf<aa_string>(), 16);
    expect(sizeOf<aa_entry>(), 64);
    expect(sizeOf<aa_browser_row>(), 96);
    expect(sizeOf<aa_browser_result>(), 88);
    expect(sizeOf<aa_browser_query_request>(), 56);
    expect(sizeOf<aa_browser_page_request>(), 40);
    expect(sizeOf<aa_browser_folder_page_request>(), 48);
    expect(sizeOf<aa_browser_select_request>(), 40);
  });
  group(
    'real archive through persistent isolate and C ABI',
    () {
      late BrowserClient client;
      setUp(() async {
        client = await BrowserClient.start(library: library);
      });
      tearDown(() async {
        await client.close();
      });
      Future<BrowserPage> open() async {
        final r = await client.request('open', {'path': fixture});
        expect(r.archive, greaterThan(0));
        expect(r.entryCount, greaterThan(300));
        return r;
      }

      Future<BrowserPage> query([String folder = '', String search = '']) =>
          client.request('query', {'folder': folder, 'search': search});
      Future<BrowserPage> page(int q, [int offset = 0]) =>
          client.request('page', {'query': q, 'offset': offset});
      test(
        'real UTF-8 long names produce byte-limited partial pages without gaps',
        () async {
          await client.request('open', {'path': longFixture});
          final q = await query('long');
          expect(q.total, 64);
          final first = await page(q.query);
          expect(first.rows.length, allOf(greaterThan(0), lessThan(64)));
          expect(first.transferBytes, lessThanOrEqualTo(AA_MAX_PAGE_BYTES));
          final next = await page(q.query, first.rows.length);
          expect(first.rows.length + next.rows.length, 64);
          expect(next.rows.first.index, first.rows.last.index + 1);
        },
        skip: longFixture == null
            ? 'Set LITEARC_TEST_LONG_ARCHIVE for byte-limited real pages'
            : false,
      );
      test('content detection, Unicode, implicit/empty folders and duplicate ordinals', () async {
        await open();
        final q = await query();
        final p = await page(q.query);
        expect(p.rows.take(3).every((r) => r.folder), isTrue);
        final implicit = p.rows.firstWhere((r) => r.name == '隐式');
        expect(
          implicit.index,
          -1,
        ); // UINT64_MAX represented as signed Dart int.
        final duplicates = p.rows
            .where((r) => r.name == 'duplicate.txt')
            .toList();
        expect(duplicates.length, 2);
        expect(duplicates.map((r) => r.index).toSet().length, 2);
        final empty = await query('empty');
        expect(empty.total, 0);
        final unicode = await query('隐式/子目录');
        expect((await page(unicode.query)).rows.single.name, '可可🐄🐈.txt');
        final folded = await query('', 'café');
        expect((await page(folded.query)).rows.single.name, 'CAFÉ.txt');
      });
      test(
        'paging, native select-all and folder windows preserve list selection',
        () async {
          await open();
          final q = await query('many');
          expect(q.total, 300);
          final p = await page(q.query, 128);
          expect(p.rows.length, 128);
          expect(p.offset, 128);
          final selected = await client.request('select', {
            'query': q.query,
            'all': true,
          });
          expect(selected.selection, greaterThan(0));
          final tree = await client.request('folders', {'folder': ''});
          expect(tree.query, 0);
          expect(tree.rows.every((r) => r.folder), isTrue);
          expect((await page(q.query, 256)).rows.length, 44);
          final sort = await client.request('query', {
            'folder': 'many',
            'sort': 1,
            'descending': true,
          });
          expect(sort.query, isNot(q.query));
          await expectLater(
            page(q.query),
            throwsA(isA<BrowserFailure>().having((e) => e.status, 'status', 9)),
          );
        },
      );
      test('bounded rapid replacement, cancellation, close and idempotent shutdown', () async {
        await open();
        final futures = <Future<void>>[];
        for (var i = 0; i < 12; i++) {
          futures.add(
            query('many', '$i').then<void>(
              (_) {},
              onError: (Object e) {
                expect(e, isA<BrowserFailure>());
              },
            ),
          );
        }
        await Future.wait(futures);
        await client.request('cancel');
        await client.request('close');
        await expectLater(query(), throwsA(isA<BrowserFailure>()));
        await open();
        final pending = query('many');
        final checked = pending.then<void>(
          (_) {},
          onError: (Object e) {
            expect(e, isA<BrowserFailure>());
          },
        );
        await client.close();
        await checked;
        await client.close();
        await expectLater(query(), throwsA(isA<BrowserFailure>()));
      });
      test('structured missing-file error crosses isolate without pointer ownership', () async {
        await expectLater(
          client.request('open', {'path': '$fixture.missing'}),
          throwsA(
            isA<BrowserFailure>().having(
              (e) => e.code,
              'code',
              AA_ERROR_FILE_NOT_FOUND,
            ),
          ),
        );
      });
    },
    skip: library == null || fixture == null
        ? 'Set LITEARC_TEST_LIBRARY and LITEARC_TEST_ARCHIVE for real C ABI tests'
        : false,
  );
  test(
    'failed library load completes startup with a structured error',
    () async {
      await expectLater(
        BrowserClient.start(library: 'nonexistent-litearc-library'),
        throwsA(isA<BrowserFailure>()),
      );
    },
  );
}
