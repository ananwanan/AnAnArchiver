import 'dart:async';

import 'package:flutter/foundation.dart';

import '../platform/settings_store.dart';

class SettingsController extends ChangeNotifier {
  SettingsController(this.store);
  final SettingsStore store;
  Preferences value = const Preferences();
  Object? error;
  bool ready = false, blocked = false, _disposed = false;
  Preferences? _pending;
  Future<void>? _saving;
  Future<void> load() async {
    try {
      value = await store.load();
      error = null;
      blocked = false;
    } catch (e) {
      error = e;
      blocked = true;
    }
    ready = true;
    if (!_disposed) notifyListeners();
  }

  void update({
    int? theme,
    String? language,
    List<String>? recent,
    int? width,
    int? height,
    bool? maximized,
  }) {
    if (_disposed || !ready || blocked) return;
    value = Preferences(
      theme: theme ?? value.theme,
      language: language ?? value.language,
      recent: recent ?? value.recent,
      width: width ?? value.width,
      height: height ?? value.height,
      maximized: maximized ?? value.maximized,
      revision: value.revision,
      migration: value.migration,
    );
    _pending = value; // one coalesced snapshot, including during ongoing IO
    _saving ??= _drain();
    notifyListeners();
  }

  Future<void> _drain() async {
    await Future<void>.delayed(Duration.zero);
    while (_pending != null) {
      final next = _pending!;
      _pending = null;
      try {
        final saved = await store.save(next, value.revision);
        value = Preferences.parse(
          value.toJson()..['revision'] = saved.revision,
        );
        error = null;
      } catch (e) {
        error = e;
        blocked = true;
        _pending = null;
        break;
      }
    }
    _saving = null;
    if (!_disposed) notifyListeners();
  }

  void remember(String path) =>
      update(recent: rememberRecent(value.recent, path));
  void remove(String path) =>
      update(recent: value.recent.where((p) => p != path).toList());
  void clear() => update(recent: []);
  Future<void> flush() async {
    await _saving;
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    super.dispose();
  }
}
