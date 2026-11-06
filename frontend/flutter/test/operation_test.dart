import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:litearc/controllers/browser_controller.dart';
import 'package:litearc/controllers/operation_controller.dart';
import 'package:litearc/l10n/app_localizations.dart';
import 'package:litearc/platform/desktop_adapter.dart';
import 'package:litearc/widgets/create_archive_dialog.dart';
import 'package:litearc/widgets/operation_panel.dart';
import 'package:litearc_binding/litearc_binding.dart';

import 'support/synthetic_transport.dart';

class OperationTransport extends SyntheticTransport {
  OperationTransport() : super(count: 1000000);
  final events = StreamController<TaskProgress>.broadcast(sync: true);
  final calls = <(String, Map<String, Object?>)>[];
  Future<BrowserPage> Function(String, Map<String, Object?>)? operation;
  Completer<BrowserPage>? cancelGate;
  @override
  Stream<TaskProgress> get progress => events.stream;
  @override
  Future<BrowserPage> request(
    String op, [
    Map<String, Object?> args = const {},
  ]) {
    calls.add((op, Map.of(args)..remove('password')));
    if (op == 'cancelOperation' && cancelGate != null) {
      return cancelGate!.future;
    }
    if (op == 'extractAll' || op == 'extractSelected' || op == 'compress') {
      return operation?.call(op, args) ?? Future.value(const BrowserPage());
    }
    return super.request(op, args);
  }
}

class OperationPicker implements DesktopAdapter {
  @override
  Future<String?> chooseArchive() async => null;
  @override
  Future<void> setTitle(String title) async {}
  @override
  Future<String?> chooseDirectory() async => 'directory-input';
  @override
  Future<List<String>> chooseInputs() async => ['file-input'];
  @override
  Future<String?> chooseOutput(String suggestedName) async => suggestedName;
}

Widget localized(Widget child) => MaterialApp(
  localizationsDelegates: AppLocalizations.localizationsDelegates,
  supportedLocales: AppLocalizations.supportedLocales,
  locale: const Locale('en'),
  home: Scaffold(body: child),
);

void main() {
  test('dispose during cancellation acknowledgement cannot reopen or notify a dead view', () async {
    final t = OperationTransport();
    final c = BrowserController(t);
    await c.open('synthetic');
    final done = Completer<BrowserPage>();
    t.operation = (_, _) => done.future;
    final running = c.operations.run('compress', {'destination': 'output'});
    t.cancelGate = Completer<BrowserPage>();
    final replacement = c.open('replacement');
    await Future<void>.delayed(Duration.zero);
    c.dispose();
    t.cancelGate!.complete(const BrowserPage());
    await replacement;
    done.completeError(const BrowserFailure(8, 12, 'cancelled'));
    await running;
    c.operations.answerPassword(null);
    await c.closeArchive();
    await c.cancel();
    expect(t.calls.where((call) => call.$1 == 'open').length, 1);
    expect(t.closed, isTrue);
  });
  test('cancel before a snapshot preserves the terminal error and cleanup diagnosis', () async {
    final t = OperationTransport();
    final c = OperationController(t);
    final done = Completer<BrowserPage>();
    t.operation = (_, _) => done.future;
    final running = c.run('extractAll', {'destination': 'output'});
    await c.cancel();
    done.completeError(
      const BrowserFailure(
        8,
        13,
        'cleanup failed',
        'raw diagnostic',
        'output/partial',
      ),
    );
    await running;
    expect(c.error?.code, 13);
    expect(c.error?.path, 'output/partial');
    c.dispose();
  });
  test('selected extraction requires completed native handle, million select-all remains bounded, all independent of search', () async {
    final t = OperationTransport();
    final c = BrowserController(t);
    await c.open('synthetic');
    c.selectAll();
    await c.extract('output', all: false);
    expect(t.calls.where((r) => r.$1 == 'extractSelected'), isEmpty);
    c.error = null;
    await Future<void>.delayed(const Duration(milliseconds: 150));
    expect(c.canExtractSelected, isTrue);
    await c.extract('output', all: false);
    final args = t.calls.lastWhere((r) => r.$1 == 'extractSelected').$2;
    expect(args['selection'], 1);
    expect(args.containsKey('ranges'), isFalse);
    expect(args.containsKey('indices'), isFalse);
    c.setSearch('filtered');
    await c.extract('all', all: true);
    expect(
      t.calls.lastWhere((r) => r.$1 == 'extractAll').$2['path'],
      'synthetic',
    );
    expect(c.canExtractSelected, isFalse);
    await c.shutdown();
  });

  test('password required / wrong / retry clears request references; password cancel and dispose finish', () async {
    final t = OperationTransport();
    final c = OperationController(t);
    var attempts = 0;
    t.operation = (op, args) async {
      attempts++;
      if (args['password'] == 'correct') return const BrowserPage();
      throw BrowserFailure(
        8,
        args['password'] == null ? 9 : 8,
        'password error',
      );
    };
    final options = <String, Object?>{'destination': 'output'};
    final run = c.run('extractAll', options);
    await Future<void>.delayed(Duration.zero);
    expect(c.passwordError?.code, 9);
    c.answerPassword('wrong');
    await Future<void>.delayed(Duration.zero);
    expect(c.passwordError?.code, 8);
    c.answerPassword('correct');
    await run;
    expect(attempts, 3);
    expect(options.containsKey('password'), isFalse);
    expect(c.busy, isFalse);
    final cancel = c.run('extractAll', {'destination': 'output'});
    await Future<void>.delayed(Duration.zero);
    await c.cancel();
    await cancel;
    expect(c.error?.code, 12);
    final disposing = c.run('extractAll', {'destination': 'output'});
    await Future<void>.delayed(Duration.zero);
    c.dispose();
    await disposing;
  });

  test('operation snapshot is independent of browser progress; conflict identity and terminal success after cancellation race', () async {
    final t = OperationTransport();
    final c = OperationController(t);
    final done = Completer<BrowserPage>();
    t.operation = (_, _) => done.future;
    final running = c.run('extractAll', {'destination': 'output'});
    const snapshot = TaskProgress(
      'extractAll',
      2,
      3,
      20,
      taskId: 19,
      conflict: TaskConflict(27, 'a', 'output/a'),
    );
    t.events.add(snapshot);
    t.events.add(const TaskProgress('query', 5, 1, 10));
    expect(c.progress, snapshot);
    await c.resolve(snapshot, 3, true);
    expect(t.calls.last.$2, {
      'task': 19,
      'conflict': 27,
      'overwrite': 3,
      'all': true,
    });
    await c.cancel();
    t.events.add(const TaskProgress('extractAll', 5, 4, 30, taskId: 19));
    done.complete(const BrowserPage());
    await running;
    expect(c.progress?.state, 5);
    expect(c.error, isNull);
    await expectLater(
      c.resolve(snapshot, 1, false),
      throwsA(isA<BrowserFailure>()),
    );
    c.dispose();
  });

  testWidgets(
    'external cancellation clears a pending password buffer synchronously',
    (tester) async {
      final c = OperationController(OperationTransport());
      final answer = c.requestPassword(const BrowserFailure(8, 9, 'required'));
      await tester.pumpWidget(
        localized(
          AnimatedBuilder(animation: c, builder: (_, _) => OperationPanel(c)),
        ),
      );
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const Key('operation-password')),
        'temporary-fixture-input',
      );
      final buffer = tester
          .widget<TextField>(find.byKey(const Key('operation-password')))
          .controller!;
      expect(buffer.text.isNotEmpty, isTrue);
      await c.cancel();
      expect(buffer.text.isEmpty, isTrue);
      expect(await answer, isNull);
      await tester.pumpWidget(const SizedBox());
      c.dispose();
    },
  );
  testWidgets(
    'password input clears on submit; cancel while waiting finishes the operation',
    (tester) async {
      final t = OperationTransport();
      final c = OperationController(t);
      t.operation = (_, _) async =>
          throw const BrowserFailure(8, 9, 'required');
      final running = c.run('extractAll', {'destination': 'output'});
      await tester.pumpWidget(
        localized(
          AnimatedBuilder(animation: c, builder: (_, _) => OperationPanel(c)),
        ),
      );
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const Key('operation-password')),
        'temporary-password',
      );
      await tester.tap(find.text('Retry'));
      await tester.pumpAndSettle();
      final input = tester.widget<TextField>(
        find.byKey(const Key('operation-password')),
      );
      expect(input.controller!.text, isEmpty);
      expect(input.obscureText, isTrue);
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      await running;
      expect(c.error?.code, 12);
      await tester.pumpWidget(const SizedBox());
      c.dispose();
    },
  );

  testWidgets(
    'unknown totals are indeterminate; conflict answer and task apply-to-all preserve IDs',
    (tester) async {
      final t = OperationTransport();
      final c = OperationController(t);
      final done = Completer<BrowserPage>();
      t.operation = (_, _) => done.future;
      final running = c.run('extractAll', {'destination': 'output'});
      t.events.add(
        const TaskProgress(
          'extractAll',
          2,
          2,
          50,
          taskId: 5,
          conflict: TaskConflict(3, 'a', 'output/a'),
        ),
      );
      await tester.pumpWidget(
        localized(
          AnimatedBuilder(animation: c, builder: (_, _) => OperationPanel(c)),
        ),
      );
      await tester.pump();
      expect(
        tester
            .widget<LinearProgressIndicator>(
              find.byType(LinearProgressIndicator),
            )
            .value,
        isNull,
      );
      await tester.tap(find.byType(Checkbox));
      await tester.pump();
      await tester.tap(find.text('Skip'));
      await tester.pump();
      expect(t.calls.last.$2, {
        'task': 5,
        'conflict': 3,
        'overwrite': 2,
        'all': true,
      });
      done.complete(const BrowserPage());
      await running;
      await tester.pumpWidget(const SizedBox());
      c.dispose();
    },
  );

  testWidgets(
    'creation dialog exposes only native pair/options and platform paths',
    (tester) async {
      const capability = CompressionCapability(
        format: 1,
        method: 1,
        formatName: 'ZIP',
        methodName: 'Store',
        extension: '.zip',
        hasLevels: false,
        minLevel: 0,
        maxLevel: 0,
        defaultLevel: 0,
        permissions: false,
        timestamps: true,
      );
      Map<String, Object?>? submitted;
      await tester.pumpWidget(
        localized(
          Builder(
            builder: (context) => TextButton(
              onPressed: () async {
                submitted = await showDialog<Map<String, Object?>>(
                  context: context,
                  builder: (_) => CreateArchiveDialog(
                    capabilities: const [capability],
                    platform: OperationPicker(),
                  ),
                );
              },
              child: const Text('launch'),
            ),
          ),
        ),
      );
      await tester.tap(find.text('launch'));
      await tester.pumpAndSettle();
      expect(find.text('ZIP · Store'), findsOneWidget);
      expect(find.text('Password'), findsNothing);
      expect(find.text('Preserve permissions'), findsNothing);
      await tester.tap(find.text('Add files'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Add folder'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Choose output'));
      await tester.pumpAndSettle();
      await tester.tap(find.widgetWithText(FilledButton, 'Create archive'));
      await tester.pumpAndSettle();
      expect(submitted?['paths'], ['file-input', 'directory-input']);
      expect(submitted?['destination'], 'archive.zip');
      expect(submitted?['permissions'], false);
      expect(submitted?['timestamps'], true);
    },
  );
}
