import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:litearc/controllers/browser_controller.dart';
import 'package:litearc/main.dart';
import 'package:litearc/platform/desktop_adapter.dart';
import 'package:litearc_binding/litearc_binding.dart';

// Explicit fixture adapter tests the real desktop renderer + worker + backend.
// Native OS dialogs are separately exercised in the production Release app.
class FixtureOperationsPicker implements DesktopAdapter {
  FixtureOperationsPicker(this.source, this.input, this.output);
  final String source, input, output;
  @override
  Future<void> setTitle(String title) => SystemDesktopAdapter().setTitle(title);
  @override
  Future<String?> chooseArchive() async => source;
  @override
  Future<String?> chooseDirectory() async => null;
  @override
  Future<List<String>> chooseInputs() async => [input];
  @override
  Future<String?> chooseOutput(String suggestedName) async => output;
}

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  testWidgets(
    'real desktop extraction, creation, password, conflict, cancellation and shutdown',
    (tester) async {
      // IntegrationTest leaves the platform text input active by default.
      // Register the test text channel for deterministic tester.enterText;
      // production native keyboard input is checked separately in Release.
      tester.testTextInput.register();
      addTearDown(tester.testTextInput.unregister);
      const fixtures = String.fromEnvironment('OPERATIONS_FIXTURES');
      expect(fixtures, isNotEmpty);
      final temp = await Directory.systemTemp.createTemp('litearc-desktop-f4-');
      final client = await BrowserClient.start();
      final c = BrowserController(client);
      Future<void> until(bool Function() done) async {
        for (var i = 0; i < 1000 && !done(); i++) {
          await tester.pump(const Duration(milliseconds: 20));
          await Future<void>.delayed(const Duration(milliseconds: 20));
        }
        expect(
          done(),
          isTrue,
          reason:
              'desktop operation deadline; busy=${c.operations.busy}; password=${c.operations.waitingForPassword}; passwordError=${c.operations.passwordError?.code}; error=${c.operations.error?.code}; state=${c.operations.progress?.state}; snapshotError=${c.operations.progress?.error?.code}',
        );
        await tester.pump();
      }

      final input = File('${temp.path}/输入🐈.txt');
      await input.writeAsString('desktop roundtrip');
      final created = '${temp.path}/created.zip';
      try {
        await tester.pumpWidget(
          LiteArcApp(
            controller: c,
            platform: FixtureOperationsPicker(
              '$fixtures/browse.xxx',
              input.path,
              created,
            ),
            initialLocale: const Locale('en'),
          ),
        );
        await tester.pumpAndSettle();
        await tester.tap(find.text('Open archive').last);
        await until(() => c.query != 0 && !c.busy && c.rowAt(0) != null);
        expect(c.error, isNull);
        c.setSearch('CAFÉ');
        await until(() => c.query != 0 && !c.busy);
        await c.extract('${temp.path}/all', all: true, overwrite: 1);
        expect(c.operations.error, isNull);
        expect(
          await File('${temp.path}/all/duplicate.txt').readAsString(),
          'second',
        );
        expect(
          await File('${temp.path}/all/隐式/子目录/可可🐄🐈.txt').readAsString(),
          'hello',
        );
        expect(await Directory('${temp.path}/all/empty').exists(), isTrue);
        await c.setSort(1);
        c.setSearch('duplicate');
        await until(() => c.query != 0 && !c.busy && c.rowAt(0) != null);
        c.choose(
          1,
        ); // ascending size => second duplicate, original ordinal retained
        await until(() => c.selectionHandle != 0);
        await c.extract('${temp.path}/selected', all: false);
        expect(
          await File('${temp.path}/selected/duplicate.txt').readAsString(),
          'second',
        );

        final conflictOperation = c.extract(
          '${temp.path}/selected',
          all: false,
        );
        await until(() => c.operations.progress?.conflict != null);
        expect(find.text('The destination already exists'), findsOneWidget);
        await tester.tap(find.byType(Checkbox));
        await tester.pump();
        await tester.tap(find.text('Overwrite'));
        await tester.pump();
        await until(() => !c.operations.busy);
        await conflictOperation;
        expect(c.operations.progress?.state, 5);

        await c.open('$fixtures/encrypted.zip');
        final decrypt = c.extract(
          '${temp.path}/secret',
          all: true,
          overwrite: 1,
        );
        await until(() => c.operations.waitingForPassword);
        expect(c.operations.passwordError?.code, 9);
        await tester.enterText(
          find.byKey(const Key('operation-password')),
          'wrong-test-password',
        );
        expect(
          tester
              .widget<TextField>(find.byKey(const Key('operation-password')))
              .controller!
              .text
              .isNotEmpty,
          isTrue,
          reason: 'password field received fixture input',
        );
        await tester.pump();
        await tester.tap(find.text('Retry'));
        await until(() => c.operations.passwordError?.code == 8);
        expect(find.text('Incorrect password.'), findsOneWidget);
        await tester.enterText(
          find.byKey(const Key('operation-password')),
          'test-only-passphrase',
        );
        await tester.tap(find.text('Retry'));
        await until(() => !c.operations.busy);
        await decrypt;
        expect(c.operations.error, isNull);
        expect(
          await File('${temp.path}/secret/secret.txt').readAsString(),
          'secret',
        );
        final cancelPassword = c.extract(
          '${temp.path}/cancel-password',
          all: true,
          overwrite: 1,
        );
        await until(() => c.operations.waitingForPassword);
        await tester.tap(find.text('Cancel').last);
        await until(() => !c.operations.busy);
        await cancelPassword;
        expect(c.operations.error?.code, 12);
        expect(
          await File('${temp.path}/cancel-password/secret.txt').exists(),
          isFalse,
        );
        c.operations.dismiss();
        await tester.pump();

        await tester.tap(find.text('Create archive').first);
        await tester.pumpAndSettle();
        await tester.tap(find.text('Add files'));
        await tester.pumpAndSettle();
        await tester.tap(find.text('Choose output'));
        await tester.pumpAndSettle();
        await tester.tap(find.widgetWithText(FilledButton, 'Create archive'));
        await until(
          () => c.operations.progress?.terminal == true && !c.operations.busy,
        );
        expect(c.operations.error, isNull);
        await c.open(created);
        expect(c.entries, 1);
        await c.extract('${temp.path}/roundtrip', all: true);
        expect(
          await File('${temp.path}/roundtrip/输入🐈.txt').readAsString(),
          'desktop roundtrip',
        );

        // Shutdown uses the same owned-worker path as AppLifecycleListener.
        final waiting = c.extract('${temp.path}/roundtrip', all: true);
        await until(() => c.operations.progress?.conflict != null);
        final watch = Stopwatch()..start();
        await c.shutdown().timeout(const Duration(seconds: 5));
        await waiting;
        binding.reportData = {
          'mode': const bool.fromEnvironment('dart.vm.product')
              ? 'Release'
              : 'Profile/debug',
          'shutdownWhileConflictMs': watch.elapsedMilliseconds,
          'maxRssBytes': ProcessInfo.maxRss,
          'realOperationFlow': 'passed',
        };
        // ignore: avoid_print
        print('LITEARC_DESKTOP_OPERATIONS ${binding.reportData}');
      } finally {
        await c.shutdown();
        await tester.pumpWidget(const SizedBox());
        await temp.delete(recursive: true);
      }
    },
  );
}
