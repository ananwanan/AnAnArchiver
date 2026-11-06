import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:litearc/platform/settings_store.dart';
import 'package:litearc/controllers/settings_controller.dart';
import 'package:path/path.dart' as p;

void main() {
  late Directory temp;
  setUp(() async {
    temp = await Directory.systemTemp.createTemp('litearc-settings-');
  });
  tearDown(() async {
    await temp.delete(recursive: true);
  });
  String file(String name) => p.join(temp.path, name);
  test('first Qt migration preserves known theme/list, no stat and repeated start ignores legacy', () async {
    final missing = file('不存在.zip');
    final store = SettingsStore(
      temp.path,
      legacyFixture: {
        'theme': 2,
        'recent': [missing, missing],
      },
    );
    final first = await store.load();
    expect(first.theme, 2);
    expect(first.recent, [missing]);
    expect(first.migration, 'imported');
    expect(await File(missing).exists(), false);
    final next = await SettingsStore(
      temp.path,
      legacyFixture: {'theme': 'unknown'},
    ).load();
    expect(next.toJson(), first.toJson());
    expect(await File(file('preferences.json.new')).exists(), false);
  });
  test(
    'damaged/unknown data is never overwritten or marked migrated',
    () async {
      final store = SettingsStore(temp.path, legacyFixture: {'theme': 42});
      await expectLater(store.load(), throwsFormatException);
      expect(await File(file('preferences.json')).exists(), false);
      final source = File(file('preferences.json'));
      for (final content in [
        '{broken',
        jsonEncode({'schema': 100}),
      ]) {
        await source.writeAsString(content);
        await expectLater(store.load(), throwsA(anything));
        expect(await source.readAsString(), content);
      }
    },
  );
  test('concurrent writers serialize and reject stale revision without losing committed data', () async {
    final a = SettingsStore(temp.path, legacyFixture: {});
    final b = SettingsStore(temp.path, legacyFixture: {});
    await Future.wait([a.load(), b.load()]);
    final outcomes = await Future.wait([
      a
          .save(const Preferences(theme: 1), 0)
          .then<Object>((v) => v, onError: (Object e) => e),
      b
          .save(const Preferences(theme: 2), 0)
          .then<Object>((v) => v, onError: (Object e) => e),
    ]);
    expect(outcomes.whereType<Preferences>().length, 1);
    expect(outcomes.whereType<FileSystemException>().length, 1);
    expect((await a.load()).revision, 1);
  });
  test(
    'controller coalesces writes and recent paths are bounded and deduplicated',
    () async {
      final c = SettingsController(SettingsStore(temp.path, legacyFixture: {}));
      await c.load();
      for (var i = 0; i < 100; i++) {
        c.remember(file('archive-$i.zip'));
        c.update(theme: i % 3);
      }
      c.remember(file('archive-99.zip'));
      await c.flush();
      expect(c.value.recent.length, 20);
      expect(c.value.recent.first, file('archive-99.zip'));
      final loaded = await c.store.load();
      expect(loaded.recent, c.value.recent);
      expect(loaded.theme, 0);
      expect(loaded.revision, lessThan(100));
      c.remove(loaded.recent.first);
      await c.flush();
      expect(c.value.recent.length, 19);
      c.clear();
      await c.flush();
      expect((await c.store.load()).recent, isEmpty);
      c.dispose();
    },
  );
  test(
    'strict Qt INI string-list format and rejected QVariant/invalid values',
    () {
      if (Platform.isWindows) {
        expect(
          recentKey(r'\\?\UNC\server\share\a.zip'),
          recentKey(r'\\server\share\A.zip'),
        );
      }
      expect(parseQtIni('[archives]\nrecent=@Invalid()'), {
        'recent': <String>[],
      });
      expect(parseQtIni('[archives]\nrecent="/tmp/trailing space "'), {
        'recent': ['/tmp/trailing space '],
      });
      expect(
        parseQtIni(
          '[appearance]\ntheme=2\n[archives]\nrecent="/tmp/中文, a.zip", /tmp/second.zip',
        ),
        {
          'theme': 2,
          'recent': ['/tmp/中文, a.zip', '/tmp/second.zip'],
        },
      );
      expect(
        () => parseQtIni('[archives]\nrecent=@Variant(unknown)'),
        throwsFormatException,
      );
      expect(
        () => parseQtIni('[archives]\nrecent="unfinished'),
        throwsFormatException,
      );
    },
  );
}
