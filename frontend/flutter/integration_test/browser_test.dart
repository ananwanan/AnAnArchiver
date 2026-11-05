import 'dart:ui' show FrameTiming;
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:litearc_binding/litearc_binding.dart';
import 'package:litearc/controllers/browser_controller.dart';
import 'package:litearc/main.dart';
import 'package:litearc/platform/desktop_adapter.dart';

class FixturePicker implements DesktopAdapter {
  @override
  Future<void> setTitle(String title) => SystemDesktopAdapter().setTitle(title);
  @override
  Future<String?> chooseArchive() async =>
      const String.fromEnvironment('FIXTURE');
}

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  testWidgets('real C ABI open and browse on desktop', (tester) async {
    tester.platformDispatcher.localeTestValue = const Locale('en');
    addTearDown(tester.platformDispatcher.clearLocaleTestValue);
    const path = String.fromEnvironment('FIXTURE');
    expect(
      path,
      isNotEmpty,
      reason: 'Pass --dart-define=FIXTURE=<generated real ZIP>',
    );
    final client = await BrowserClient.start();
    final c = BrowserController(client);
    await tester.pumpWidget(
      LiteArcApp(
        controller: c,
        platform: FixturePicker(),
        initialLocale: const Locale('en'),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.text('Your archives, clearly arranged.'), findsOneWidget);
    final watch = Stopwatch()..start();
    await tester.tap(find.text('Open archive').last);
    for (
      var i = 0;
      i < 1000 && (c.busy || c.query == 0 || c.rowAt(0) == null);
      i++
    ) {
      await tester.pump(const Duration(milliseconds: 20));
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    await tester.pumpAndSettle();
    expect(c.error, isNull);
    expect(c.archive, greaterThan(0));
    final openMs = watch.elapsedMilliseconds;
    expect(find.text('CAFÉ.txt'), findsOneWidget);
    final dup = c.pages.values
        .expand((p) => p.rows)
        .where((r) => r.name == 'duplicate.txt')
        .toList();
    expect(dup.length, 2);
    expect(dup[0].index, isNot(dup[1].index));
    await c.navigate('隐式/子目录');
    await tester.pumpAndSettle();
    expect(find.text('可可🐄🐈.txt'), findsOneWidget);
    await c.navigate('empty');
    await tester.pumpAndSettle();
    expect(find.text('No items in this folder'), findsOneWidget);
    await c.navigate('many');
    await tester.pumpAndSettle();
    final count = c.total;
    expect(count, greaterThanOrEqualTo(300));
    c.selectAll();
    await tester.pump(const Duration(milliseconds: 120));
    for (var i = 0; i < 100 && c.selectionHandle == 0; i++) {
      await tester.pump(const Duration(milliseconds: 20));
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    expect(c.selectionHandle, greaterThan(0));
    expect(c.selection.count, count);
    final selected = c.selectionHandle;
    await c.loadFolders('');
    expect(c.selectionHandle, selected);
    final pageWatch = Stopwatch()..start();
    for (final offset in [
      count ~/ 2,
      count - 60,
      0,
      100,
      200,
      300,
      400,
      500,
      600,
      700,
      800,
      900,
    ]) {
      if (offset >= count) continue;
      await c.ensureWindow(offset);
      expect(c.rowAt(offset), isNotNull);
      expect(c.cachedRows, lessThanOrEqualTo(1024));
    }
    final pagingMs = pageWatch.elapsedMilliseconds;
    // Actual ListView scroll events drive bounded native page requests.
    final list = find.byType(ListView).last;
    final frameTimes = <int>[];
    void callback(List<FrameTiming> timings) {
      for (final timing in timings) {
        frameTimes.add(timing.totalSpan.inMicroseconds);
      }
    }

    // Store renderer timings separately: this test is explicitly Profile or
    // Release depending on the invoked Flutter command, never widget timings.
    binding.addTimingsCallback(callback);
    for (var i = 0; i < 30; i++) {
      await tester.drag(list, const Offset(0, -300));
      await tester.pump(const Duration(milliseconds: 30));
    }
    await tester.pumpAndSettle();
    binding.removeTimingsCallback(callback);
    expect(c.cachedRows, lessThanOrEqualTo(1024));
    await c.setSort(1);
    expect(c.selection.count, 0);
    c.setSearch('file-0000001');
    await tester.pump(const Duration(milliseconds: 250));
    for (var i = 0; i < 100 && (c.busy || c.query == 0); i++) {
      await tester.pump(const Duration(milliseconds: 20));
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    expect(c.total, 1);
    final pending = c.refresh();
    await c.cancel();
    await pending;
    expect(c.query, 0);
    await c.closeArchive();
    await tester.pumpAndSettle();
    expect(find.text('Your archives, clearly arranged.'), findsOneWidget);
    final shutdown = Stopwatch()..start();
    await c.shutdown();
    frameTimes.sort();
    final result = {
      'entries': count,
      'openAndFirstViewMs': openMs,
      'samplePagingMs': pagingMs,
      'shutdownMs': shutdown.elapsedMilliseconds,
      'maxRssBytes': ProcessInfo.maxRss,
      'cachePagesLimit': 8,
      'cacheRowsLimit': 1024,
      'frameCount': frameTimes.length,
      'frameTotalP50us': frameTimes.isEmpty
          ? null
          : frameTimes[frameTimes.length ~/ 2],
      'frameTotalP95us': frameTimes.isEmpty
          ? null
          : frameTimes[(frameTimes.length * 0.95).floor()],
    };
    // ignore: avoid_print
    print('LITEARC_DESKTOP_METRICS ${jsonEncode(result)}');
    binding.reportData = result;
    await tester.pumpWidget(const SizedBox());
  });
}
