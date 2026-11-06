import 'package:file_selector/file_selector.dart' as picker;
import 'package:flutter/services.dart';

import 'dart:async';
import 'dart:io';

abstract interface class DesktopAdapter {
  Future<String?> chooseArchive();
  Future<void> setTitle(String title);
  Future<String?> chooseDirectory();
  Future<List<String>> chooseInputs();
  Future<String?> chooseOutput(String suggestedName);
}

abstract interface class DesktopEvents {
  Stream<List<String>> get openFiles;
  Future<List<String>> ready();
  Future<Map<String, Object?>> windowState();
  Future<void> restoreWindow(Map<String, Object?> state);
  Future<void> dispose();
}

class SystemDesktopAdapter implements DesktopAdapter, DesktopEvents {
  SystemDesktopAdapter() {
    _channel.setMethodCallHandler((call) async {
      if (call.method == 'openFiles' && call.arguments is List) {
        _open.add((call.arguments as List).cast<String>());
      }
    });
  }
  final _channel = const MethodChannel('litearc/window');
  final _open = StreamController<List<String>>.broadcast();
  bool _disposed = false;
  @override
  Stream<List<String>> get openFiles => _open.stream;
  @override
  Future<List<String>> ready() async =>
      (await _channel.invokeListMethod<String>('ready')) ?? [];
  @override
  Future<Map<String, Object?>> windowState() async => Map<String, Object?>.from(
    await _channel.invokeMapMethod<String, Object?>('state') ?? {},
  );
  @override
  Future<void> restoreWindow(Map<String, Object?> state) =>
      _channel.invokeMethod('restore', state);
  @override
  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    if (Platform.isWindows) await _channel.invokeMethod<void>('closing');
    _channel.setMethodCallHandler(null);
    await _open.close();
  }

  @override
  Future<String?> chooseDirectory() => picker.getDirectoryPath();
  @override
  Future<List<String>> chooseInputs() async =>
      (await picker.openFiles()).map((file) => file.path).toList();
  @override
  Future<String?> chooseOutput(String suggestedName) async =>
      (await picker.getSaveLocation(suggestedName: suggestedName))?.path;
  @override
  Future<void> setTitle(String title) =>
      const MethodChannel('litearc/window').invokeMethod<void>('title', title);
  @override
  Future<String?> chooseArchive() async {
    // Deliberately allow all files: format recognition belongs to Application.
    final file = await picker.openFile();
    return file?.path;
  }
}
