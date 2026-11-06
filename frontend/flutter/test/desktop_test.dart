import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:litearc/controllers/browser_controller.dart';
import 'package:litearc/controllers/desktop_controller.dart';
import 'package:litearc/controllers/task_history_controller.dart';
import 'package:litearc/main.dart';
import 'package:litearc_binding/litearc_binding.dart';

import 'support/synthetic_transport.dart';
import 'widget_test.dart' show Picker;

class SlowOpen extends SyntheticTransport {
  final started = <String>[];
  Completer<BrowserPage>? pending;
  @override
  Future<BrowserPage> request(
    String op, [
    Map<String, Object?> args = const {},
  ]) {
    if (op == 'open') {
      started.add(args['path'] as String);
      if (started.length == 1) {
        return (pending = Completer<BrowserPage>()).future;
      }
    }
    if (op == 'cancel') {
      if (pending?.isCompleted == false) {
        pending!.completeError(const BrowserFailure(9, 12, 'superseded'));
      }
    }
    return super.request(op, args);
  }
}

class ExportRace extends SyntheticTransport {
  final handoffGate = Completer<BrowserPage>();
  final discarded = <String>[];
  @override
  Future<BrowserPage> request(
    String op, [
    Map<String, Object?> args = const {},
  ]) {
    if (op == 'exportControl') {
      if (args['action'] == 3) return handoffGate.future;
      if (args['action'] == 4) discarded.add(args['path'] as String);
      return Future.value(const BrowserPage(paths: ['private-root']));
    }
    return super.request(op, args);
  }
}

void main() {
  String absolute(String name) =>
      '${Directory.systemTemp.path}${Platform.pathSeparator}$name';
  test(
    'navigation during handoff never discards output already offered to OS',
    () async {
      final t = ExportRace();
      final c = BrowserController(t);
      await c.open('synthetic');
      c.selectAll();
      await Future<void>.delayed(const Duration(milliseconds: 100));
      final e = ExportController(c, 'private-parent');
      await e.prepare();
      expect(e.paths, ['private-root']);
      final handed = e.handoff();
      await c.closeArchive();
      t.handoffGate.complete(const BrowserPage());
      expect(await handed, ['private-root']);
      expect(t.discarded, isEmpty);
      e.dispose();
      c.dispose();
    },
  );
  test('external open coalesces duplicates/latest pending, rejects multi and closes pending events', () async {
    final t = SlowOpen();
    final c = BrowserController(t);
    final d = DesktopOpenController(c);
    d.openFiles([absolute('a.xxx')]);
    await Future<void>.delayed(Duration.zero);
    d.openFiles([absolute('a.xxx')]);
    d.openFiles([absolute('b.xxx')]);
    d.openFiles([absolute('c.xxx')]);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(t.started, [absolute('a.xxx'), absolute('c.xxx')]);
    expect(c.path, absolute('c.xxx'));
    d.openFiles([absolute('a'), absolute('b')]);
    expect(c.operations.error?.code, 5);
    d.close();
    d.openFiles([absolute('d')]);
    expect(t.started.length, 2);
    c.dispose();
  });
  test(
    'task history is bounded frontend summaries without stale conflicts',
    () async {
      final events = StreamController<TaskProgress>.broadcast(sync: true);
      final h = TaskHistoryController(events.stream);
      h.destinationFor = (p) => '/output-${p.taskId}';
      for (var i = 1; i <= 100; i++) {
        events.add(
          TaskProgress(
            'compress',
            5,
            i,
            i,
            taskId: i,
            conflict: const TaskConflict(7, 'a', 'b'),
          ),
        );
      }
      expect(h.tasks.length, 32);
      expect(h.destinations.length, 32);
      expect(h.tasks.values.every((p) => p.conflict == null), true);
      events.add(const TaskProgress('extractAll', 2, 5, 80, taskId: 101));
      h.clear();
      expect(h.tasks.keys, [101]);
      expect(h.destinations, {101: '/output-101'});
      h.dispose();
      await events.close();
    },
  );
  testWidgets(
    'shortcut focus: query select-all, search text, dialog Escape and operation password',
    (tester) async {
      tester.view.physicalSize = const Size(1280, 720);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final t = SyntheticTransport(count: 1000);
      final c = BrowserController(t);
      await c.open('synthetic');
      await tester.pumpWidget(
        LiteArcApp(
          controller: c,
          platform: Picker(),
          initialLocale: const Locale('en'),
        ),
      );
      await tester.pumpAndSettle();
      Future<void> shortcut(LogicalKeyboardKey key) async {
        await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
        await tester.sendKeyEvent(key);
        await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
        await tester.pumpAndSettle();
      }

      await tester.tap(find.text('file-0.txt'));
      await shortcut(LogicalKeyboardKey.keyA);
      expect(c.selection.count, 1000);
      await shortcut(LogicalKeyboardKey.keyF);
      await tester.enterText(find.byKey(const Key('archive-search')), 'typed');
      await tester.pumpAndSettle();
      await shortcut(LogicalKeyboardKey.keyA);
      expect(c.selection.count, 0);
      final before = t.requests
          .where((v) => v == 'cancel' || v == 'cancelOperation')
          .length;
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(
        t.requests.where((v) => v == 'cancel' || v == 'cancelOperation').length,
        before,
      );
      await tester.tap(find.byTooltip('Task center'));
      await tester.pumpAndSettle();
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(find.byType(AlertDialog), findsNothing);
      final password = c.operations.requestPassword(
        const BrowserFailure(8, 9, 'required'),
      );
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const Key('operation-password')),
        'secret',
      );
      await shortcut(LogicalKeyboardKey.keyA);
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(c.operations.waitingForPassword, true);
      expect(c.selection.count, 0);
      c.operations.answerPassword(null);
      await password;
      await tester.pumpWidget(const SizedBox());
    },
  );
}
