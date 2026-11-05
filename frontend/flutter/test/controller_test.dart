import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:litearc/controllers/browser_controller.dart';

import 'support/synthetic_transport.dart';

void main() {
  test(
    'byte-limited short pages stop at cache capacity without retry loops',
    () async {
      final c = BrowserController(SyntheticTransport(count: 1000, pageRows: 7));
      await c.open('synthetic').timeout(const Duration(seconds: 2));
      expect(c.pages.length, 8);
      expect(c.cachedRows, 56);
      for (var i = 0; i < 56; i++) {
        expect(c.rowAt(i), isNotNull);
      }
      await c.ensureWindow(56).timeout(const Duration(seconds: 2));
      expect(c.rowAt(56), isNotNull);
      expect(c.cachedRows, 56);
      await c.shutdown();
    },
  );

  test(
    'multi-selection uses current query ranges and resets on navigation',
    () async {
      final c = BrowserController(SyntheticTransport());
      await c.open('synthetic');
      c.choose(2);
      c.choose(5, additive: true);
      expect(c.selection.count, 2);
      expect(c.selection.contains(2), true);
      expect(c.selection.contains(5), true);
      c.choose(8, extend: true);
      expect(c.selection.ranges, [(5, 9)]);
      await c.navigate('new');
      expect(c.selection.count, 0);
      await c.shutdown();
    },
  );
  test('interval selection remains compact and supports holes/merges', () {
    final s = RowSelection()..replace(0, 1000000);
    expect(s.count, 1000000);
    expect(s.ranges.length, 1);
    s.toggle(500);
    expect(s.contains(500), false);
    expect(s.ranges.length, 2);
    s.toggle(500);
    expect(s.ranges.length, 1);
  });
  for (final count in [100000, 1000000]) {
    test(
      '$count synthetic rows: navigation/window cache/selection bounded',
      () async {
        final t = SyntheticTransport(count: count);
        final c = BrowserController(t);
        expect(c.isHome, true);
        expect(t.requests, isEmpty);
        await c.open('synthetic');
        for (final offset in [
          1000,
          50000,
          count - 100,
          0,
          15000,
          20000,
          30000,
          40000,
          60000,
          70000,
          80000,
        ]) {
          await c.ensureWindow(offset);
          expect(c.rowAt(offset), isNotNull);
          expect(c.pages.length, lessThanOrEqualTo(8));
          expect(c.cachedRows, lessThanOrEqualTo(1024));
        }
        c.selectAll();
        expect(c.selection.count, count);
        expect(c.selection.ranges.length, 1);
        await Future<void>.delayed(const Duration(milliseconds: 100));
        expect(c.selectionHandle, 1);
        final oldQuery = c.query;
        await c.setSort(1);
        expect(c.query, isNot(oldQuery));
        expect(c.selection.count, 0);
        expect(c.selectionHandle, 0);
        await c.navigate('empty');
        expect(c.total, 0);
        await c.up();
        expect(c.folder, '');
        await c.closeArchive();
        expect(c.isHome, true);
        expect(c.pages, isEmpty);
        await c.shutdown();
        expect(t.closed, true);
      },
    );
  }
  test(
    'stale pages cannot populate replacement query or disposed controller',
    () async {
      final t = SyntheticTransport();
      final c = BrowserController(t);
      await c.open('synthetic');
      t.gate = Completer<void>();
      final old = c.ensureWindow(500);
      final next = c.navigate('new');
      t.gate!.complete();
      await old;
      await next;
      expect(c.pages.values.every((p) => p.query == c.query), true);
      c.choose(0);
      c.setSearch('new');
      expect(c.selection.count, 0);
      expect(c.query, 0);
      await c.shutdown();
    },
  );
}
