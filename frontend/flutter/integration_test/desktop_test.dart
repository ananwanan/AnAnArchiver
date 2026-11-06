import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:litearc/controllers/browser_controller.dart';
import 'package:litearc/controllers/desktop_controller.dart';
import 'package:litearc/controllers/settings_controller.dart';
import 'package:litearc/main.dart';
import 'package:litearc/platform/settings_store.dart';
import 'package:litearc/widgets/desktop_transfer.dart';
import 'package:litearc_binding/litearc_binding.dart';

import 'operations_test.dart' show FixtureOperationsPicker;

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  testWidgets(
    'F5 real renderer/binding, settings, task center and durable export lifecycle',
    (tester) async {
      const fixtures = String.fromEnvironment('OPERATIONS_FIXTURES');
      expect(fixtures, isNotEmpty);
      final temp = await Directory.systemTemp.createTemp('litearc-f5-desktop-');
      final settings = SettingsController(
        SettingsStore('${temp.path}/settings', legacyFixture: {'theme': 1}),
      );
      await settings.load();
      final client = await BrowserClient.start();
      final c = BrowserController(client);
      final e = ExportController(c, '${temp.path}/exports');
      Future<void> until(bool Function() done) async {
        for (var i = 0; i < 1000 && !done(); i++) {
          await tester.pump(const Duration(milliseconds: 20));
          await Future<void>.delayed(const Duration(milliseconds: 20));
        }
        expect(done(), true);
        await tester.pump();
      }

      try {
        await tester.pumpWidget(
          LiteArcApp(
            controller: c,
            settings: settings,
            platform: FixtureOperationsPicker('$fixtures/browse.xxx', '', ''),
            initialLocale: const Locale('en'),
          ),
        );
        await tester.tap(find.text('Open archive').last);
        await until(() => c.query != 0 && !c.busy && c.rowAt(0) != null);
        expect(c.error, isNull);
        expect(
          recentKey(settings.value.recent.single),
          recentKey('$fixtures/browse.xxx'),
        );
        c.setSearch('duplicate');
        await until(() => !c.busy && c.total == 2 && c.rowAt(1) != null);
        c.choose(1);
        await until(() => c.canExtractSelected);
        final preparing = e.prepare();
        await until(() => !e.preparing);
        await preparing;
        expect(c.operations.error, isNull);
        expect(e.paths.length, 1);
        expect(await File(e.paths.single).readAsString(), 'second');
        final output = await e.handoff();
        final root = e.root!;
        await c.closeArchive();
        await tester.pumpAndSettle();
        expect(await File(output.single).readAsString(), 'second');
        await tester.tap(find.byTooltip('Task center'));
        await tester.pumpAndSettle();
        expect(find.byType(AlertDialog), findsOneWidget);
        await tester.tap(find.text('Close').last);
        await tester.pumpAndSettle();
        settings.update(theme: 2, language: 'zh-CN');
        await settings.flush();
        expect((await settings.store.load()).theme, 2);
        expect(find.byType(ExportDragHandle), findsNothing);
        final shutdown = Stopwatch()..start();
        e.dispose();
        await c.shutdown();
        await settings.flush();
        expect(await File(output.single).readAsString(), 'second');
        final restarted = await BrowserClient.start();
        try {
          expect(
            (await restarted.request('exportControl', {
              'action': 7,
              'path': '${temp.path}/exports',
            })).paths,
            [root],
          );
          await restarted.request('exportControl', {'action': 4, 'path': root});
        } finally {
          await restarted.close();
        }
        binding.reportData = {
          'mode': 'Profile Flutter + Release C ABI; fixture picker; OS drag separate',
          'shutdown_and_restart_ms': shutdown.elapsedMilliseconds,
          'maxRss': ProcessInfo.maxRss,
        };
      } finally {
        e.dispose();
        await tester.pumpWidget(const SizedBox());
        await c.shutdown();
        await settings.flush();
        await temp.delete(recursive: true);
      }
    },
  );
}
