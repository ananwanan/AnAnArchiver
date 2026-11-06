import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:litearc_binding/litearc_binding.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:litearc/main.dart';
import 'package:litearc/controllers/browser_controller.dart';
import 'package:litearc/platform/desktop_adapter.dart';

import 'support/synthetic_transport.dart';

class Picker implements DesktopAdapter {
  @override
  Future<String?> chooseDirectory() async => null;
  @override
  Future<List<String>> chooseInputs() async => [];
  @override
  Future<String?> chooseOutput(String suggestedName) async => null;
  @override
  Future<void> setTitle(String title) async {}
  @override
  Future<String?> chooseArchive() async => 'synthetic';
}

class FoldersTransport extends SyntheticTransport {
  String folder = '';
  @override
  Future<BrowserPage> request(
    String op, [
    Map<String, Object?> args = const {},
  ]) async {
    if (op == 'query') {
      folder = args['folder'] as String;
      return BrowserPage(
        archive: 1,
        query: ++query,
        total: folder.isEmpty ? 1 : 0,
      );
    }
    if (op == 'page') {
      return BrowserPage(
        archive: 1,
        query: query,
        total: 1,
        rows: const [
          BrowserRow(
            identity: 123,
            index: -1,
            name: 'empty',
            path: 'empty',
            folder: true,
            type: 2,
            flags: 0,
            size: 0,
            modified: 0,
          ),
        ],
      );
    }
    return super.request(op, args);
  }
}

void main() {
  testWidgets('double click and Enter navigate folders, Up returns to root', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1280, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final c = BrowserController(FoldersTransport());
    await c.open('synthetic');
    await tester.pumpWidget(
      LiteArcApp(
        controller: c,
        platform: Picker(),
        initialLocale: const Locale('en'),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text('empty'));
    await tester.pump(const Duration(milliseconds: 350));
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    expect(c.folder, 'empty');
    await tester.tap(find.byTooltip('Up one level'));
    await tester.pumpAndSettle();
    expect(c.folder, '');
    await tester.tap(find.text('empty'));
    await tester.pump(const Duration(milliseconds: 50));
    await tester.tap(find.text('empty'));
    await tester.pumpAndSettle();
    expect(c.folder, 'empty');
    await tester.pumpWidget(const SizedBox());
  });
  testWidgets(
    'Home, platform picker, virtual list, search, language and close',
    (tester) async {
      tester.view.physicalSize = const Size(1280, 720);
      tester.view.devicePixelRatio = 1;
      addTearDown(() => tester.view.resetPhysicalSize());
      final t = SyntheticTransport(count: 1000000);
      final c = BrowserController(t);
      await tester.pumpWidget(LiteArcApp(controller: c, platform: Picker()));
      await tester.pumpAndSettle();
      expect(find.text('Your archives, clearly arranged.'), findsOneWidget);
      expect(t.requests, isEmpty);
      await tester.tap(find.text('Open archive').last);
      await tester.pumpAndSettle();
      expect(find.text('file-0.txt'), findsOneWidget);
      expect(find.byType(ListView), findsNWidgets(2));
      expect(find.text('file-999999.txt'), findsNothing);
      await tester.tap(find.text('file-0.txt'));
      await tester.pump(const Duration(milliseconds: 350));
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.keyA);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await tester.pumpAndSettle();
      expect(c.selection.count, 1000000);

      await tester.enterText(find.byType(TextField), 'needle');
      await tester.pump(const Duration(milliseconds: 200));
      await tester.pumpAndSettle();
      expect(c.search, 'needle');
      expect(c.selection.count, 0);
      await tester.tap(find.byTooltip('Language'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('简体中文'));
      await tester.pumpAndSettle();
      expect(find.text('轻匣'), findsOneWidget);
      await tester.tap(find.byTooltip('关闭归档'));
      await tester.pumpAndSettle();
      expect(find.text('归档文件，清晰有序。'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
      await tester.pumpAndSettle();
      expect(t.closed, true);
    },
  );
  for (final scale in [1.0, 1.25, 1.5, 2.0]) {
    testWidgets('Home and browser render at $scale text scale', (tester) async {
      tester.view.physicalSize = const Size(1600, 1000);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      tester.platformDispatcher.textScaleFactorTestValue = scale;
      addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
      final c = BrowserController(SyntheticTransport());
      await c.open('synthetic');
      await tester.pumpWidget(
        MediaQuery(
          data: MediaQueryData(textScaler: TextScaler.linear(scale)),
          child: LiteArcApp(controller: c, platform: Picker()),
        ),
      );
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    });
  }
}
